#include <gtest/gtest.h>
#include "data_collector.hpp"
#include <filesystem>
#include <unistd.h>

TEST(StorageMigrationTest, PreservesLegacyRowsAndPersistsWholeTeam) {
    std::filesystem::create_directories(TEST_DB_DIRECTORY);
    const auto path = std::string(TEST_DB_DIRECTORY) + "/migracao_" + std::to_string(getpid()) + ".db"; std::filesystem::remove(path);
    sqlite3* legacy = nullptr; ASSERT_EQ(sqlite3_open(path.c_str(),&legacy),SQLITE_OK);
    ASSERT_EQ(sqlite3_exec(legacy,"CREATE TABLE robot_events(id INTEGER PRIMARY KEY,timestamp TEXT NOT NULL,robot_id INTEGER NOT NULL,team_id INTEGER,bay_id INTEGER,position_x INTEGER NOT NULL,position_y INTEGER NOT NULL,event_type INTEGER NOT NULL,description TEXT NOT NULL,destination TEXT); INSERT INTO robot_events VALUES(1,'antigo',1,NULL,NULL,0,0,0,'preservado','');",nullptr,nullptr,nullptr),SQLITE_OK); sqlite3_close(legacy);
    {
        DataCollector collector(path); collector.start();
        RobotEvent pickup(-1,1,2,{2,2},EventType::CARGO_PICKUP,"Coleta","Zona 3"); pickup.trace(7,1,0,"logistica",1,epochMilliseconds(),{1,2,3});
        RobotEvent delivery(-1,1,2,{3,2},EventType::CARGO_DELIVERED,"Entrega","Zona 3"); delivery.trace(7,1,0,"logistica",2,epochMilliseconds(),{1,2,3});
        collector.enqueueEvent(pickup); collector.enqueueEvent(delivery); collector.enqueueEvent(delivery); collector.stop();
    }
    sqlite3* db = nullptr; ASSERT_EQ(sqlite3_open_v2(path.c_str(),&db,SQLITE_OPEN_READONLY,nullptr),SQLITE_OK);
    const auto count = [&](const char* sql) { sqlite3_stmt* s = nullptr; const int prepared = sqlite3_prepare_v2(db,sql,-1,&s,nullptr); if (prepared != SQLITE_OK) return -1; int value = -1; if (sqlite3_step(s) == SQLITE_ROW) value = sqlite3_column_int(s,0); sqlite3_finalize(s); return value; };
    EXPECT_EQ(count("SELECT COUNT(*) FROM robot_events"),3); EXPECT_EQ(count("SELECT COUNT(*) FROM robot_events WHERE description='preservado' AND run_id IS NULL"),1);
    EXPECT_EQ(count("SELECT COUNT(*) FROM cargo_members"),3); EXPECT_EQ(count("SELECT COUNT(*) FROM cargo_transports WHERE cargo_id=7 AND status='entregue'"),1);
    sqlite3_close(db); std::filesystem::remove(path);
}
TEST(StorageMigrationTest, SurfacesWorkerWriteFailureAndMarksSessionFailed) {
    std::filesystem::create_directories(TEST_DB_DIRECTORY);
    const auto path = std::string(TEST_DB_DIRECTORY) + "/falha_gravacao_" + std::to_string(getpid()) + ".db"; std::filesystem::remove(path);
    {
        DataCollector collector(path); collector.start(); sqlite3* db = nullptr;
        ASSERT_EQ(sqlite3_open(path.c_str(),&db),SQLITE_OK);
        ASSERT_EQ(sqlite3_exec(db,"CREATE TRIGGER bloquear BEFORE INSERT ON robot_events BEGIN SELECT RAISE(ABORT,'bloqueado'); END",nullptr,nullptr,nullptr),SQLITE_OK);
        sqlite3_close(db); collector.enqueueEvent(RobotEvent()); EXPECT_THROW(collector.stop(),std::runtime_error);
        EXPECT_THROW(collector.checkError(),std::runtime_error);
    }
    std::filesystem::remove(path);
}
