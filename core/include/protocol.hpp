#ifndef PROTOCOL_HPP
#define PROTOCOL_HPP

#include "domain.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

constexpr std::uint32_t PROTOCOL_VERSION = 1;
constexpr std::size_t MAX_MESSAGE_BYTES = 64 * 1024;

enum class MessageType : std::uint32_t {
    RobotHello = 1, RobotReady, AssignMission, CancelMission, CommandAck,
    SensorUpdate, RobotSnapshot, NavigationSetpoint, PassageRequest,
    PassageGranted, PassageReleased, Couple, Uncouple, PauseMission,
    ResumeMission, Shutdown, RobotStopped, RobotDisconnected
};

struct Message {
    MessageType type = MessageType::RobotHello;
    int robot_id = -1;
    std::uint64_t generation = 0;
    std::uint64_t sequence = 0;
    std::uint64_t command_id = 0;
    std::uint64_t revision = 0;
    EntityId mission_id = 0;
    EntityId cargo_id = 0;
    EntityId team_id = 0;
    int bay_id = -1;
    MissionPhase phase = MissionPhase::NONE;
    RobotState state = RobotState::IDLE;
    Pos position;
    Pos target;
    int battery = 100;
    bool failure = false;
    bool coupled = false;
    bool accepted = true;
    std::uint64_t distance = 0;
    std::uint64_t step = 0;
    std::uint64_t sensor_sequence = 0;
    std::int64_t occurred_at_ms = 0;
    int passage_id = -1;
    int direction = 0;
    int team_size = 1;
    std::vector<Pos> route;
    std::vector<int> members;
    std::string text;
};

std::vector<std::uint8_t> encodeMessage(const Message& message);
Message decodeMessage(const std::vector<std::uint8_t>& bytes);
RobotSnapshot snapshotFromMessage(const Message& message);
Message messageFromSnapshot(const RobotSnapshot& snapshot);

#endif
