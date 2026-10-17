#include <gtest/gtest.h>
#include "data_collector.hpp"

#include <fstream>
#include <regex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifndef DATA_COLLECTOR_TEST_LOG
#error "Defina DATA_COLLECTOR_TEST_LOG no CMake para apontar para test_log.txt"
#endif

namespace {
const std::string kTestLogPath = DATA_COLLECTOR_TEST_LOG;

std::vector<std::string> readLines() {
    std::ifstream input(kTestLogPath);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(input, line)) {
        lines.push_back(line);
    }
    return lines;
}

class DataCollectorTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Cada teste inicia com o arquivo vazio, sem afetar logs/events.txt.
        std::ofstream output(kTestLogPath, std::ios::trunc);
        ASSERT_TRUE(output.is_open()) << "Nao foi possivel preparar " << kTestLogPath;
    }
};
} // namespace

TEST_F(DataCollectorTest, RecordsRobotEventWithTimestamp) {
    DataCollector collector(kTestLogPath);
    collector.start();
    collector.recordRobotEvent(7, EventType::ROBOT_MOVING, "Deslocamento iniciado");
    collector.stop();

    const auto lines = readLines();
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_TRUE(std::regex_search(lines[0], std::regex(R"(^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2} - )")));
    EXPECT_NE(lines[0].find("Robot ID: 7"), std::string::npos);
    EXPECT_NE(lines[0].find("Type: 1"), std::string::npos);
    EXPECT_NE(lines[0].find("Description: Deslocamento iniciado"), std::string::npos);
}

TEST_F(DataCollectorTest, RecordsCargoDeliveryDetails) {
    DataCollector collector(kTestLogPath);
    collector.start();
    collector.recordCargoDelivery(3, "Estoque B", {2, 5, 8});
    collector.stop();

    const auto lines = readLines();
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_NE(lines[0].find("Robot ID: -1"), std::string::npos);
    EXPECT_NE(lines[0].find("Type: 7"), std::string::npos);
    EXPECT_NE(lines[0].find("Bay ID: 3"), std::string::npos);
    EXPECT_NE(lines[0].find("Destination: Estoque B"), std::string::npos);
    EXPECT_NE(lines[0].find("Team Robots: 2 5 8"), std::string::npos);
}

TEST_F(DataCollectorTest, DrainsPendingEventsWhenStopped) {
    constexpr int kEvents = 200;
    DataCollector collector(kTestLogPath);
    collector.start();
    for (int i = 0; i < kEvents; ++i) {
        collector.recordRobotEvent(i, EventType::ROBOT_ARRIVED, "Evento pendente");
    }
    collector.stop();

    const auto lines = readLines();
    ASSERT_EQ(lines.size(), static_cast<std::size_t>(kEvents));
    EXPECT_NE(lines.front().find("Robot ID: 0"), std::string::npos);
    EXPECT_NE(lines.back().find("Robot ID: 199"), std::string::npos);
}

TEST_F(DataCollectorTest, HandlesConcurrentProducersWithoutLosingEvents) {
    constexpr int kProducers = 4;
    constexpr int kEventsPerProducer = 50;
    DataCollector collector(kTestLogPath);
    collector.start();

    std::vector<std::thread> producers;
    for (int p = 0; p < kProducers; ++p) {
        producers.emplace_back([&collector, p] {
            for (int i = 0; i < kEventsPerProducer; ++i) {
                collector.recordRobotEvent(p, EventType::ROBOT_MOVING, "Evento concorrente");
            }
        });
    }
    for (auto& producer : producers) {
        producer.join();
    }
    collector.stop();

    const auto lines = readLines();
    ASSERT_EQ(lines.size(), static_cast<std::size_t>(kProducers * kEventsPerProducer));
    for (const auto& line : lines) {
        EXPECT_NE(line.find("Description: Evento concorrente"), std::string::npos);
    }
}

TEST_F(DataCollectorTest, RejectsEventsBeforeStartAndAfterStop) {
    DataCollector collector(kTestLogPath);
    EXPECT_THROW(collector.recordRobotEvent(1, EventType::ROBOT_CREATED, "Antes"), std::runtime_error);
    collector.start();
    collector.stop();
    EXPECT_THROW(collector.recordRobotEvent(2, EventType::ROBOT_CREATED, "Depois"), std::runtime_error);
    EXPECT_TRUE(readLines().empty());
}

TEST_F(DataCollectorTest, SupportsRepeatedStartStopAndRestart) {
    DataCollector collector(kTestLogPath);
    collector.start();
    collector.start(); // chamada duplicada nao pode gerar segunda thread
    collector.recordRobotEvent(1, EventType::ROBOT_CREATED, "Primeiro ciclo");
    collector.stop();
    collector.stop(); // encerramento repetido nao deve falhar
    collector.start();
    collector.recordRobotEvent(2, EventType::ROBOT_CREATED, "Segundo ciclo");
    collector.stop();

    const auto lines = readLines();
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_NE(lines[0].find("Primeiro ciclo"), std::string::npos);
    EXPECT_NE(lines[1].find("Segundo ciclo"), std::string::npos);
}

TEST_F(DataCollectorTest, AppendsAcrossDistinctInstances) {
    {
        DataCollector first(kTestLogPath);
        first.start();
        first.recordRobotEvent(10, EventType::ROBOT_CREATED, "Primeira instancia");
        first.stop();
    }
    {
        DataCollector second(kTestLogPath);
        second.start();
        second.recordRobotEvent(11, EventType::ROBOT_CREATED, "Segunda instancia");
        second.stop();
    }
    const auto lines = readLines();
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_NE(lines[0].find("Primeira instancia"), std::string::npos);
    EXPECT_NE(lines[1].find("Segunda instancia"), std::string::npos);
}
