#ifndef DATA_COLLECTOR_HPP
#define DATA_COLLECTOR_HPP

#include "robot_event.hpp"
#include <condition_variable>
#include <exception>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <sqlite3.h>

class DataCollector {
public:
    explicit DataCollector(const std::string& database_path);
    ~DataCollector();
    DataCollector(const DataCollector&) = delete;
    DataCollector& operator=(const DataCollector&) = delete;
    void start();
    void stop();
    void enqueueEvent(const RobotEvent& event);
    void checkError() const;
    EntityId runId() const;
private:
    enum class CollectorState { STOPPED, RUNNING, STOPPING, FAILED };
    mutable std::mutex queue_mutex;
    std::condition_variable queue_cv;
    std::queue<RobotEvent> event_queue;
    CollectorState state = CollectorState::STOPPED;
    std::exception_ptr worker_error;
    std::mutex lifecycle_mutex;
    std::thread collector_thread;
    std::string db_path;
    sqlite3* db = nullptr;
    EntityId run_id = 0;
    void openDatabase();
    void createTable();
    void closeDatabase() noexcept;
    void collectorLoop() noexcept;
    void writeEventToDatabase(const RobotEvent& event);
};

#endif
