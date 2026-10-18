#include "data_collector.hpp"
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
using Statement = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;
void execute(sqlite3* db, const std::string& sql) {
    char* error = nullptr;
    if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &error) != SQLITE_OK) {
        const std::string message = error ? error : sqlite3_errmsg(db); sqlite3_free(error); throw std::runtime_error("SQLite: " + message);
    }
}
Statement prepare(sqlite3* db, const char* sql) {
    sqlite3_stmt* stmt = nullptr;
    const int result = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    Statement holder(stmt, sqlite3_finalize);
    if (result != SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(db)); return holder;
}
void text(sqlite3_stmt* stmt, int index, const std::string& value) {
    if (sqlite3_bind_text(stmt, index, value.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK) throw std::runtime_error("Falha ao vincular texto SQLite");
}
void done(sqlite3* db, sqlite3_stmt* stmt) { if (sqlite3_step(stmt) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db)); }
}
DataCollector::DataCollector(const std::string& database_path) : db_path(database_path) {}
DataCollector::~DataCollector() { try { stop(); } catch (const std::exception& e) { std::cerr << "Coletor de dados: " << e.what() << '\n'; } }
void DataCollector::openDatabase() {
    const auto parent = std::filesystem::path(db_path).parent_path(); if (!parent.empty()) std::filesystem::create_directories(parent);
    if (sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
        const std::string error = db ? sqlite3_errmsg(db) : "Falha ao criar conexão"; closeDatabase(); throw std::runtime_error(error);
    }
    sqlite3_busy_timeout(db, 3000); execute(db, "PRAGMA foreign_keys=ON;");
}
void DataCollector::createTable() {
    auto version = prepare(db, "PRAGMA user_version");
    if (sqlite3_step(version.get()) != SQLITE_ROW || sqlite3_column_int(version.get(), 0) > 1) throw std::runtime_error("Versão de banco incompatível");
    version.reset();
    execute(db, "BEGIN IMMEDIATE");
    try {
        execute(db, R"SQL(CREATE TABLE IF NOT EXISTS robot_events (
            id INTEGER PRIMARY KEY AUTOINCREMENT, timestamp TEXT NOT NULL,
            robot_id INTEGER NOT NULL, team_id INTEGER, bay_id INTEGER,
            position_x INTEGER NOT NULL, position_y INTEGER NOT NULL,
            event_type INTEGER NOT NULL, description TEXT NOT NULL, destination TEXT);
            CREATE TABLE IF NOT EXISTS runs (id INTEGER PRIMARY KEY AUTOINCREMENT,
                started_at_ms INTEGER NOT NULL, ended_at_ms INTEGER, status TEXT NOT NULL);
        )SQL");
        std::vector<std::string> columns;
        { auto stmt = prepare(db, "PRAGMA table_info(robot_events)"); while (sqlite3_step(stmt.get()) == SQLITE_ROW) columns.emplace_back(reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 1))); }
        const std::vector<std::pair<std::string, std::string>> additions{{"run_id","INTEGER"},{"cargo_id","INTEGER"},{"mission_id","INTEGER"},{"generation","INTEGER"},{"source","TEXT"},{"source_sequence","INTEGER"},{"occurred_at_ms","INTEGER"}};
        for (const auto& field : additions) {
            bool found = false; for (const auto& column : columns) if (column == field.first) found = true;
            if (!found) execute(db, "ALTER TABLE robot_events ADD COLUMN " + field.first + " " + field.second);
        }
        execute(db, R"SQL(CREATE UNIQUE INDEX IF NOT EXISTS event_identity ON robot_events
                (run_id,source,generation,source_sequence,event_type) WHERE run_id IS NOT NULL;
            CREATE TABLE IF NOT EXISTS cargo_transports (
                run_id INTEGER NOT NULL REFERENCES runs(id), mission_id INTEGER NOT NULL,
                cargo_id INTEGER NOT NULL, bay_id INTEGER, destination TEXT, status TEXT NOT NULL,
                pickup_at_ms INTEGER, delivered_at_ms INTEGER, PRIMARY KEY(run_id,mission_id));
            CREATE TABLE IF NOT EXISTS cargo_members (
                run_id INTEGER NOT NULL, mission_id INTEGER NOT NULL, robot_id INTEGER NOT NULL,
                PRIMARY KEY(run_id,mission_id,robot_id),
                FOREIGN KEY(run_id,mission_id) REFERENCES cargo_transports(run_id,mission_id));
            PRAGMA user_version=1;
        )SQL");
        execute(db, "COMMIT");
    } catch (...) { sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr); throw; }
}
void DataCollector::closeDatabase() noexcept { if (db) { if (sqlite3_close(db) == SQLITE_OK) db = nullptr; } }
void DataCollector::start() {
    std::lock_guard<std::mutex> lifecycle(lifecycle_mutex);
    { std::lock_guard<std::mutex> lock(queue_mutex); if (state == CollectorState::FAILED) throw std::runtime_error("Coletor falhou e não pode reiniciar"); if (state != CollectorState::STOPPED) return; }
    try {
        openDatabase(); createTable();
        auto session = prepare(db, "INSERT INTO runs(started_at_ms,status) VALUES (?,'em execução')");
        sqlite3_bind_int64(session.get(), 1, epochMilliseconds()); done(db, session.get()); session.reset();
        { std::lock_guard<std::mutex> lock(queue_mutex); run_id = sqlite3_last_insert_rowid(db); state = CollectorState::RUNNING; worker_error = nullptr; }
        collector_thread = std::thread(&DataCollector::collectorLoop, this);
    } catch (...) { { std::lock_guard<std::mutex> lock(queue_mutex); state = CollectorState::STOPPED; } closeDatabase(); throw; }
}
void DataCollector::stop() {
    std::lock_guard<std::mutex> lifecycle(lifecycle_mutex);
    { std::lock_guard<std::mutex> lock(queue_mutex); if (state == CollectorState::STOPPED) return; if (!db && worker_error) std::rethrow_exception(worker_error); if (state == CollectorState::RUNNING) state = CollectorState::STOPPING; }
    queue_cv.notify_all(); if (collector_thread.joinable()) collector_thread.join();
    std::exception_ptr error;
    { std::lock_guard<std::mutex> lock(queue_mutex); error = worker_error; }
    try {
        auto session = prepare(db, "UPDATE runs SET ended_at_ms=?,status=? WHERE id=?");
        sqlite3_bind_int64(session.get(), 1, epochMilliseconds()); text(session.get(), 2, error ? "falhou" : "encerrada"); sqlite3_bind_int64(session.get(), 3, run_id); done(db, session.get());
    } catch (...) { if (!error) error = std::current_exception(); }
    closeDatabase();
    { std::lock_guard<std::mutex> lock(queue_mutex); if (!error) state = CollectorState::STOPPED; else { state = CollectorState::FAILED; worker_error = error; } }
    if (error) std::rethrow_exception(error);
}
void DataCollector::enqueueEvent(const RobotEvent& event) {
    { std::lock_guard<std::mutex> lock(queue_mutex); if (state != CollectorState::RUNNING) throw std::runtime_error("Coletor não aceita novos eventos"); if (event_queue.size() >= 10000) throw std::runtime_error("Fila do coletor esgotada"); event_queue.push(event); }
    queue_cv.notify_one();
}
void DataCollector::collectorLoop() noexcept {
    try {
        while (true) {
            std::unique_lock<std::mutex> lock(queue_mutex);
            queue_cv.wait(lock, [&] { return !event_queue.empty() || state != CollectorState::RUNNING; });
            if (event_queue.empty() && state == CollectorState::STOPPING) break;
            auto event = event_queue.front(); event_queue.pop(); lock.unlock(); writeEventToDatabase(event);
        }
    } catch (...) { std::lock_guard<std::mutex> lock(queue_mutex); worker_error = std::current_exception(); state = CollectorState::FAILED; }
}
void DataCollector::checkError() const { std::lock_guard<std::mutex> lock(queue_mutex); if (worker_error) std::rethrow_exception(worker_error); }
EntityId DataCollector::runId() const { std::lock_guard<std::mutex> lock(queue_mutex); return run_id; }
void DataCollector::writeEventToDatabase(const RobotEvent& e) {
    execute(db, "BEGIN IMMEDIATE");
    try {
        auto stmt = prepare(db, R"SQL(INSERT INTO robot_events
            (timestamp,robot_id,team_id,bay_id,position_x,position_y,event_type,description,destination,
             run_id,cargo_id,mission_id,generation,source,source_sequence,occurred_at_ms)
            VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?) ON CONFLICT(run_id,source,generation,source_sequence,event_type) WHERE run_id IS NOT NULL DO NOTHING)SQL");
        text(stmt.get(), 1, e.getTimestamp()); sqlite3_bind_int(stmt.get(), 2, e.getRobotId());
        if (e.getTeamId() >= 0) sqlite3_bind_int(stmt.get(), 3, e.getTeamId());
        if (e.getBayId() >= 0) sqlite3_bind_int(stmt.get(), 4, e.getBayId());
        sqlite3_bind_int(stmt.get(), 5, e.getPosition().x); sqlite3_bind_int(stmt.get(), 6, e.getPosition().y);
        sqlite3_bind_int(stmt.get(), 7, static_cast<int>(e.getType())); text(stmt.get(), 8, e.getDescription()); text(stmt.get(), 9, e.getDestination());
        sqlite3_bind_int64(stmt.get(), 10, run_id); sqlite3_bind_int64(stmt.get(), 11, e.cargoId()); sqlite3_bind_int64(stmt.get(), 12, e.missionId());
        sqlite3_bind_int64(stmt.get(), 13, e.generation()); text(stmt.get(), 14, e.source()); sqlite3_bind_int64(stmt.get(), 15, e.sourceSequence()); sqlite3_bind_int64(stmt.get(), 16, e.occurredAt());
        done(db, stmt.get()); stmt.reset();
        if (e.missionId() > 0 && (e.getType() == EventType::CARGO_PICKUP || e.getType() == EventType::CARGO_DELIVERED)) {
            auto transport = prepare(db, R"SQL(INSERT INTO cargo_transports
                (run_id,mission_id,cargo_id,bay_id,destination,status,pickup_at_ms,delivered_at_ms)
                VALUES (?,?,?,?,?,?,?,?) ON CONFLICT(run_id,mission_id) DO UPDATE SET
                status=excluded.status, delivered_at_ms=COALESCE(excluded.delivered_at_ms,cargo_transports.delivered_at_ms),
                pickup_at_ms=COALESCE(cargo_transports.pickup_at_ms,excluded.pickup_at_ms))SQL");
            sqlite3_bind_int64(transport.get(), 1, run_id); sqlite3_bind_int64(transport.get(), 2, e.missionId()); sqlite3_bind_int64(transport.get(), 3, e.cargoId()); sqlite3_bind_int(transport.get(), 4, e.getBayId()); text(transport.get(), 5, e.getDestination());
            const bool delivered = e.getType() == EventType::CARGO_DELIVERED; text(transport.get(), 6, delivered ? "entregue" : "em transporte");
            sqlite3_bind_int64(transport.get(), delivered ? 8 : 7, e.occurredAt()); done(db, transport.get());
            for (const auto id : e.participants()) {
                auto member = prepare(db, "INSERT OR IGNORE INTO cargo_members(run_id,mission_id,robot_id) VALUES (?,?,?)");
                sqlite3_bind_int64(member.get(), 1, run_id); sqlite3_bind_int64(member.get(), 2, e.missionId()); sqlite3_bind_int(member.get(), 3, id); done(db, member.get());
            }
        }
        execute(db, "COMMIT");
    } catch (...) { sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr); throw; }
}
