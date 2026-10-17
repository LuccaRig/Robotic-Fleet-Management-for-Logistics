
#ifndef DATA_COLLECTOR_HPP
#define DATA_COLLECTOR_HPP

#include <string>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>

enum class EventType {
    ROBOT_CREATED,
    ROBOT_MOVING,
    ROBOT_ARRIVED,
    LOW_BATTERY,
    ROBOT_CHARGING,
    ROBOT_FAILURE,
    CARGO_PICKUP,
    CARGO_DELIVERED
};

struct RobotEvent {
    int robot_id = -1;
    EventType type = EventType::ROBOT_CREATED;
    std::string description;
    std::string timestamp;
    int bay_id = -1;
    std::string destination;
    std::vector<int> team_robot_ids;
};

class DataCollector {
private:
    enum class CollectorState {
        STOPPED,
        RUNNING,
        STOPPING
    };

    CollectorState state = CollectorState::STOPPED;

    std::queue<RobotEvent> event_queue;

    std::mutex queue_mutex;
    std::mutex lifecycle_mutex;
    std::condition_variable queue_cv;

    std::thread collector_thread;
    std::string log_file_path;

    void collectorLoop();
    void writeEventToDisk(const RobotEvent& event);
    std::string getTimestamp() const;

public:
    explicit DataCollector(const std::string& file_path);
    ~DataCollector();

    void start();
    void stop();

    void enqueueEvent(const RobotEvent& event);

    void recordRobotEvent(
        int robot_id,
        EventType type,
        const std::string& description
    );

    void recordCargoDelivery(
        int bay_id,
        const std::string& destination,
        const std::vector<int>& robot_ids
    );
};

#endif
