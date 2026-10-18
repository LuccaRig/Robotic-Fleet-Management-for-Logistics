#include "robot_agent.hpp"
#include <algorithm>
#include <stdexcept>

RobotAgent::RobotAgent(int id, int x, int y, std::uint64_t generation, Sender sender)
    : sender_(std::move(sender)), distance_position_{x, y} {
    state_.id = id; state_.generation = generation; state_.position = {x, y}; state_.target = {x, y};
    state_.connected = true; state_.available = true;
}
RobotAgent::~RobotAgent() { stop(); }
void RobotAgent::start() {
    std::lock_guard<std::mutex> lifecycle(lifecycle_);
    { std::lock_guard<std::mutex> lock(mutex_); if (running_) return; running_ = true; stopping_ = false; error_ = nullptr; }
    try { navigation_thread_ = std::thread(&RobotAgent::loop, this, true); status_thread_ = std::thread(&RobotAgent::loop, this, false); }
    catch (...) {
        { std::lock_guard<std::mutex> lock(mutex_); running_ = false; }
        cv_.notify_all(); if (navigation_thread_.joinable()) navigation_thread_.join(); throw;
    }
}
void RobotAgent::stop() {
    std::lock_guard<std::mutex> lifecycle(lifecycle_);
    { std::lock_guard<std::mutex> lock(mutex_); running_ = false; stopping_ = true; }
    cv_.notify_all();
    if (navigation_thread_.joinable()) navigation_thread_.join();
    if (status_thread_.joinable()) status_thread_.join();
}
RobotSnapshot RobotAgent::snapshot() const { std::lock_guard<std::mutex> lock(mutex_); auto copy = state_; copy.updated_at_ms = epochMilliseconds(); return copy; }
void RobotAgent::checkError() const { std::lock_guard<std::mutex> lock(mutex_); if (error_) std::rethrow_exception(error_); }
void RobotAgent::emit(Message message) { if (sender_) sender_(std::move(message)); }
void RobotAgent::publish() { emit(messageFromSnapshot(snapshot())); }
void RobotAgent::loop(bool navigation) noexcept {
    try {
        const auto period = std::chrono::milliseconds(navigation ? 100 : 500);
        auto next = std::chrono::steady_clock::now();
        while (true) {
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait_until(lock, next, [&] { return !running_ || (!navigation && (dirty_ || !sensor_reads_.empty())); });
                if (!running_) break;
            }
            if (navigation) navigationTick(); else statusTick();
            next = std::chrono::steady_clock::now() + period;
        }
    } catch (...) {
        { std::lock_guard<std::mutex> lock(mutex_); error_ = std::current_exception(); running_ = false; }
        cv_.notify_all();
    }
}

void RobotAgent::handle(const Message& m) {
    Message ack; bool acknowledge = false; bool sensor_changed = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (m.robot_id != state_.id || m.generation != state_.generation) return;
        if (m.type == MessageType::PassageGranted) {
            if (m.mission_id == state_.mission_id && m.revision == state_.revision && m.phase == state_.phase && waiting_passage_ && m.step == route_index_ + 1 && route_index_ < route_.size() && m.target == route_[route_index_]) {
                passage_granted_ = m.accepted; waiting_passage_ = false;
            }
        } else if (m.type == MessageType::SensorUpdate) {
            if (m.battery < 0 || m.battery > 100 || m.sensor_sequence <= state_.sensor_sequence) return;
            if (m.mission_id != state_.mission_id || m.revision != state_.revision) return;
            if (sensor_reads_.size() >= 4096) throw std::runtime_error("Fila de leituras do robô esgotada");
            sensor_reads_.push_back(m); state_.sensor_sequence = m.sensor_sequence;
            if (m.failure && !state_.failure) before_failure_ = state_.state;
            state_.battery = m.battery; state_.failure = m.failure; state_.coupled = m.coupled;
            state_.position = m.position;
            if (waiting_position_ && m.step == route_index_ + 1 && route_index_ < route_.size() && route_[route_index_] == m.position) {
                ++route_index_; state_.step = route_index_; waiting_position_ = false;
            }
            if (m.failure) state_.state = RobotState::ERROR;
            else if (state_.state == RobotState::ERROR) {
                if (state_.mission_id) state_.state = RobotState::SUSPENDED;
                else if (state_.phase == MissionPhase::TO_CHARGE || state_.phase == MissionPhase::CHARGING) state_.state = before_failure_;
                else state_.state = RobotState::IDLE;
            }
            if (state_.state == RobotState::COUPLING && m.coupled) state_.state = RobotState::WAITING_TO_LOAD;
            if (state_.state == RobotState::UNCOUPLING && !m.coupled) state_.state = RobotState::WAITING_TO_UNLOAD;
            dirty_ = true; sensor_changed = true;
        } else if (m.command_id != 0) {
            command_pending_ = true; acknowledge = true; ack.type = MessageType::CommandAck; ack.command_id = m.command_id;
            ack.mission_id = m.mission_id; ack.phase = m.phase;
            if (m.command_id <= last_command_) ack.accepted = m.command_id == last_command_ && last_accepted_;
            else {
                last_command_ = m.command_id; ack.accepted = true;
                const bool same_mission = m.mission_id == state_.mission_id;
                const bool pickup = m.phase == MissionPhase::TO_PICKUP && !state_.coupled && state_.state == RobotState::IDLE;
                const bool delivery = m.phase == MissionPhase::TO_DELIVERY && state_.coupled && same_mission && state_.state == RobotState::WAITING_TO_LOAD;
                const bool charging = m.phase == MissionPhase::TO_CHARGE && !state_.coupled && state_.mission_id == 0;
                if (m.type == MessageType::AssignMission && !state_.failure && (pickup || delivery || charging)) {
                    state_.mission_id = m.mission_id; state_.cargo_id = m.cargo_id; state_.team_id = m.team_id; state_.bay_id = m.bay_id;
                    state_.phase = m.phase; state_.target = m.target;
                    state_.state = charging ? RobotState::NAVIGATING_TO_CHARGE : (delivery ? RobotState::NAVIGATING_TO_DELIVERY : RobotState::NAVIGATING_TO_PICKUP);
                    route_ = m.route; members_ = m.members; route_index_ = 0; state_.step = 0; waiting_position_ = false; state_.available = false;
                } else if (m.type == MessageType::CancelMission && same_mission && !state_.coupled) {
                    state_.mission_id = 0; state_.cargo_id = 0; state_.team_id = 0; state_.bay_id = -1; state_.phase = MissionPhase::NONE;
                    state_.state = state_.failure ? RobotState::ERROR : RobotState::IDLE; route_.clear(); waiting_position_ = false;
                } else if (m.type == MessageType::Couple && same_mission && !state_.failure && !state_.coupled && state_.state == RobotState::WAITING_TO_LOAD) {
                    state_.phase = MissionPhase::COUPLING; state_.state = RobotState::COUPLING;
                } else if (m.type == MessageType::Uncouple && same_mission && !state_.failure && state_.coupled && state_.state == RobotState::WAITING_TO_UNLOAD) {
                    state_.phase = MissionPhase::UNCOUPLING; state_.state = RobotState::UNCOUPLING;
                } else if (m.type == MessageType::PauseMission && same_mission) {
                    state_.phase = MissionPhase::SUSPENDED; state_.state = state_.failure ? RobotState::ERROR : RobotState::SUSPENDED; waiting_position_ = false;
                } else if (m.type == MessageType::ResumeMission && same_mission && !state_.failure && state_.battery > 15 && state_.state == RobotState::SUSPENDED) {
                    if (m.phase == MissionPhase::COUPLING) {
                        state_.phase = m.phase; state_.state = state_.coupled ? RobotState::WAITING_TO_LOAD : RobotState::COUPLING;
                    } else if (m.phase == MissionPhase::UNCOUPLING) {
                        state_.phase = m.phase; state_.state = state_.coupled ? RobotState::UNCOUPLING : RobotState::WAITING_TO_UNLOAD;
                    } else if (m.phase == MissionPhase::TO_DELIVERY && state_.coupled) {
                        state_.phase = MissionPhase::TO_DELIVERY; state_.state = RobotState::NAVIGATING_TO_DELIVERY;
                        route_ = m.route; members_ = m.members; state_.target = m.target; route_index_ = 0; state_.step = 0; waiting_position_ = false;
                    } else ack.accepted = false;
                } else if (m.type != MessageType::Shutdown) ack.accepted = false;
                if (ack.accepted && m.type != MessageType::Shutdown) {
                    state_.revision = m.command_id; waiting_passage_ = false; passage_granted_ = false;
                }
                last_accepted_ = ack.accepted; dirty_ = true;
            }
        }
    }
    if (acknowledge) { emit(ack); std::lock_guard<std::mutex> lock(mutex_); command_pending_ = false; }
    if (sensor_changed) publish();
    cv_.notify_all();
}

void RobotAgent::navigationTick() {
    Message movement; bool send = false; bool changed = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const bool navigating = state_.state == RobotState::NAVIGATING_TO_PICKUP || state_.state == RobotState::NAVIGATING_TO_DELIVERY || state_.state == RobotState::NAVIGATING_TO_CHARGE;
        if (stopping_ || command_pending_ || state_.failure || !navigating || waiting_position_ || waiting_passage_) return;
        if (state_.battery == 0 || (state_.battery <= 15 && state_.state != RobotState::NAVIGATING_TO_CHARGE)) return;
        if (route_index_ >= route_.size()) {
            if (state_.phase == MissionPhase::TO_PICKUP) { state_.state = RobotState::WAITING_TO_LOAD; state_.phase = MissionPhase::PICKUP_READY; }
            else if (state_.phase == MissionPhase::TO_DELIVERY) { state_.state = RobotState::WAITING_TO_UNLOAD; state_.phase = MissionPhase::DELIVERY_READY; }
            else if (state_.phase == MissionPhase::TO_CHARGE) { state_.state = RobotState::CHARGING; state_.phase = MissionPhase::CHARGING; }
            changed = true;
        } else {
            movement = messageFromSnapshot(state_); movement.type = passage_granted_ ? MessageType::NavigationSetpoint : MessageType::PassageRequest;
            movement.target = route_[route_index_]; movement.step = route_index_ + 1; movement.members = members_;
            movement.team_size = state_.coupled ? std::max(1, static_cast<int>(members_.size())) : 1;
            if (passage_granted_) { waiting_position_ = true; passage_granted_ = false; }
            else waiting_passage_ = true;
            send = true;
        }
    }
    if (send) emit(movement); if (changed) publish();
}
void RobotAgent::statusTick() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        while (!sensor_reads_.empty()) {
            const auto& sensor = sensor_reads_.front();
            state_.distance += static_cast<std::uint64_t>(gridDistance(distance_position_, sensor.position));
            distance_position_ = sensor.position; sensor_reads_.pop_front();
        }
        if (state_.failure) state_.state = RobotState::ERROR;
        if (state_.state == RobotState::CHARGING && state_.battery >= 100 && !state_.failure) {
            state_.state = RobotState::IDLE; state_.phase = MissionPhase::NONE; state_.mission_id = 0;
            route_.clear(); waiting_position_ = false; waiting_passage_ = false; passage_granted_ = false;
        }
        state_.available = state_.state == RobotState::IDLE && !state_.failure && !state_.coupled && state_.battery > 15;
        dirty_ = false;
    }
    publish();
}
void RobotAgent::atribuirTarefa(const std::string&, int x, int y) {
    Message m; const auto s = snapshot(); m.type = MessageType::AssignMission; m.robot_id = s.id; m.generation = s.generation;
    { std::lock_guard<std::mutex> lock(mutex_); m.command_id = last_command_ + 1; }
    m.mission_id = 1; m.phase = MissionPhase::TO_PICKUP; m.target = {x, y};
    Pos p = s.position;
    while (p.x != x) { p.x += p.x < x ? 1 : -1; m.route.push_back(p); }
    while (p.y != y) { p.y += p.y < y ? 1 : -1; m.route.push_back(p); }
    handle(m);
}
void RobotAgent::setRobotStatus(int battery, bool failure) {
    auto s = snapshot(); Message m = messageFromSnapshot(s); m.type = MessageType::SensorUpdate;
    { std::lock_guard<std::mutex> lock(mutex_); m.sensor_sequence = std::max(++local_sensor_, state_.sensor_sequence + 1); }
    m.battery = battery; m.failure = failure; handle(m);
}
