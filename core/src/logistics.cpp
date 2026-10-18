#include "logistics.hpp"
#include <algorithm>
#include <stdexcept>

LogisticsOrganizer::LogisticsOrganizer(const WarehouseGrid& warehouse, const FleetStateStore& fleet, std::vector<Cargo> cargo, Sender sender, Observer observer)
    : warehouse_(warehouse), fleet_(fleet), planner_(warehouse), sender_(std::move(sender)), observer_(std::move(observer)), bays_(warehouse.bays()) {
    for (auto& pair : bays_) pair.second.cargo_queue.clear();
    for (auto c : cargo) {
        if (c.id <= 0 || c.size < 1 || c.size > 4 || !bays_.count(c.bay_id) || !warehouse.destinations().count(c.destination_id) || cargo_.count(c.id)) throw std::invalid_argument("Cadastro de carga inválido");
        c.state = CargoState::PENDING; c.mission_id = 0; bays_.at(c.bay_id).cargo_queue.push_back(c.id); cargo_.emplace(c.id, std::move(c));
    }
}
LogisticsOrganizer::~LogisticsOrganizer() { stop(); }
void LogisticsOrganizer::start() {
    std::lock_guard<std::mutex> lifecycle(lifecycle_);
    std::lock_guard<std::mutex> lock(queue_mutex_);
    if (running_) return; running_ = true; error_ = nullptr;
    try { thread_ = std::thread(&LogisticsOrganizer::loop, this); } catch (...) { running_ = false; throw; }
}
void LogisticsOrganizer::stop() {
    std::lock_guard<std::mutex> lifecycle(lifecycle_);
    { std::lock_guard<std::mutex> lock(queue_mutex_); running_ = false; }
    cv_.notify_all(); if (thread_.joinable()) thread_.join();
}
void LogisticsOrganizer::onMessage(const Message& m) {
    { std::lock_guard<std::mutex> lock(queue_mutex_); if (events_.size() >= 4096) throw std::runtime_error("Fila da logística esgotada"); events_.push_back(m); }
    cv_.notify_all();
}
void LogisticsOrganizer::requestResume(EntityId id) { Message m; m.type = MessageType::ResumeMission; m.mission_id = id; onMessage(m); }
void LogisticsOrganizer::loop() noexcept {
    try {
        while (true) {
            {
                std::unique_lock<std::mutex> lock(queue_mutex_);
                cv_.wait_for(lock, std::chrono::milliseconds(200), [&] { return !running_ || !events_.empty(); });
                if (!running_) break;
            }
            processOnce();
        }
    } catch (...) { std::lock_guard<std::mutex> lock(queue_mutex_); error_ = std::current_exception(); running_ = false; }
}
void LogisticsOrganizer::processOnce() {
    std::deque<Message> events;
    { std::lock_guard<std::mutex> lock(queue_mutex_); events.swap(events_); }
    std::vector<std::pair<int, Message>> actions;
    std::vector<LogisticsNotice> notices;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        for (const auto& m : events) report(m);
        for (auto& pair : missions_) advance(pair.second);
        schedule(); actions.swap(actions_); notices.swap(notices_);
    }
    for (auto& action : actions) if (!sender_(action.first, action.second)) {
        Message disconnected; disconnected.type = MessageType::RobotDisconnected; disconnected.robot_id = action.first;
        const auto robot = fleet_.get(action.first); if (robot) disconnected.generation = robot->generation;
        disconnected.text = "Não foi possível enviar comando"; onMessage(disconnected);
    }
    for (const auto& item : notices) if (observer_) observer_(item);
}
void LogisticsOrganizer::schedule() {
    std::set<int> reserved;
    for (const auto& pair : missions_) if (pair.second.phase != MissionPhase::NONE) reserved.insert(pair.second.members.begin(), pair.second.members.end());
    const auto robots = fleet_.snapshot();
    std::set<int> needs_charge;
    for (auto& bay_pair : bays_) {
        auto& bay = bay_pair.second;
        if (bay.cargo_queue.empty()) continue;
        auto& cargo = cargo_.at(bay.cargo_queue.front());
        if (cargo.state != CargoState::PENDING) continue;
        const auto destination = warehouse_.destinations().at(cargo.destination_id).position;
        auto delivery = planner_.route(bay.position, destination, cargo.size);
        const auto charging = planner_.nearestCharger(destination);
        if (!delivery || !charging) { cargo.reason = "Sem rota compatível com a carga ou sem carregador alcançável"; continue; }
        std::vector<RobotSnapshot> chosen;
        for (const auto& robot : robots) {
            if (!robot.connected || !robot.available || robot.failure || robot.coupled || reserved.count(robot.id) || charging_.count(robot.id)) continue;
            const auto pickup = planner_.route(robot.position, bay.position);
            if (!pickup) continue;
            const auto cost = pickup->size() + delivery->size() + charging->second.size() + 15;
            if (static_cast<std::size_t>(robot.battery) <= cost) { needs_charge.insert(robot.id); continue; }
            chosen.push_back(robot); if (chosen.size() == static_cast<std::size_t>(cargo.size)) break;
        }
        if (chosen.size() != static_cast<std::size_t>(cargo.size)) { cargo.reason = "Aguardando equipe completa com rota e bateria suficientes"; continue; }
        TeamMission mission; mission.id = ++next_mission_; mission.cargo_id = cargo.id; mission.delivery_route = *delivery;
        for (const auto& robot : chosen) { mission.members.push_back(robot.id); mission.generations[robot.id] = robot.generation; reserved.insert(robot.id); }
        cargo.state = CargoState::GATHERING; cargo.mission_id = mission.id; cargo.reason.clear();
        auto& stored = missions_.emplace(mission.id, std::move(mission)).first->second;
        for (const auto& robot : chosen) {
            Message m; m.type = MessageType::AssignMission; m.phase = MissionPhase::TO_PICKUP; m.mission_id = stored.id;
            m.cargo_id = cargo.id; m.team_id = stored.id; m.bay_id = bay.id; m.target = bay.position;
            m.route = *planner_.route(robot.position, bay.position); m.members = {robot.id};
            actions_.emplace_back(robot.id, std::move(m));
        }
        notice(LogisticsNotice::Kind::Assigned, stored);
    }
    for (const auto& robot : robots) {
        if (charging_.count(robot.id) && (!robot.connected || (robot.state == RobotState::IDLE && robot.battery == 100))) charging_.erase(robot.id);
        if (!robot.connected || robot.failure || robot.coupled || robot.mission_id || reserved.count(robot.id) || charging_.count(robot.id) || robot.state != RobotState::IDLE || robot.battery == 100) continue;
        if (robot.battery > 15 && !needs_charge.count(robot.id)) continue;
        const auto charger = planner_.nearestCharger(robot.position);
        if (!charger || charger->second.size() > static_cast<std::size_t>(robot.battery)) continue;
        Message m; m.type = MessageType::AssignMission; m.phase = MissionPhase::TO_CHARGE;
        m.target = charger->first; m.route = charger->second; m.members = {robot.id};
        charging_.insert(robot.id); actions_.emplace_back(robot.id, std::move(m));
    }
}
void LogisticsOrganizer::report(const Message& m) {
    if (m.type == MessageType::ResumeMission && m.robot_id == -1) {
        const auto it = missions_.find(m.mission_id); if (it != missions_.end()) resume(it->second); return;
    }
    const bool reading = m.type == MessageType::SensorUpdate || m.type == MessageType::RobotSnapshot || m.type == MessageType::RobotReady;
    if (reading) {
        const auto previous = health_.find(m.robot_id);
        if (previous != health_.end()) {
            const auto& old = previous->second;
            if (m.generation < old.generation || (m.generation == old.generation && (m.sensor_sequence < old.sensor_sequence || m.revision < old.revision))) return;
        }
        health_[m.robot_id] = snapshotFromMessage(m);
    }
    if (m.type == MessageType::RobotDisconnected) {
        for (auto& pair : missions_) {
            auto& mission = pair.second;
            if (mission.generations.count(m.robot_id) && mission.generations.at(m.robot_id) == m.generation)
                interrupt(mission, "Integrante desconectado: " + std::to_string(m.robot_id));
        }
        charging_.erase(m.robot_id); return;
    }
    if (m.type == MessageType::CommandAck && !m.accepted && m.mission_id == 0) charging_.erase(m.robot_id);
    auto it = missions_.find(m.mission_id);
    if (it == missions_.end() || !it->second.generations.count(m.robot_id) || it->second.generations.at(m.robot_id) != m.generation) return;
    auto& mission = it->second;
    if (m.type == MessageType::CommandAck) {
        if (!m.accepted) interrupt(mission, "Comando rejeitado pelo robô " + std::to_string(m.robot_id));
        return;
    }
    if (m.type != MessageType::RobotSnapshot && m.type != MessageType::RobotReady && m.type != MessageType::SensorUpdate) return;
    if (m.coupled) { mission.ever_coupled = true; mission.coupled_members.insert(m.robot_id); }
    if (m.failure || m.battery <= 15) { interrupt(mission, m.failure ? "Falha em integrante da equipe" : "Bateria insuficiente em integrante da equipe"); return; }
    if (m.type != MessageType::SensorUpdate) mission.reports[m.robot_id] = snapshotFromMessage(m);
}
void LogisticsOrganizer::advance(TeamMission& mission) {
    if (mission.phase == MissionPhase::NONE || mission.phase == MissionPhase::SUSPENDED) return;
    auto all = [&](auto predicate) {
        for (const auto id : mission.members) {
            const auto it = mission.reports.find(id);
            if (it == mission.reports.end() || !predicate(it->second)) return false;
        }
        return true;
    };
    auto& cargo = cargo_.at(mission.cargo_id);
    if (mission.phase == MissionPhase::TO_PICKUP && all([](const RobotSnapshot& s) { return s.state == RobotState::WAITING_TO_LOAD && s.phase == MissionPhase::PICKUP_READY && !s.failure; })) {
        notice(LogisticsNotice::Kind::Gathered, mission); cargo.state = CargoState::COUPLING;
        command(mission, MessageType::Couple, MissionPhase::COUPLING);
    } else if (mission.phase == MissionPhase::COUPLING && all([](const RobotSnapshot& s) { return s.coupled && s.state == RobotState::WAITING_TO_LOAD && !s.failure; })) {
        mission.ever_coupled = true; cargo.state = CargoState::TRANSPORTING;
        notice(LogisticsNotice::Kind::Coupled, mission);
        command(mission, MessageType::AssignMission, MissionPhase::TO_DELIVERY);
        notice(LogisticsNotice::Kind::TransportStarted, mission);
    } else if (mission.phase == MissionPhase::TO_DELIVERY && all([](const RobotSnapshot& s) { return s.coupled && s.state == RobotState::WAITING_TO_UNLOAD && s.phase == MissionPhase::DELIVERY_READY && !s.failure; })) {
        cargo.state = CargoState::UNCOUPLING; command(mission, MessageType::Uncouple, MissionPhase::UNCOUPLING);
    } else if (mission.phase == MissionPhase::UNCOUPLING && all([](const RobotSnapshot& s) { return !s.coupled && s.state == RobotState::WAITING_TO_UNLOAD && s.phase == MissionPhase::UNCOUPLING && !s.failure; })) {
        cargo.state = CargoState::DELIVERED; cargo.reason.clear();
        auto& queue = bays_.at(cargo.bay_id).cargo_queue;
        if (queue.empty() || queue.front() != cargo.id) throw std::logic_error("Fila da baía inconsistente");
        queue.pop_front(); notice(LogisticsNotice::Kind::Delivered, mission);
        command(mission, MessageType::CancelMission, MissionPhase::NONE);
    }
}
void LogisticsOrganizer::interrupt(TeamMission& mission, const std::string& reason) {
    if (mission.phase == MissionPhase::NONE || mission.phase == MissionPhase::SUSPENDED) return;
    auto& cargo = cargo_.at(mission.cargo_id); cargo.reason = reason;
    if (!mission.ever_coupled && mission.phase == MissionPhase::TO_PICKUP) {
        cargo.state = CargoState::PENDING; cargo.mission_id = 0;
        command(mission, MessageType::CancelMission, MissionPhase::NONE); notice(LogisticsNotice::Kind::Cancelled, mission);
    } else {
        mission.suspended_phase = mission.phase; cargo.state = CargoState::SUSPENDED;
        command(mission, MessageType::PauseMission, MissionPhase::SUSPENDED); notice(LogisticsNotice::Kind::Suspended, mission);
    }
}
void LogisticsOrganizer::resume(TeamMission& mission) {
    if (mission.phase != MissionPhase::SUSPENDED) return;
    auto& cargo = cargo_.at(mission.cargo_id);
    std::optional<Pos> position;
    bool all_coupled = true;
    for (const auto id : mission.members) {
        const auto robot = fleet_.get(id);
        const auto health = health_.find(id);
        const bool confirmed = robot && health != health_.end() && health->second.generation == robot->generation &&
            robot->sensor_sequence >= health->second.sensor_sequence && robot->revision >= health->second.revision &&
            !health->second.failure && health->second.battery > 15;
        if (!robot || !confirmed || !robot->connected || robot->generation != mission.generations.at(id) || robot->failure || robot->battery <= 15 || robot->state != RobotState::SUSPENDED) {
            cargo.reason = "Recupere todos os integrantes e aguarde confirmação de suspensão"; return;
        }
        if (position && robot->position != *position) { cargo.reason = "Equipe fora da mesma posição lógica"; return; }
        position = robot->position; all_coupled = all_coupled && robot->coupled;
    }
    if (mission.suspended_phase == MissionPhase::COUPLING) {
        cargo.state = CargoState::COUPLING; command(mission, MessageType::ResumeMission, MissionPhase::COUPLING);
    } else if (mission.suspended_phase == MissionPhase::UNCOUPLING) {
        cargo.state = CargoState::UNCOUPLING; command(mission, MessageType::ResumeMission, MissionPhase::UNCOUPLING);
    } else {
        if (!all_coupled) { cargo.reason = "Transporte exige que todos os integrantes estejam acoplados"; return; }
        auto route = planner_.route(*position, warehouse_.destinations().at(cargo.destination_id).position, cargo.size);
        if (!route) { cargo.reason = "Sem rota para retomar o transporte"; return; }
        mission.delivery_route = *route; cargo.state = CargoState::TRANSPORTING;
        command(mission, MessageType::ResumeMission, MissionPhase::TO_DELIVERY);
    }
    cargo.reason.clear(); notice(LogisticsNotice::Kind::Resumed, mission);
}
void LogisticsOrganizer::command(TeamMission& mission, MessageType type, MissionPhase phase) {
    const auto& cargo = cargo_.at(mission.cargo_id);
    mission.phase = phase;
    for (const auto id : mission.members) {
        Message m; m.type = type; m.phase = phase; m.mission_id = mission.id; m.cargo_id = cargo.id;
        m.team_id = mission.id; m.bay_id = cargo.bay_id; m.members = mission.members; m.team_size = cargo.size;
        if (type == MessageType::AssignMission || type == MessageType::ResumeMission) {
            m.target = warehouse_.destinations().at(cargo.destination_id).position; m.route = mission.delivery_route;
        }
        actions_.emplace_back(id, std::move(m));
    }
    mission.reports.clear();
}
void LogisticsOrganizer::notice(LogisticsNotice::Kind kind, const TeamMission& mission) { notices_.push_back({kind, cargo_.at(mission.cargo_id), mission, epochMilliseconds()}); }
std::vector<Cargo> LogisticsOrganizer::cargos() const { std::lock_guard<std::mutex> lock(state_mutex_); std::vector<Cargo> result; for (const auto& c : cargo_) result.push_back(c.second); return result; }
std::vector<TeamMission> LogisticsOrganizer::missions() const { std::lock_guard<std::mutex> lock(state_mutex_); std::vector<TeamMission> result; for (const auto& m : missions_) result.push_back(m.second); return result; }
bool LogisticsOrganizer::allDelivered() const { for (const auto& c : cargos()) if (c.state != CargoState::DELIVERED) return false; return true; }
void LogisticsOrganizer::checkError() const { std::lock_guard<std::mutex> lock(queue_mutex_); if (error_) std::rethrow_exception(error_); }
