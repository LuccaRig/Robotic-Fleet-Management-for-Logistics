#include "passages.hpp"
#include <algorithm>
#include <iterator>
#include <stdexcept>

PassageCoordinator::PassageCoordinator(const WarehouseGrid& warehouse, Sender sender, Observer observer)
    : warehouse_(warehouse), sender_(std::move(sender)), observer_(std::move(observer)) {
    for (const auto& pair : warehouse.passages()) { Region region; region.definition = pair.second; regions_.emplace(pair.first, std::move(region)); }
}
std::string PassageCoordinator::key(const Message& m) { return m.coupled ? "e" + std::to_string(m.mission_id) : "r" + std::to_string(m.robot_id) + ":" + std::to_string(m.generation); }
int PassageCoordinator::direction(const Message& m) { const int dx = m.target.x - m.position.x, dy = m.target.y - m.position.y; return dx ? (dx > 0 ? 1 : -1) : (dy > 0 ? 2 : -2); }
Message PassageCoordinator::grant(const Message& request, int passage) const { auto m = request; m.type = MessageType::PassageGranted; m.passage_id = passage; m.accepted = true; return m; }
void PassageCoordinator::pump(Region& region, std::vector<Message>& grants) {
    while (!region.queue.empty()) {
        auto& waiting = region.queue.front(); const auto& first = waiting.requests.begin()->second;
        const int slots = first.coupled ? static_cast<int>(first.members.size()) : 1;
        const int flow = direction(first);
        if (region.occupied + slots > region.definition.capacity || (region.occupied && region.direction != flow)) break;
        region.claims.emplace(waiting.key, Claim{first, slots, flow, false}); region.occupied += slots; region.direction = flow;
        region.maximum = std::max(region.maximum, region.occupied);
        for (const auto& pair : waiting.requests) grants.push_back(grant(pair.second, region.definition.id));
        region.queue.pop_front();
    }
}
void PassageCoordinator::request(const Message& m) {
    std::vector<Message> grants;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto* passage = warehouse_.passageAt(m.target);
        if (!passage) grants.push_back(grant(m, -1));
        else {
            auto& region = regions_.at(passage->id); const auto claim_key = key(m);
            if (region.claims.count(claim_key)) grants.push_back(grant(m, passage->id));
            else {
                const int slots = m.coupled ? static_cast<int>(m.members.size()) : 1;
                if (slots < 1 || slots > passage->capacity || slots != m.team_size) throw std::runtime_error("Solicitação incompatível com a capacidade da passagem");
                auto waiting = std::find_if(region.queue.begin(), region.queue.end(), [&](const Waiting& item) { return item.key == claim_key; });
                if (waiting == region.queue.end()) { region.queue.push_back({claim_key, {}}); waiting = std::prev(region.queue.end()); }
                waiting->requests[m.robot_id] = m; pump(region, grants);
            }
        }
    }
    deliver(std::move(grants), {});
}
void PassageCoordinator::releaseVacant(Region& region, std::vector<PassageNotice>& notices) {
    for (auto it = region.claims.begin(); it != region.claims.end();) {
        auto members = it->second.context.coupled ? it->second.context.members : std::vector<int>{it->second.context.robot_id};
        bool occupied = false, known = true;
        for (const auto id : members) {
            const auto p = positions_.find(id); if (p == positions_.end()) { known = false; break; }
            const auto* passage = warehouse_.passageAt(p->second.position); if (passage && passage->id == region.definition.id) occupied = true;
        }
        if (occupied && !it->second.entered) { it->second.entered = true; notices.push_back({true, region.definition.id, it->second.slots, it->second.context}); }
        if (known && !occupied && it->second.entered) {
            notices.push_back({false, region.definition.id, it->second.slots, it->second.context}); region.occupied -= it->second.slots; it = region.claims.erase(it);
        } else ++it;
    }
    if (!region.occupied) region.direction = 0;
}
void PassageCoordinator::confirm(const Message& sensor) {
    std::vector<Message> grants; std::vector<PassageNotice> notices;
    {
        std::lock_guard<std::mutex> lock(mutex_); auto old = positions_.find(sensor.robot_id);
        if (old != positions_.end() && (sensor.generation < old->second.generation || (sensor.generation == old->second.generation && sensor.sensor_sequence < old->second.sensor_sequence))) return;
        positions_[sensor.robot_id] = sensor;
        for (auto& pair : regions_) { releaseVacant(pair.second, notices); pump(pair.second, grants); }
    }
    deliver(std::move(grants), std::move(notices));
}
void PassageCoordinator::cancel(int robot_id, EntityId mission_id) {
    std::vector<Message> grants;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& pair : regions_) {
            auto& region = pair.second;
            for (auto it = region.queue.begin(); it != region.queue.end();) {
                for (auto r = it->requests.begin(); r != it->requests.end();) {
                    if (r->first == robot_id || (mission_id && r->second.mission_id == mission_id)) r = it->requests.erase(r); else ++r;
                }
                if (it->requests.empty()) it = region.queue.erase(it); else ++it;
            }
            for (auto it = region.claims.begin(); it != region.claims.end();) {
                const auto& context = it->second.context;
                const bool matches = context.robot_id == robot_id || (mission_id && context.mission_id == mission_id);
                bool outside = true;
                for (const auto id : (context.coupled ? context.members : std::vector<int>{context.robot_id})) {
                    const auto p = positions_.find(id);
                    if (p == positions_.end()) { outside = false; break; }
                    const auto* passage = warehouse_.passageAt(p->second.position);
                    if (passage && passage->id == region.definition.id) outside = false;
                }
                if (matches && outside && !it->second.entered) { region.occupied -= it->second.slots; it = region.claims.erase(it); } else ++it;
            }
            if (!region.occupied) region.direction = 0; pump(region, grants);
        }
    }
    deliver(std::move(grants), {});
}
bool PassageCoordinator::permits(const Message& m) const {
    std::lock_guard<std::mutex> lock(mutex_); const auto* passage = warehouse_.passageAt(m.target);
    if (!passage) return true;
    const auto& claims = regions_.at(passage->id).claims;
    const auto it = claims.find(key(m)); return it != claims.end() && it->second.context.mission_id == m.mission_id;
}
std::vector<PassageSnapshot> PassageCoordinator::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_); std::vector<PassageSnapshot> result;
    for (const auto& pair : regions_) { const auto& r = pair.second; result.push_back({pair.first, r.definition.capacity, r.occupied, r.direction, r.queue.size(), r.maximum}); } return result;
}
bool PassageCoordinator::removeRobot(int id) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& pair : regions_) for (const auto& claim : pair.second.claims)
            if (claim.second.context.coupled && std::find(claim.second.context.members.begin(), claim.second.context.members.end(), id) != claim.second.context.members.end()) return false;
        const auto it = positions_.find(id); if (it == positions_.end()) return false;
        it->second.position = {-1, -1};
    }
    cancel(id, 0);
    Message removed; { std::lock_guard<std::mutex> lock(mutex_); removed = positions_.at(id); }
    confirm(removed); return true;
}
void PassageCoordinator::deliver(std::vector<Message> grants, std::vector<PassageNotice> notices) {
    for (auto& m : grants) sender_(std::move(m));
    for (const auto& n : notices) if (observer_) observer_(n);
}
