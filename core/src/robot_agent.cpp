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
    Message ack; bool acknowledge = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (m.robot_id != state_.id || m.generation != state_.generation) return;
        if (m.type == MessageType::SensorUpdate) {
            if (m.battery < 0 || m.battery > 100 || m.sensor_sequence <= state_.sensor_sequence) return;
            if (m.mission_id != state_.mission_id) return;
            sensor_reads_.push_back(m); state_.sensor_sequence = m.sensor_sequence;
            state_.battery = m.battery; state_.failure = m.failure; state_.coupled = m.coupled;
            state_.position = m.position;
            if (waiting_position_ && m.step == route_index_ + 1 && route_index_ < route_.size() && route_[route_index_] == m.position) {
                ++route_index_; state_.step = route_index_; waiting_position_ = false;
            }
            if (m.failure) state_.state = RobotState::ERROR;
            dirty_ = true;
        } else if (m.command_id != 0) {
            acknowledge = true; ack.type = MessageType::CommandAck; ack.command_id = m.command_id;
            ack.mission_id = m.mission_id; ack.phase = m.phase;
            if (m.command_id <= last_command_) ack.accepted = m.command_id == last_command_ && last_accepted_;
            else {
                last_command_ = m.command_id; ack.accepted = true;
                if (m.type == MessageType::AssignMission && !state_.failure && !state_.coupled) {
                    state_.mission_id = m.mission_id; state_.cargo_id = m.cargo_id; state_.team_id = m.team_id; state_.bay_id = m.bay_id;
                    state_.phase = m.phase; state_.target = m.target; state_.state = RobotState::NAVIGATING_TO_PICKUP;
                    route_ = m.route; members_ = m.members; route_index_ = 0; state_.step = 0; waiting_position_ = false; state_.available = false;
                } else if (m.type == MessageType::CancelMission && !state_.coupled) {
                    state_.mission_id = 0; state_.cargo_id = 0; state_.team_id = 0; state_.phase = MissionPhase::NONE;
                    state_.state = state_.failure ? RobotState::ERROR : RobotState::IDLE; route_.clear(); waiting_position_ = false;
                } else if (m.type != MessageType::Shutdown) ack.accepted = false;
                last_accepted_ = ack.accepted; dirty_ = true;
            }
        }
    }
    cv_.notify_all(); if (acknowledge) emit(ack);
}

void RobotAgent::navigationTick() {
    Message movement; bool send = false; bool changed = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_ || state_.failure || state_.coupled || state_.state != RobotState::NAVIGATING_TO_PICKUP || waiting_position_) return;
        if (route_index_ >= route_.size()) {
            state_.state = RobotState::WAITING_TO_LOAD; state_.phase = MissionPhase::PICKUP_READY; changed = true;
        } else {
            movement = messageFromSnapshot(state_); movement.type = MessageType::NavigationSetpoint;
            movement.target = route_[route_index_]; movement.step = route_index_ + 1; movement.members = members_;
            movement.team_size = std::max(1, static_cast<int>(members_.size())); waiting_position_ = true; send = true;
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
