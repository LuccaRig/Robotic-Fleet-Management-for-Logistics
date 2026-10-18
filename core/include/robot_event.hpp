#ifndef ROBOT_EVENT_HPP
#define ROBOT_EVENT_HPP

#include "domain.hpp"
#include <atomic>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

enum class EventType {
    ROBOT_CREATED = 0, ROBOT_MOVING = 1, ROBOT_ARRIVED = 2, LOW_BATTERY = 3,
    ROBOT_CHARGING = 4, ROBOT_FAILURE = 5, CARGO_PICKUP = 6, CARGO_DELIVERED = 7,
    ROBOT_READY, ROBOT_DISCONNECTED, ROBOT_STOPPED, MISSION_ASSIGNED,
    MISSION_CANCELLED, TEAM_GATHERED, TRANSPORT_STARTED, MISSION_SUSPENDED,
    MISSION_RESUMED, PASSAGE_ENTERED, PASSAGE_EXITED, ROBOT_RECOVERED, RECHARGE_COMPLETED
};

class RobotEvent {
public:
    RobotEvent() : RobotEvent(-1, -1, -1, {0, 0}, EventType::ROBOT_CREATED, "", "") {}
    RobotEvent(int robot_id, int team_id, int bay_id, Pos position, EventType type, const std::string& description, const std::string& destination)
        : robot_id_(robot_id), team_id_(team_id), bay_id_(bay_id), position_(position), type_(type), description_(description), destination_(destination), occurred_at_ms_(epochMilliseconds()), source_sequence_(++counter_) {
        const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::tm local{}; localtime_r(&now, &local); std::ostringstream stream; stream << std::put_time(&local, "%d-%m-%Y %H:%M:%S"); timestamp_ = stream.str();
    }
    int getRobotId() const { return robot_id_; }
    int getTeamId() const { return team_id_; }
    int getBayId() const { return bay_id_; }
    Pos getPosition() const { return position_; }
    EventType getType() const { return type_; }
    const std::string& getDescription() const { return description_; }
    const std::string& getTimestamp() const { return timestamp_; }
    const std::string& getDestination() const { return destination_; }
    EntityId cargoId() const { return cargo_id_; }
    EntityId missionId() const { return mission_id_; }
    std::int64_t occurredAt() const { return occurred_at_ms_; }
    std::uint64_t generation() const { return generation_; }
    std::uint64_t sourceSequence() const { return source_sequence_; }
    const std::string& source() const { return source_; }
    const std::vector<int>& participants() const { return participants_; }
    RobotEvent& trace(EntityId cargo, EntityId mission, std::uint64_t generation, std::string source, std::uint64_t sequence, std::int64_t occurred, std::vector<int> participants = {}) {
        cargo_id_ = cargo; mission_id_ = mission; generation_ = generation; source_ = std::move(source);
        source_sequence_ = sequence; occurred_at_ms_ = occurred; participants_ = std::move(participants); return *this;
    }
private:
    int robot_id_, team_id_, bay_id_;
    Pos position_;
    EventType type_;
    std::string description_, destination_, timestamp_;
    EntityId cargo_id_ = 0, mission_id_ = 0;
    std::int64_t occurred_at_ms_;
    std::uint64_t generation_ = 0, source_sequence_;
    std::string source_ = "local";
    std::vector<int> participants_;
    inline static std::atomic<std::uint64_t> counter_{0};
};

#endif
