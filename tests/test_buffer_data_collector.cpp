#include <gtest/gtest.h>
#include "buffer.hpp"

#include <sqlite3.h>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifndef TEST_DB_DIRECTORY
#error "TEST_DB_DIRECTORY precisa ser definido pelo CMake"
#endif

namespace {
class BufferCollectorTest : public ::testing::Test {
protected:
    std::string path;
    void SetUp() override {
        std::filesystem::create_directories(TEST_DB_DIRECTORY);
        path = std::string(TEST_DB_DIRECTORY) + "/buffer_" +
            ::testing::UnitTest::GetInstance()->current_test_info()->name() + ".db";
        std::filesystem::remove(path);
    }
    void TearDown() override { std::filesystem::remove(path); }

    int count(const char* filter = "1=1") {
        sqlite3* db = nullptr;
        if (sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
            if (db) sqlite3_close(db);
            throw std::runtime_error("Nao foi possivel abrir banco de teste");
        }
        const std::string sql = std::string("SELECT COUNT(*) FROM robot_events WHERE ") + filter;
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
            sqlite3_close(db);
            throw std::runtime_error("Falha na consulta SQL");
        }
        if (sqlite3_step(stmt) != SQLITE_ROW) {
            sqlite3_finalize(stmt);
            sqlite3_close(db);
            throw std::runtime_error("Falha ao ler consulta SQL");
        }
        int result = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        return result;
    }
};
} // namespace

TEST_F(BufferCollectorTest, CircularRetentionStillPersistsAllEvents) {
    DataCollector collector(path);
    collector.start();
    Buffer buffer(collector, 3);
    for (int i = 0; i < 5; ++i) {
        buffer.addEvent(RobotEvent(i, 9, 2, {i, i + 1}, EventType::ROBOT_MOVING,
                                   "Movimento", "Destino"));
    }
    ASSERT_EQ(buffer.size(), 3u);
    const auto snapshot = buffer.snapshot();
    ASSERT_EQ(snapshot.size(), 3u);
    EXPECT_EQ(snapshot[0].getRobotId(), 2);
    EXPECT_EQ(snapshot[1].getRobotId(), 3);
    EXPECT_EQ(snapshot[2].getRobotId(), 4);
    collector.stop();
    EXPECT_EQ(count(), 5);
    EXPECT_EQ(count("robot_id=0 AND position_x=0 AND position_y=1"), 1);
    EXPECT_EQ(count("robot_id=4 AND position_x=4 AND position_y=5"), 1);
}

TEST_F(BufferCollectorTest, ClearDoesNotErasePersistedEvents) {
    DataCollector collector(path);
    collector.start();
    Buffer buffer(collector, 5);
    buffer.addEvent(RobotEvent(42, -1, -1, {4, 2}, EventType::ROBOT_CREATED,
                               "Inicializado", ""));
    buffer.clear();
    EXPECT_EQ(buffer.size(), 0u);
    EXPECT_TRUE(buffer.snapshot().empty());
    collector.stop();
    EXPECT_EQ(count("robot_id=42"), 1);
}

TEST_F(BufferCollectorTest, ConcurrentBufferWritersReachSQLite) {
    DataCollector collector(path);
    collector.start();
    Buffer buffer(collector, 10);
    std::vector<std::thread> threads;
    for (int p = 0; p < 4; ++p) {
        threads.emplace_back([&buffer, p]() {
            for (int i = 0; i < 50; ++i)
                buffer.addEvent(RobotEvent(p, p, -1, {i, p}, EventType::ROBOT_MOVING,
                                           "Concorrente", ""));
        });
    }
    for (auto& thread : threads) thread.join();
    EXPECT_EQ(buffer.size(), 10u);
    collector.stop();
    EXPECT_EQ(count(), 200);
    for (int p = 0; p < 4; ++p)
        EXPECT_EQ(count(("robot_id=" + std::to_string(p)).c_str()), 50);
}

TEST_F(BufferCollectorTest, RejectedEventDoesNotStayInBuffer) {
    DataCollector collector(path);
    Buffer buffer(collector, 3);
    EXPECT_THROW(buffer.addEvent(RobotEvent()), std::runtime_error);
    EXPECT_EQ(buffer.size(), 0u);
    collector.start();
    collector.stop();
    EXPECT_THROW(buffer.addEvent(RobotEvent()), std::runtime_error);
    EXPECT_EQ(buffer.size(), 0u);
    EXPECT_EQ(count(), 0);
}
