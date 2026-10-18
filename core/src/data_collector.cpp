#include "data_collector.hpp"

#include <iostream>
#include <stdexcept>

DataCollector::DataCollector(const std::string& database_path)
    : db_path(database_path) {}

DataCollector::~DataCollector() {
    try {
        stop();
    } catch (const std::exception& e) {
        std::cerr << "DataCollector: " << e.what() << '\n';
    }
}

void DataCollector::openDatabase() {
    const int result = sqlite3_open_v2(
        db_path.c_str(), &db,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr
    );
    if (result != SQLITE_OK) {
        std::string error = db ? sqlite3_errmsg(db) : "falha ao criar conexao";
        closeDatabase();
        throw std::runtime_error("Erro ao abrir SQLite: " + error);
    }
    sqlite3_busy_timeout(db, 3000);
}

void DataCollector::createTable() {
    const char* sql = R"SQL(
        CREATE TABLE IF NOT EXISTS robot_events (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp   TEXT NOT NULL,
            robot_id    INTEGER NOT NULL,
            team_id     INTEGER,
            bay_id      INTEGER,
            position_x  INTEGER NOT NULL,
            position_y  INTEGER NOT NULL,
            event_type  INTEGER NOT NULL,
            description TEXT NOT NULL,
            destination TEXT
        );
    )SQL";

    char* error = nullptr;
    const int result = sqlite3_exec(db, sql, nullptr, nullptr, &error);
    if (result != SQLITE_OK) {
        std::string message = error ? error : "erro desconhecido";
        sqlite3_free(error);
        throw std::runtime_error("Erro ao criar tabela: " + message);
    }
}

void DataCollector::closeDatabase() noexcept {
    if (db != nullptr) {
        const int result = sqlite3_close(db);
        if (result != SQLITE_OK) {
            std::cerr << "Erro ao fechar SQLite: " << sqlite3_errmsg(db) << '\n';
        } else {
            db = nullptr;
        }
    }
}

void DataCollector::start() {
    std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex);
    {
        std::lock_guard<std::mutex> queue_lock(queue_mutex);
        if (state == CollectorState::FAILED) {
            throw std::runtime_error("Coletor falhou; nao e possivel reiniciar");
        }
        if (state != CollectorState::STOPPED) return;
    }

    try {
        openDatabase();
        createTable();
        {
            std::lock_guard<std::mutex> queue_lock(queue_mutex);
            state = CollectorState::RUNNING;
            worker_error = nullptr;
        }
        collector_thread = std::thread(&DataCollector::collectorLoop, this);
    } catch (...) {
        {
            std::lock_guard<std::mutex> queue_lock(queue_mutex);
            state = CollectorState::STOPPED;
        }
        closeDatabase();
        throw;
    }
}

void DataCollector::stop() {
    std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex);
    {
        std::lock_guard<std::mutex> queue_lock(queue_mutex);
        if (state == CollectorState::STOPPED) return;
        if (state == CollectorState::RUNNING) {
            state = CollectorState::STOPPING;
        }
    }

    queue_cv.notify_all();
    if (collector_thread.joinable()) {
        collector_thread.join();
    }
    closeDatabase();

    std::exception_ptr error;
    {
        std::lock_guard<std::mutex> queue_lock(queue_mutex);
        error = worker_error;
        if (state != CollectorState::FAILED) {
            state = CollectorState::STOPPED;
        }
    }
    if (error) std::rethrow_exception(error);
}

void DataCollector::enqueueEvent(const RobotEvent& event) {
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        if (state != CollectorState::RUNNING) {
            throw std::runtime_error("DataCollector nao aceita novos eventos");
        }
        event_queue.push(event);
    }
    queue_cv.notify_one();
}

void DataCollector::collectorLoop() noexcept {
    try {
        while (true) {
            std::unique_lock<std::mutex> lock(queue_mutex);
            queue_cv.wait(lock, [this] {
                return !event_queue.empty() || state != CollectorState::RUNNING;
            });
            if (event_queue.empty() && state == CollectorState::STOPPING) {
                break;
            }

            RobotEvent event = event_queue.front();
            event_queue.pop();
            lock.unlock();
            writeEventToDatabase(event);
        }
    } catch (...) {
        std::lock_guard<std::mutex> lock(queue_mutex);
        worker_error = std::current_exception();
        state = CollectorState::FAILED;
    }
}

void DataCollector::writeEventToDatabase(const RobotEvent& event) {
    const char* sql = R"SQL(
        INSERT INTO robot_events (
            timestamp, robot_id, team_id, bay_id, position_x, position_y,
            event_type, description, destination
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);
    )SQL";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(sqlite3_errmsg(db));
    }

    const Pos pos = event.getPosition();
    sqlite3_bind_text(stmt, 1, event.getTimestamp().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, event.getRobotId());
    if (event.getTeamId() < 0) sqlite3_bind_null(stmt, 3);
    else sqlite3_bind_int(stmt, 3, event.getTeamId());
    if (event.getBayId() < 0) sqlite3_bind_null(stmt, 4);
    else sqlite3_bind_int(stmt, 4, event.getBayId());
    sqlite3_bind_int(stmt, 5, pos.x);
    sqlite3_bind_int(stmt, 6, pos.y);
    sqlite3_bind_int(stmt, 7, static_cast<int>(event.getType()));
    sqlite3_bind_text(stmt, 8, event.getDescription().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 9, event.getDestination().c_str(), -1, SQLITE_TRANSIENT);

    const int result = sqlite3_step(stmt);
    std::string error = (result == SQLITE_DONE) ? "" : sqlite3_errmsg(db);
    sqlite3_finalize(stmt);
    if (result != SQLITE_DONE) {
        throw std::runtime_error("Erro ao inserir evento no SQLite: " + error);
    }
}
