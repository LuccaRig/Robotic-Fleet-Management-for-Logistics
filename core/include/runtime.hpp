#ifndef RUNTIME_HPP
#define RUNTIME_HPP

#include "event_recorder.hpp"
#include "hardware.hpp"
#include "orchestrator.hpp"
#include <atomic>
#include <map>
#include <set>

struct RobotStart { int id; Pos position; };
struct Scenario {
    std::string name;
    WarehouseGrid warehouse;
    std::vector<Cargo> cargos;
    std::vector<RobotStart> robots;
};

class ControlRuntime {
public:
    ControlRuntime(std::string executable, std::string database, Scenario scenario);
    ~ControlRuntime();
    void start();
    void stop();
    void tick();
    bool addRobot(int id, Pos position);
    bool removeRobot(int id);
    bool interruptRobot(int id);
    bool setBattery(int id, int battery);
    bool setFailure(int id, bool failure);
    void resume(EntityId mission_id);
    bool complete() const;
    std::size_t eventCount(EventType type) const;
    void command(const std::string& line);
    void print(const std::string& text) const;
    const FleetStateStore& fleet() const { return manager_.fleet(); }
    std::vector<Cargo> cargos() const { return logistics_.cargos(); }
    std::vector<TeamMission> missions() const { return logistics_.missions(); }
    std::vector<RobotProcess> processes() const { return manager_.processes(); }
    std::vector<PassageSnapshot> passages() const { return passages_.snapshot(); }
    bool exitRequested() const { return exit_requested_; }
private:
    WarehouseGrid warehouse_;
    std::vector<RobotStart> initial_;
    DataCollector collector_;
    Buffer buffer_;
    mutable std::mutex console_;
    mutable std::mutex metrics_mutex_;
    std::map<EventType, std::size_t> metrics_;
    EventRecorder recorder_;
    Orchestrator manager_;
    PassageCoordinator passages_;
    LocalHardwareAdapter hardware_;
    LogisticsOrganizer logistics_;
    std::atomic<std::uint64_t> commands_{0};
    std::mutex pending_mutex_;
    std::map<std::uint64_t, Message> pending_;
    std::set<int> removals_;
    bool started_ = false;
    bool stopped_ = false;
    bool exit_requested_ = false;
    bool sendCommand(int id, Message message);
    void onMessage(const Message& message);
    void onSensor(Message sensor);
};

#endif
