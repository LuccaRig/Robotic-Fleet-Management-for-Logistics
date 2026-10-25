#include <gtest/gtest.h>
#include "data_collector.hpp"

#include <sqlite3.h>
#include <iostream>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifndef TEST_DB_DIRECTORY
#error "TEST_DB_DIRECTORY precisa ser definido pelo CMake"
#endif

namespace {
class DataCollectorTest : public ::testing::Test {
protected:
    std::string path;
    void SetUp() override {
        std::filesystem::create_directories(TEST_DB_DIRECTORY);
        path = std::string(TEST_DB_DIRECTORY) + "/collector_" +
            ::testing::UnitTest::GetInstance()->current_test_info()->name() + ".db";
        std::filesystem::remove(path);
    }
    void TearDown() override { std::filesystem::remove(path); }
};

int countEvents(const std::string& path) {
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        throw std::runtime_error("Falha ao abrir banco de teste");
    }
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM robot_events", -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        throw std::runtime_error("Falha ao consultar banco de teste");
    }
    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        throw std::runtime_error("Falha ao contar eventos");
    }
    int result = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return result;
}

// Executa uma consulta SQL que deve devolver um inteiro na primeira coluna.
int queryInt(const std::string& path, const char* sql) {
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        throw std::runtime_error("Falha ao abrir banco de teste");
    }
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        throw std::runtime_error("Falha ao preparar consulta");
    }
    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        throw std::runtime_error("Consulta nao retornou dados");
    }
    int result = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return result;
}
} // namespace

TEST_F(DataCollectorTest, PersistsRobotEventFieldsAndTimestamp) {
    ColetorDados collector(path);
    collector.iniciar();
    EventoRobo e(7, 4, 3, {10, 25}, TipoEvento::ROBO_EM_MOVIMENTO,
                 "Deslocamento iniciado", "Baia 3");
    collector.enfileirarEvento(e);
    collector.parar();

    EXPECT_EQ(countEvents(path), 1);
    EXPECT_EQ(queryInt(path, R"SQL(
        SELECT COUNT(*) FROM robot_events WHERE robot_id=7 AND team_id=4
          AND bay_id=3 AND position_x=10 AND position_y=25
          AND event_type=1 AND description='Deslocamento iniciado'
          AND destination='Baia 3'
          AND timestamp GLOB '[0-3][0-9]-[0-1][0-9]-[0-9][0-9][0-9][0-9] *'
    )SQL"), 1);
}

TEST_F(DataCollectorTest, PersistsDeliveryDetails) {
    ColetorDados collector(path);
    collector.iniciar();
    collector.enfileirarEvento(EventoRobo(2, 8, 5, {6, 7}, TipoEvento::CARGA_ENTREGUE,
                                      "Carga entregue", "Estoque B"));
    collector.parar();
    EXPECT_EQ(queryInt(path, R"SQL(
        SELECT COUNT(*) FROM robot_events WHERE robot_id=2 AND team_id=8
          AND bay_id=5 AND position_x=6 AND position_y=7
          AND event_type=7 AND destination='Estoque B'
    )SQL"), 1);
}

TEST_F(DataCollectorTest, DrainsPendingEventsWhenStopped) {
    ColetorDados collector(path);
    collector.iniciar();
    for (int i = 0; i < 200; ++i)
        collector.enfileirarEvento(EventoRobo(i, -1, -1, {i, 0},
                                          TipoEvento::ROBO_CHEGOU, "Pendente", ""));
    collector.parar();
    EXPECT_EQ(countEvents(path), 200);
    EXPECT_EQ(queryInt(path, "SELECT COUNT(*) FROM robot_events WHERE robot_id=199"), 1);
}

TEST_F(DataCollectorTest, HandlesConcurrentProducersWithoutLosingEvents) {
    ColetorDados collector(path);
    collector.iniciar();
    std::vector<std::thread> threads;
    for (int p = 0; p < 4; ++p) {
        threads.emplace_back([&collector, p]() {
            for (int i = 0; i < 50; ++i)
                collector.enfileirarEvento(EventoRobo(p, p, -1, {i, p},
                                      TipoEvento::ROBO_EM_MOVIMENTO, "Concorrente", ""));
        });
    }
    for (auto& thread : threads) thread.join();
    collector.parar();
    const int total = countEvents(path);

    std::cout << "\nEventos enviados: 200\n";
    std::cout << "Eventos persistidos no SQLite: "
            << total << "/200\n";

    EXPECT_EQ(total, 200);
}

TEST_F(DataCollectorTest, RejectsEventsBeforeStartAndAfterStop) {
    ColetorDados collector(path);
    EventoRobo event;
    EXPECT_THROW(collector.enfileirarEvento(event), std::runtime_error);
    collector.iniciar();
    collector.parar();
    EXPECT_THROW(collector.enfileirarEvento(event), std::runtime_error);
    EXPECT_EQ(countEvents(path), 0);
}

TEST_F(DataCollectorTest, SupportsRepeatedStartStopAndRestart) {
    ColetorDados collector(path);
    collector.iniciar();
    collector.iniciar();
    collector.enfileirarEvento(EventoRobo());
    collector.parar();
    collector.parar();
    collector.iniciar();
    collector.enfileirarEvento(EventoRobo());
    collector.parar();
    EXPECT_EQ(countEvents(path), 2);
}

TEST_F(DataCollectorTest, PersistsAcrossDistinctInstances) {
    {
        ColetorDados first(path);
        first.iniciar();
        first.enfileirarEvento(EventoRobo());
        first.parar();
    }
    {
        ColetorDados second(path);
        second.iniciar();
        second.enfileirarEvento(EventoRobo());
        second.parar();
    }
    EXPECT_EQ(countEvents(path), 2);
}
