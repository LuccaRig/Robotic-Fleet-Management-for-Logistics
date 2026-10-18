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
            if (!robot.connected || !robot.available || robot.failure || robot.coupled || reserved.count(robot.id)) continue;
            const auto pickup = planner_.route(robot.position, bay.position);
            if (!pickup) continue;
            const auto cost = pickup->size() + delivery->size() + charging->second.size() + 15;
            if (static_cast<std::size_t>(robot.battery) <= cost) continue;
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
}
void LogisticsOrganizer::report(const Message& m) {
    if (m.type != MessageType::RobotSnapshot && m.type != MessageType::RobotReady) return;
    auto it = missions_.find(m.mission_id);
    if (it == missions_.end() || !it->second.generations.count(m.robot_id) || it->second.generations.at(m.robot_id) != m.generation) return;
    it->second.reports[m.robot_id] = snapshotFromMessage(m);
}
void LogisticsOrganizer::advance(TeamMission&) {}
void LogisticsOrganizer::interrupt(TeamMission&, const std::string&) {}
void LogisticsOrganizer::resume(TeamMission&) {}
void LogisticsOrganizer::command(TeamMission&, MessageType, MissionPhase) {}
void LogisticsOrganizer::notice(LogisticsNotice::Kind kind, const TeamMission& mission) { notices_.push_back({kind, cargo_.at(mission.cargo_id), mission, epochMilliseconds()}); }
std::vector<Cargo> LogisticsOrganizer::cargos() const { std::lock_guard<std::mutex> lock(state_mutex_); std::vector<Cargo> result; for (const auto& c : cargo_) result.push_back(c.second); return result; }
std::vector<TeamMission> LogisticsOrganizer::missions() const { std::lock_guard<std::mutex> lock(state_mutex_); std::vector<TeamMission> result; for (const auto& m : missions_) result.push_back(m.second); return result; }
bool LogisticsOrganizer::allDelivered() const { for (const auto& c : cargos()) if (c.state != CargoState::DELIVERED) return false; return true; }
void LogisticsOrganizer::checkError() const { std::lock_guard<std::mutex> lock(queue_mutex_); if (error_) std::rethrow_exception(error_); }
