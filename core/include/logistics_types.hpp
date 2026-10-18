#ifndef LOGISTICS_TYPES_HPP
#define LOGISTICS_TYPES_HPP

#include "domain.hpp"
#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>

enum class CargoState { PENDING, RESERVED, GATHERING, COUPLING, TRANSPORTING, UNCOUPLING, DELIVERED, SUSPENDED };
inline std::string cargoStateName(CargoState state) {
    switch (state) {
    case CargoState::PENDING: return "Pendente";
    case CargoState::RESERVED: return "Reservada";
    case CargoState::GATHERING: return "Reunindo equipe";
    case CargoState::COUPLING: return "Acoplando";
    case CargoState::TRANSPORTING: return "Em transporte";
    case CargoState::UNCOUPLING: return "Desacoplando";
    case CargoState::DELIVERED: return "Entregue";
    case CargoState::SUSPENDED: return "Suspensa";
    }
    return "Desconhecido";
}
struct Cargo {
    EntityId id = 0;
    int size = 1;
    int bay_id = -1;
    int destination_id = -1;
    CargoState state = CargoState::PENDING;
    EntityId mission_id = 0;
    std::string reason;
};
struct LoadingBay { int id; Pos position; std::deque<EntityId> cargo_queue; };
struct Destination { int id; Pos position; };
struct Passage { int id; int capacity; std::vector<Pos> cells; };
struct TeamMission {
    EntityId id = 0;
    EntityId cargo_id = 0;
    MissionPhase phase = MissionPhase::TO_PICKUP;
    MissionPhase suspended_phase = MissionPhase::NONE;
    std::vector<int> members;
    std::map<int, std::uint64_t> generations;
    std::map<int, RobotSnapshot> reports;
    std::vector<Pos> delivery_route;
    std::set<int> coupled_members;
    bool ever_coupled = false;
};

#endif
