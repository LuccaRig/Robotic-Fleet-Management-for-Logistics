#ifndef PASSAGES_HPP
#define PASSAGES_HPP

#include "protocol.hpp"
#include "warehouse.hpp"
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>

struct PassageSnapshot { int id; int capacity; int occupied; int direction; std::size_t waiting; int maximum; };
struct PassageNotice { bool entered; int passage_id; int slots; Message context; };
class PassageCoordinator {
public:
    using Sender = std::function<void(Message)>;
    using Observer = std::function<void(const PassageNotice&)>;
    PassageCoordinator(const WarehouseGrid& warehouse, Sender sender, Observer observer = {});
    void request(const Message& message);
    void confirm(const Message& sensor);
    void cancel(int robot_id, EntityId mission_id);
    bool permits(const Message& movement) const;
    std::vector<PassageSnapshot> snapshot() const;
    bool removeRobot(int id);
private:
    struct Claim { Message context; int slots; int direction; bool entered = false; };
    struct Waiting { std::string key; std::map<int, Message> requests; };
    struct Region { Passage definition; int occupied = 0; int direction = 0; int maximum = 0; std::map<std::string, Claim> claims; std::deque<Waiting> queue; };
    const WarehouseGrid& warehouse_;
    Sender sender_;
    Observer observer_;
    mutable std::mutex mutex_;
    std::map<int, Region> regions_;
    std::map<int, Message> positions_;
    static std::string key(const Message& message);
    static int direction(const Message& message);
    Message grant(const Message& request, int passage) const;
    void pump(Region& region, std::vector<Message>& grants);
    void releaseVacant(Region& region, std::vector<PassageNotice>& notices);
    void deliver(std::vector<Message> grants, std::vector<PassageNotice> notices);
};

#endif
