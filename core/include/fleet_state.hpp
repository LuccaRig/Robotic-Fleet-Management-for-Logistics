#ifndef FLEET_STATE_HPP
#define FLEET_STATE_HPP

#include "domain.hpp"
#include <map>
#include <mutex>
#include <optional>
#include <vector>

class FleetStateStore {
public:
    bool registerRobot(int id, std::uint64_t generation, Pos start) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = robots_.find(id);
        if (it != robots_.end() && generation <= it->second.generation) return false;
        RobotSnapshot snapshot; snapshot.id = id; snapshot.generation = generation; snapshot.position = start;
        snapshot.target = start; snapshot.updated_at_ms = epochMilliseconds();
        robots_[id] = snapshot; return true;
    }
    bool update(RobotSnapshot snapshot) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = robots_.find(snapshot.id);
        if (it == robots_.end() || snapshot.generation != it->second.generation || snapshot.sequence <= it->second.sequence || snapshot.revision < it->second.revision || snapshot.sensor_sequence < it->second.sensor_sequence || (!it->second.connected && it->second.sequence > 0)) return false;
        it->second = std::move(snapshot); return true;
    }
    bool disconnect(int id, std::uint64_t generation) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = robots_.find(id);
        if (it == robots_.end() || generation != it->second.generation) return false;
        it->second.connected = false; it->second.available = false; it->second.updated_at_ms = epochMilliseconds(); return true;
    }
    std::optional<RobotSnapshot> get(int id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = robots_.find(id);
        if (it == robots_.end()) return std::nullopt;
        return it->second;
    }
    std::vector<RobotSnapshot> snapshot() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<RobotSnapshot> result;
        for (const auto& pair : robots_) result.push_back(pair.second);
        return result;
    }
private:
    mutable std::mutex mutex_;
    std::map<int, RobotSnapshot> robots_;
};

#endif
