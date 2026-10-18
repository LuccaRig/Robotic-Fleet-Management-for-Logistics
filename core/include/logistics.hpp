#ifndef LOGISTICS_HPP
#define LOGISTICS_HPP

#include "fleet_state.hpp"
#include "protocol.hpp"
#include "route_planner.hpp"
#include <condition_variable>
#include <deque>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>

struct LogisticsNotice {
    enum class Kind { Assigned, Gathered, Coupled, TransportStarted, Delivered, Cancelled, Suspended, Resumed };
    Kind kind;
    Cargo cargo;
    TeamMission mission;
    std::int64_t occurred_at_ms = epochMilliseconds();
};

class LogisticsOrganizer {
public:
    using Sender = std::function<bool(int, Message)>;
    using Observer = std::function<void(const LogisticsNotice&)>;
    LogisticsOrganizer(const WarehouseGrid& warehouse, const FleetStateStore& fleet, std::vector<Cargo> cargo, Sender sender, Observer observer = {});
    ~LogisticsOrganizer();
    void start();
    void stop();
    void onMessage(const Message& message);
    void requestResume(EntityId mission_id);
    void processOnce();
    std::vector<Cargo> cargos() const;
    std::vector<TeamMission> missions() const;
    bool allDelivered() const;
    void checkError() const;
private:
    const WarehouseGrid& warehouse_;
    const FleetStateStore& fleet_;
    RoutePlanner planner_;
    Sender sender_;
    Observer observer_;
    mutable std::mutex state_mutex_;
    std::map<EntityId, Cargo> cargo_;
    std::map<int, LoadingBay> bays_;
    std::map<EntityId, TeamMission> missions_;
    EntityId next_mission_ = 0;
    std::set<int> charging_;
    std::vector<std::pair<int, Message>> actions_;
    std::vector<LogisticsNotice> notices_;
    mutable std::mutex queue_mutex_;
    std::condition_variable cv_;
    std::deque<Message> events_;
    bool running_ = false;
    std::thread thread_;
    std::mutex lifecycle_;
    std::exception_ptr error_;
    void loop() noexcept;
    void schedule();
    void report(const Message& message);
    void advance(TeamMission& mission);
    void interrupt(TeamMission& mission, const std::string& reason);
    void resume(TeamMission& mission);
    void notice(LogisticsNotice::Kind kind, const TeamMission& mission);
    void command(TeamMission& mission, MessageType type, MissionPhase phase);
};

#endif
