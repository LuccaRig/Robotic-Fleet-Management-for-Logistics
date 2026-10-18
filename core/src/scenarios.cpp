#include "scenarios.hpp"
#include <algorithm>
#include <stdexcept>
#include <unistd.h>

namespace {
WarehouseGrid layout() {
    WarehouseGrid map(21, 15);
    for (int x = 0; x < 21; ++x) { map.setCell(x, 0, CellState::OBSTACLE); map.setCell(x, 14, CellState::OBSTACLE); }
    for (int y = 1; y < 14; ++y) {
        map.setCell(0, y, CellState::OBSTACLE); map.setCell(20, y, CellState::OBSTACLE);
        for (const int x : {6, 12, 17}) map.setCell(x, y, CellState::OBSTACLE);
    }
    int id = 0;
    for (const auto item : std::vector<std::pair<int,int>>{{6,3},{12,4},{17,5}}) {
        map.setCell(item.first, item.second, CellState::EMPTY); map.addPassage({++id, id, {{item.first, item.second}}});
        map.setCell(item.first, 10, CellState::EMPTY); map.addPassage({10 + id, 4, {{item.first, 10}}});
    }
    map.addBay({1, {2,3}, {}}); map.addBay({2, {2,10}, {}});
    map.addDestination({1, {19,3}}); map.addDestination({2, {19,10}});
    map.setCell(2, 1, CellState::CHARGING_STATION); map.setCell(19, 12, CellState::CHARGING_STATION);
    return map;
}
Cargo cargo(EntityId id, int size, int bay = 1, int destination = 1) {
    Cargo c; c.id = id; c.size = size; c.bay_id = bay; c.destination_id = destination; return c;
}
bool allSuspended(const ControlRuntime& runtime, const TeamMission& mission, bool healthy) {
    for (const auto id : mission.members) {
        const auto s = runtime.fleet().get(id);
        if (!s || (healthy ? s->state != RobotState::SUSPENDED || s->failure : (s->state != RobotState::SUSPENDED && s->state != RobotState::ERROR))) return false;
    }
    return true;
}
}
std::string scenarioNames() { return "base, cooperativo, disponibilidade, passagens, rota-equipe, falhas, recarga, processos"; }
Scenario makeScenario(const std::string& name) {
    Scenario s{name, layout(), {cargo(1,1)}, {{1,{2,5}}}};
    if (name == "base") return s;
    if (name == "cooperativo") { s.cargos = {cargo(1,2), cargo(2,3), cargo(3,4)}; s.robots = {{1,{2,5}},{2,{3,5}},{3,{4,5}},{4,{5,5}}}; }
    else if (name == "disponibilidade") s.cargos = {cargo(1,2)};
    else if (name == "rota-equipe") { s.cargos = {cargo(1,4)}; s.robots = {{1,{2,5}},{2,{3,5}},{3,{4,5}},{4,{5,5}}}; }
    else if (name == "falhas") { s.cargos = {cargo(1,2)}; s.robots = {{1,{2,12}},{2,{3,12}}}; }
    else if (name == "recarga") s.robots = {{1,{2,2}}};
    else if (name == "processos") s.cargos.clear();
    else if (name == "passagens") {
        s.warehouse.addBay({3,{19,3},{}}); s.warehouse.addBay({4,{3,3},{}}); s.warehouse.addBay({5,{4,3},{}});
        s.warehouse.addBay({6,{18,3},{}}); s.warehouse.addBay({7,{19,4},{}}); s.warehouse.addDestination({3,{2,3}});
        s.cargos = {cargo(1,1,1,1), cargo(2,1,4,1), cargo(3,1,5,1), cargo(4,1,3,3), cargo(5,1,6,3), cargo(6,1,7,3)};
        s.robots = {{1,{2,3}},{2,{3,3}},{3,{4,3}},{4,{19,3}},{5,{18,3}},{6,{19,4}}};
    } else throw std::invalid_argument("Cenário desconhecido. Opções: " + scenarioNames());
    s.warehouse.validate(); return s;
}
void ScenarioScript::tick(ControlRuntime& runtime) {
    for (const auto& p : runtime.passages()) {
        if (p.occupied < 0 || p.occupied > p.capacity || p.maximum > p.capacity) throw std::runtime_error("Invariante de capacidade violada");
        if (p.maximum > 0) passages_seen_.insert(p.id);
        if (p.waiting > 0) queue_observed_ = true;
    }
    const auto first = runtime.fleet().get(1);
    if (name_ == "disponibilidade" && stage_ == 0 && first && first->connected) {
        const auto cargos = runtime.cargos();
        if (!cargos.empty() && cargos.front().state == CargoState::PENDING && !cargos.front().reason.empty()) {
            if (!runtime.addRobot(2, {3,5})) throw std::runtime_error("Falha ao completar a equipe");
            runtime.print("Evidência: carga aguardou equipe completa antes do despacho"); stage_ = 1;
        }
    } else if (name_ == "recarga") {
        if (stage_ == 0 && first && first->connected && !first->coupled) {
            if (!runtime.setBattery(1, 10)) throw std::runtime_error("Falha ao injetar bateria baixa"); stage_ = 1;
        } else if (stage_ == 1 && runtime.eventCount(EventType::ROBOT_CHARGING)) stage_ = 2;
        else if (stage_ == 2 && runtime.eventCount(EventType::RECHARGE_COMPLETED)) { runtime.print("Evidência: recarga completa confirmada antes do retorno"); stage_ = 3; }
    } else if (name_ == "processos") {
        const auto second = runtime.fleet().get(2);
        if (stage_ == 0 && first && first->connected) {
            if (runtime.addRobot(1,{2,5}) || !runtime.addRobot(2,{3,5})) throw std::runtime_error("Regra de ID duplicado incorreta"); stage_ = 1;
        } else if (stage_ == 1 && second && second->connected) {
            for (const auto& p : runtime.processes()) if (p.pid == getpid()) throw std::runtime_error("Robô sem processo independente");
            generation_ = second->generation;
            if (!runtime.interruptRobot(2)) throw std::runtime_error("Falha ao interromper processo"); stage_ = 2;
        } else if (stage_ == 2 && second && !second->connected) {
            bool exists = false; for (const auto& p : runtime.processes()) if (p.id == 2) exists = true;
            if (!exists && runtime.removeRobot(2)) stage_ = 3;
        } else if (stage_ == 3 && runtime.addRobot(2,{3,5})) stage_ = 4;
        else if (stage_ == 4 && second && second->connected && second->generation > generation_) { runtime.print("Evidência: processo recriado com nova geração após remoção explícita"); stage_ = 5; }
    } else if (name_ == "falhas") {
        const auto missions = runtime.missions();
        const auto active = std::find_if(missions.rbegin(), missions.rend(), [](const TeamMission& m) { return m.phase != MissionPhase::NONE; });
        if (stage_ == 0 && active != missions.rend() && active->phase == MissionPhase::TO_PICKUP && first && first->state == RobotState::NAVIGATING_TO_PICKUP) {
            if (!runtime.setFailure(1,true)) throw std::runtime_error("Falha ao injetar defeito"); stage_ = 1;
        } else if (stage_ == 1 && runtime.eventCount(EventType::MISSION_CANCELLED) && first && first->failure) {
            runtime.setFailure(1,false); stage_ = 2;
        } else if (stage_ == 2 && active != missions.rend() && active->phase == MissionPhase::TO_DELIVERY) {
            runtime.setFailure(2,true); stage_ = 3;
        } else if (stage_ == 3 && active != missions.rend() && active->phase == MissionPhase::SUSPENDED && allSuspended(runtime,*active,false)) {
            runtime.setFailure(2,false); for (const auto id : active->members) runtime.setBattery(id,100); stage_ = 4;
        } else if (stage_ == 4 && active != missions.rend() && active->phase == MissionPhase::SUSPENDED && allSuspended(runtime,*active,true)) {
            runtime.resume(active->id); stage_ = 5;
        } else if (stage_ == 5 && runtime.eventCount(EventType::MISSION_RESUMED)) stage_ = 6;
    }
}
bool ScenarioScript::complete(const ControlRuntime& runtime) const {
    if (!runtime.complete()) return false;
    if (name_ == "disponibilidade") return stage_ == 1;
    if (name_ == "recarga") return stage_ == 3;
    if (name_ == "processos") return stage_ == 5;
    if (name_ == "falhas") return stage_ == 6 && runtime.eventCount(EventType::MISSION_CANCELLED) && runtime.eventCount(EventType::MISSION_SUSPENDED);
    if (name_ == "passagens") return queue_observed_ && passages_seen_.count(1) && passages_seen_.count(2) && passages_seen_.count(3);
    if (name_ == "rota-equipe") return passages_seen_.count(11) && passages_seen_.count(12) && passages_seen_.count(13) && !passages_seen_.count(1) && !passages_seen_.count(2) && !passages_seen_.count(3);
    return runtime.eventCount(EventType::CARGO_DELIVERED) > 0;
}
