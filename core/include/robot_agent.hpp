#ifndef ROBOT_AGENT_HPP
#define ROBOT_AGENT_HPP

#include "protocol.hpp"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>

class RobotAgent {
public:
    using Sender = std::function<void(Message)>;
    RobotAgent(int id, int x, int y, std::uint64_t generation = 1, Sender sender = {});
    ~RobotAgent();
    RobotAgent(const RobotAgent&) = delete;
    RobotAgent& operator=(const RobotAgent&) = delete;
    void start();
    void stop();
    void handle(const Message& message);
    void navigationTick();
    void statusTick();
    RobotSnapshot snapshot() const;
    void checkError() const;
    void atribuirTarefa(const std::string& description, int x, int y);
    void ComandoDeNavegacao() { navigationTick(); }
    void StatusDoRobo() { statusTick(); }
    void setRobotStatus(int battery, bool failure);
    int getId() const { return snapshot().id; }
    int getX() const { return snapshot().position.x; }
    int getY() const { return snapshot().position.y; }
    int getBateria() const { return snapshot().battery; }
    RobotState getEstado() const { return snapshot().state; }
    bool isFalha() const { return snapshot().failure; }
private:
    mutable std::mutex mutex_;
    std::mutex lifecycle_;
    std::condition_variable cv_;
    RobotSnapshot state_;
    Sender sender_;
    std::vector<Pos> route_;
    std::vector<int> members_;
    std::deque<Message> sensor_reads_;
    Pos distance_position_;
    std::size_t route_index_ = 0;
    bool waiting_position_ = false;
    bool waiting_passage_ = false;
    bool passage_granted_ = false;
    bool dirty_ = true;
    bool command_pending_ = false;
    bool running_ = false;
    bool stopping_ = false;
    std::uint64_t last_command_ = 0;
    bool last_accepted_ = true;
    std::uint64_t local_sensor_ = 0;
    std::thread navigation_thread_;
    std::thread status_thread_;
    std::exception_ptr error_;
    void emit(Message message);
    void publish();
    void loop(bool navigation) noexcept;
};

#endif
