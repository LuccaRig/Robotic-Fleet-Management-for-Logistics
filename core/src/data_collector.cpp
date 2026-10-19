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
        const std::string mensagem = error ? error : sqlite3_errmsg(db); sqlite3_free(error); throw std::runtime_error("SQLite: " + mensagem);
    }
}
Statement prepare(sqlite3* db, const char* sql) {
    sqlite3_stmt* stmt = nullptr;
    const int result = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    Statement holder(stmt, sqlite3_finalize);
    if (result != SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(db)); return holder;
}
void texto(sqlite3_stmt* stmt, int index, const std::string& value) {
    if (sqlite3_bind_text(stmt, index, value.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK) throw std::runtime_error("Falha ao vincular texto SQLite");
}
void done(sqlite3* db, sqlite3_stmt* stmt) { if (sqlite3_step(stmt) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db)); }
}
ColetorDados::ColetorDados(const std::string& caminho_banco) : caminho_banco(caminho_banco) {}
ColetorDados::~ColetorDados() { try { parar(); } catch (const std::exception& e) { std::cerr << "Coletor de dados: " << e.what() << '\n'; } }
void ColetorDados::openDatabase() {
    const auto parent = std::filesystem::path(caminho_banco).parent_path(); if (!parent.empty()) std::filesystem::create_directories(parent);
    if (sqlite3_open_v2(caminho_banco.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
        const std::string error = db ? sqlite3_errmsg(db) : "Falha ao criar conexão"; closeDatabase(); throw std::runtime_error(error);
    }
    sqlite3_busy_timeout(db, 3000); execute(db, "PRAGMA foreign_keys=ON;");
}
void ColetorDados::createTable() {
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
void ColetorDados::closeDatabase() noexcept { if (db) { if (sqlite3_close(db) == SQLITE_OK) db = nullptr; } }
void ColetorDados::iniciar() {
    std::lock_guard<std::mutex> lifecycle(mutex_ciclo_vida);
    { std::lock_guard<std::mutex> lock(mutex_fila); if (estado == EstadoColetor::FALHOU) throw std::runtime_error("Coletor falhou e não pode reiniciar"); if (estado != EstadoColetor::ENCERRADO) return; }
    try {
        openDatabase(); createTable();
        auto session = prepare(db, "INSERT INTO runs(started_at_ms,status) VALUES (?,'em execução')");
        sqlite3_bind_int64(session.get(), 1, instanteUnixMs()); done(db, session.get()); session.reset();
        { std::lock_guard<std::mutex> lock(mutex_fila); run_id = sqlite3_last_insert_rowid(db); estado = EstadoColetor::EM_EXECUCAO; erro_thread = nullptr; }
        thread_coletora = std::thread(&ColetorDados::collectorLoop, this);
    } catch (...) { { std::lock_guard<std::mutex> lock(mutex_fila); estado = EstadoColetor::ENCERRADO; } closeDatabase(); throw; }
}
void ColetorDados::parar() {
    std::lock_guard<std::mutex> lifecycle(mutex_ciclo_vida);
    { std::lock_guard<std::mutex> lock(mutex_fila); if (estado == EstadoColetor::ENCERRADO) return; if (!db && erro_thread) std::rethrow_exception(erro_thread); if (estado == EstadoColetor::EM_EXECUCAO) estado = EstadoColetor::ENCERRANDO; }
    condicao_fila.notify_all(); if (thread_coletora.joinable()) thread_coletora.join();
    std::exception_ptr error;
    { std::lock_guard<std::mutex> lock(mutex_fila); error = erro_thread; }
    try {
        auto session = prepare(db, "UPDATE runs SET ended_at_ms=?,status=? WHERE id=?");
        sqlite3_bind_int64(session.get(), 1, instanteUnixMs()); texto(session.get(), 2, error ? "falhou" : "encerrada"); sqlite3_bind_int64(session.get(), 3, run_id); done(db, session.get());
    } catch (...) { if (!error) error = std::current_exception(); }
    closeDatabase();
    { std::lock_guard<std::mutex> lock(mutex_fila); if (!error) estado = EstadoColetor::ENCERRADO; else { estado = EstadoColetor::FALHOU; erro_thread = error; } }
    if (error) std::rethrow_exception(error);
}
void ColetorDados::enfileirarEvento(const EventoRobo& event) {
    { std::lock_guard<std::mutex> lock(mutex_fila); if (estado != EstadoColetor::EM_EXECUCAO) throw std::runtime_error("Coletor não aceita novos eventos"); if (fila_eventos.size() >= 10000) throw std::runtime_error("Fila do coletor esgotada"); fila_eventos.push(event); }
    condicao_fila.notify_one();
}
void ColetorDados::collectorLoop() noexcept {
    try {
        while (true) {
            std::unique_lock<std::mutex> lock(mutex_fila);
            condicao_fila.wait(lock, [&] { return !fila_eventos.empty() || estado != EstadoColetor::EM_EXECUCAO; });
            if (fila_eventos.empty() && estado == EstadoColetor::ENCERRANDO) break;
            auto event = fila_eventos.front(); fila_eventos.pop(); lock.unlock(); writeEventToDatabase(event);
        }
    } catch (...) { std::lock_guard<std::mutex> lock(mutex_fila); erro_thread = std::current_exception(); estado = EstadoColetor::FALHOU; }
}
void ColetorDados::verificarFalha() const { std::lock_guard<std::mutex> lock(mutex_fila); if (erro_thread) std::rethrow_exception(erro_thread); }
IdEntidade ColetorDados::idExecucao() const { std::lock_guard<std::mutex> lock(mutex_fila); return run_id; }
void ColetorDados::writeEventToDatabase(const EventoRobo& e) {
    execute(db, "BEGIN IMMEDIATE");
    try {
        auto stmt = prepare(db, R"SQL(INSERT INTO robot_events
            (timestamp,robot_id,team_id,bay_id,position_x,position_y,event_type,description,destination,
             run_id,cargo_id,mission_id,generation,source,source_sequence,occurred_at_ms)
            VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?) ON CONFLICT(run_id,source,generation,source_sequence,event_type) WHERE run_id IS NOT NULL DO NOTHING)SQL");
        texto(stmt.get(), 1, e.instanteTexto()); sqlite3_bind_int(stmt.get(), 2, e.idRobo());
        if (e.idEquipe() >= 0) sqlite3_bind_int(stmt.get(), 3, e.idEquipe());
        if (e.idBaia() >= 0) sqlite3_bind_int(stmt.get(), 4, e.idBaia());
        sqlite3_bind_int(stmt.get(), 5, e.posicao().x); sqlite3_bind_int(stmt.get(), 6, e.posicao().y);
        sqlite3_bind_int(stmt.get(), 7, static_cast<int>(e.tipo())); texto(stmt.get(), 8, e.descricao()); texto(stmt.get(), 9, e.destino());
        sqlite3_bind_int64(stmt.get(), 10, run_id); sqlite3_bind_int64(stmt.get(), 11, e.idCarga()); sqlite3_bind_int64(stmt.get(), 12, e.idMissao());
        sqlite3_bind_int64(stmt.get(), 13, e.geracao()); texto(stmt.get(), 14, e.origem()); sqlite3_bind_int64(stmt.get(), 15, e.sequenciaOrigem()); sqlite3_bind_int64(stmt.get(), 16, e.instanteOcorrencia());
        done(db, stmt.get()); stmt.reset();
        if (e.idMissao() > 0 && (e.tipo() == TipoEvento::CARGA_COLETADA || e.tipo() == TipoEvento::CARGA_ENTREGUE)) {
            auto transport = prepare(db, R"SQL(INSERT INTO cargo_transports
                (run_id,mission_id,cargo_id,bay_id,destination,status,pickup_at_ms,delivered_at_ms)
                VALUES (?,?,?,?,?,?,?,?) ON CONFLICT(run_id,mission_id) DO UPDATE SET
                status=excluded.status, delivered_at_ms=COALESCE(excluded.delivered_at_ms,cargo_transports.delivered_at_ms),
                pickup_at_ms=COALESCE(cargo_transports.pickup_at_ms,excluded.pickup_at_ms))SQL");
            sqlite3_bind_int64(transport.get(), 1, run_id); sqlite3_bind_int64(transport.get(), 2, e.idMissao()); sqlite3_bind_int64(transport.get(), 3, e.idCarga()); sqlite3_bind_int(transport.get(), 4, e.idBaia()); texto(transport.get(), 5, e.destino());
            const bool delivered = e.tipo() == TipoEvento::CARGA_ENTREGUE; texto(transport.get(), 6, delivered ? "entregue" : "em transporte");
            sqlite3_bind_int64(transport.get(), delivered ? 8 : 7, e.instanteOcorrencia()); done(db, transport.get());
            for (const auto id : e.participantes()) {
                auto member = prepare(db, "INSERT OR IGNORE INTO cargo_members(run_id,mission_id,robot_id) VALUES (?,?,?)");
                sqlite3_bind_int64(member.get(), 1, run_id); sqlite3_bind_int64(member.get(), 2, e.idMissao()); sqlite3_bind_int(member.get(), 3, id); done(db, member.get());
            }
        }
        execute(db, "COMMIT");
    } catch (...) { sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr); throw; }
}
