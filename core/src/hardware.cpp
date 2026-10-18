#include "hardware.hpp"
#include <algorithm>
#include <stdexcept>

LocalHardwareAdapter::LocalHardwareAdapter(const WarehouseGrid& warehouse, SensorSink sink) : warehouse_(warehouse), sink_(std::move(sink)) {}
Message LocalHardwareAdapter::sensor(Unit& unit) { unit.sensor.type = MessageType::SensorUpdate; ++unit.sensor.sensor_sequence; unit.sensor.occurred_at_ms = epochMilliseconds(); return unit.sensor; }
void LocalHardwareAdapter::deliver(std::vector<Message> messages) { for (auto& message : messages) sink_(std::move(message)); }
void LocalHardwareAdapter::registerRobot(const RobotSnapshot& snapshot) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (units_.count(snapshot.id)) throw std::runtime_error("O robô ainda ocupa o ambiente; remova-o explicitamente antes de recriar");
    Unit unit; unit.sensor = messageFromSnapshot(snapshot); unit.members = {snapshot.id}; units_.emplace(snapshot.id, std::move(unit));
}
void LocalHardwareAdapter::disconnect(int id) { std::lock_guard<std::mutex> lock(mutex_); const auto it = units_.find(id); if (it != units_.end()) { it->second.paused = true; proposals_.erase(it->second.sensor.mission_id); } }
bool LocalHardwareAdapter::contains(int id) const { std::lock_guard<std::mutex> lock(mutex_); return units_.count(id) != 0; }
bool LocalHardwareAdapter::removeRobot(int id) { std::lock_guard<std::mutex> lock(mutex_); auto it = units_.find(id); if (it == units_.end() || it->second.sensor.coupled) return false; proposals_.erase(it->second.sensor.mission_id); units_.erase(it); return true; }
void LocalHardwareAdapter::setMovementValidator(std::function<bool(const Message&)> validator) { std::lock_guard<std::mutex> lock(mutex_); validator_ = std::move(validator); }
void LocalHardwareAdapter::commandAccepted(const Message& m) {
    std::vector<Message> feedback;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = units_.find(m.robot_id); if (it == units_.end()) return;
        auto& unit = it->second;
        if (m.generation != unit.sensor.generation) return;
        unit.sensor.revision = m.command_id;
        if (m.type == MessageType::CancelMission) {
            proposals_.erase(unit.sensor.mission_id); unit.sensor.mission_id = 0; unit.sensor.cargo_id = 0; unit.sensor.team_id = 0;
            unit.sensor.phase = MissionPhase::NONE; unit.sensor.step = 0; unit.members = {m.robot_id}; unit.paused = false;
        } else if (m.type == MessageType::PauseMission) {
            proposals_.erase(unit.sensor.mission_id); unit.paused = true; unit.sensor.phase = MissionPhase::SUSPENDED;
        } else if (m.type == MessageType::AssignMission || m.type == MessageType::ResumeMission) {
            unit.sensor.mission_id = m.mission_id; unit.sensor.cargo_id = m.cargo_id; unit.sensor.team_id = m.team_id;
            unit.sensor.bay_id = m.bay_id; unit.sensor.phase = m.phase; unit.sensor.step = 0; unit.members = m.members; unit.paused = false;
            if (m.phase == MissionPhase::COUPLING) unit.sensor.coupled = true;
            if (m.phase == MissionPhase::UNCOUPLING) unit.sensor.coupled = false;
        } else if (m.type == MessageType::Couple) { unit.sensor.phase = MissionPhase::COUPLING; unit.sensor.coupled = true; }
        else if (m.type == MessageType::Uncouple) { unit.sensor.phase = MissionPhase::UNCOUPLING; unit.sensor.coupled = false; }
        else return;
        feedback.push_back(sensor(unit));
    }
    deliver(std::move(feedback));
}
void LocalHardwareAdapter::setpoint(const Message& m) {
    std::vector<Message> feedback;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = units_.find(m.robot_id); if (it == units_.end()) throw std::runtime_error("Setpoint de robô inexistente");
        auto& unit = it->second;
        if (m.generation != unit.sensor.generation || m.mission_id != unit.sensor.mission_id || m.phase != unit.sensor.phase || m.revision != unit.sensor.revision || unit.paused) return;
        if (unit.sensor.failure || unit.sensor.battery == 0) return;
        if (m.step != unit.sensor.step + 1 || m.position != unit.sensor.position || gridDistance(unit.sensor.position, m.target) != 1 || !warehouse_.traversable(m.target)) throw std::runtime_error("Setpoint físico inválido");
        if (validator_ && !validator_(m)) throw std::runtime_error("Movimento sem permissão de passagem");
        if (!unit.sensor.coupled) {
            unit.sensor.position = m.target; unit.sensor.step = m.step; --unit.sensor.battery; feedback.push_back(sensor(unit));
        } else {
            auto& proposals = proposals_[m.mission_id]; proposals[m.robot_id] = m;
            if (unit.members.empty()) throw std::runtime_error("Equipe acoplada sem integrantes");
            bool complete = true;
            for (const auto id : unit.members) {
                const auto robot = units_.find(id); const auto proposal = proposals.find(id);
                if (robot == units_.end() || proposal == proposals.end() || robot->second.paused || robot->second.sensor.failure || robot->second.sensor.battery <= 15 || !robot->second.sensor.coupled || robot->second.sensor.mission_id != m.mission_id || proposal->second.target != m.target || proposal->second.step != m.step) { complete = false; break; }
            }
            if (complete) {
                for (const auto id : unit.members) {
                    auto& member = units_.at(id); member.sensor.position = m.target; member.sensor.step = m.step;
                    --member.sensor.battery; feedback.push_back(sensor(member));
                }
                proposals_.erase(m.mission_id);
            }
        }
    }
    deliver(std::move(feedback));
}
void LocalHardwareAdapter::observe(const RobotSnapshot& s) {
    std::lock_guard<std::mutex> lock(mutex_); const auto it = units_.find(s.id);
    if (it != units_.end() && it->second.sensor.generation == s.generation && it->second.sensor.mission_id == s.mission_id) it->second.observed = s.state;
}
bool LocalHardwareAdapter::inject(int id, std::optional<int> battery, std::optional<bool> failure) {
    if (battery && (*battery < 0 || *battery > 100)) return false;
    std::vector<Message> feedback;
    {
        std::lock_guard<std::mutex> lock(mutex_); auto it = units_.find(id); if (it == units_.end()) return false;
        if (battery) it->second.sensor.battery = *battery;
        if (failure) it->second.sensor.failure = *failure;
        if (it->second.sensor.failure || it->second.sensor.battery <= 15) proposals_.erase(it->second.sensor.mission_id);
        feedback.push_back(sensor(it->second));
    }
    deliver(std::move(feedback)); return true;
}
void LocalHardwareAdapter::tick() {
    std::vector<Message> feedback;
    {
        std::lock_guard<std::mutex> lock(mutex_); const auto now = std::chrono::steady_clock::now();
        for (auto& pair : units_) {
            auto& unit = pair.second;
            if (unit.observed != RobotState::CHARGING || unit.sensor.failure || unit.sensor.battery >= 100 || warehouse_.getCell(unit.sensor.position.x, unit.sensor.position.y) != CellState::CHARGING_STATION || now - unit.last_charge < std::chrono::milliseconds(500)) continue;
            unit.last_charge = now; unit.sensor.battery = std::min(100, unit.sensor.battery + 5); feedback.push_back(sensor(unit));
        }
    }
    deliver(std::move(feedback));
}
std::optional<Message> LocalHardwareAdapter::reading(int id) const {
    std::lock_guard<std::mutex> lock(mutex_); auto it = units_.find(id); if (it == units_.end()) return std::nullopt; return it->second.sensor;
}
