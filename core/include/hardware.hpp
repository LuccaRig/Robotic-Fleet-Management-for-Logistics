#ifndef HARDWARE_HPP
#define HARDWARE_HPP

#include "protocol.hpp"
#include "warehouse.hpp"
#include <chrono>
#include <functional>
#include <map>
#include <mutex>
#include <optional>

class HardwareAdapter {
public:
    virtual ~HardwareAdapter() = default;
    virtual void commandAccepted(const Message& command) = 0;
    virtual void setpoint(const Message& message) = 0;
    virtual void tick() = 0;
};

class LocalHardwareAdapter : public HardwareAdapter {
public:
    using SensorSink = std::function<void(Message)>;
    explicit LocalHardwareAdapter(const WarehouseGrid& warehouse, SensorSink sink);
    void registerRobot(const RobotSnapshot& snapshot);
    bool contains(int id) const;
    bool removeRobot(int id);
    void commandAccepted(const Message& command) override;
    void setpoint(const Message& message) override;
    void tick() override;
    void observe(const RobotSnapshot& snapshot);
    bool inject(int id, std::optional<int> battery, std::optional<bool> failure);
    std::optional<Message> reading(int id) const;
    void setMovementValidator(std::function<bool(const Message&)> validator);
private:
    struct Unit {
        Message sensor;
        std::vector<int> members;
        bool paused = false;
        RobotState observed = RobotState::IDLE;
        std::chrono::steady_clock::time_point last_charge = std::chrono::steady_clock::now();
    };
    const WarehouseGrid& warehouse_;
    SensorSink sink_;
    mutable std::mutex mutex_;
    std::map<int, Unit> units_;
    std::map<EntityId, std::map<int, Message>> proposals_;
    std::function<bool(const Message&)> validator_;
    Message sensor(Unit& unit);
    void deliver(std::vector<Message> messages);
};

#endif
