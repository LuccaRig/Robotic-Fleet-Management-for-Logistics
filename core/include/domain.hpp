#ifndef DOMAIN_HPP
#define DOMAIN_HPP

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

using EntityId = std::int64_t;

struct Pos {
    int x = 0;
    int y = 0;
};

inline bool operator==(Pos a, Pos b) { return a.x == b.x && a.y == b.y; }
inline bool operator!=(Pos a, Pos b) { return !(a == b); }
inline int gridDistance(Pos a, Pos b) {
    return (a.x > b.x ? a.x - b.x : b.x - a.x) + (a.y > b.y ? a.y - b.y : b.y - a.y);
}
inline std::int64_t epochMilliseconds() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

enum class RobotState {
    IDLE, NAVIGATING_TO_PICKUP, WAITING_TO_LOAD, COUPLING,
    NAVIGATING_TO_DELIVERY, WAITING_TO_UNLOAD, UNCOUPLING,
    NAVIGATING_TO_CHARGE, CHARGING, SUSPENDED, ERROR, STOPPED
};

enum class MissionPhase {
    NONE, TO_PICKUP, PICKUP_READY, COUPLING, TO_DELIVERY,
    DELIVERY_READY, UNCOUPLING, TO_CHARGE, CHARGING, SUSPENDED
};

inline std::string robotStateName(RobotState state) {
    switch (state) {
    case RobotState::IDLE: return "Disponível";
    case RobotState::NAVIGATING_TO_PICKUP: return "Indo à coleta";
    case RobotState::WAITING_TO_LOAD: return "Aguardando equipe";
    case RobotState::COUPLING: return "Acoplando";
    case RobotState::NAVIGATING_TO_DELIVERY: return "Transportando";
    case RobotState::WAITING_TO_UNLOAD: return "Aguardando descarga";
    case RobotState::UNCOUPLING: return "Desacoplando";
    case RobotState::NAVIGATING_TO_CHARGE: return "Indo à recarga";
    case RobotState::CHARGING: return "Recarregando";
    case RobotState::SUSPENDED: return "Missão suspensa";
    case RobotState::ERROR: return "Falha";
    case RobotState::STOPPED: return "Encerrado";
    }
    return "Desconhecido";
}

struct RobotSnapshot {
    int id = -1;
    std::uint64_t generation = 0;
    std::uint64_t sequence = 0;
    Pos position;
    Pos target;
    RobotState state = RobotState::IDLE;
    MissionPhase phase = MissionPhase::NONE;
    int battery = 100;
    bool failure = false;
    bool coupled = false;
    bool connected = false;
    bool available = false;
    EntityId mission_id = 0;
    EntityId cargo_id = 0;
    EntityId team_id = 0;
    int bay_id = -1;
    std::uint64_t step = 0;
    std::uint64_t sensor_sequence = 0;
    std::uint64_t distance = 0;
    std::int64_t updated_at_ms = 0;
};

#endif
