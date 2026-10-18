#ifndef EVENT_RECORDER_HPP
#define EVENT_RECORDER_HPP

#include "buffer.hpp"
#include "logistics.hpp"
#include "passages.hpp"
#include <atomic>
#include <functional>
#include <map>
#include <mutex>

class EventRecorder {
public:
    using Observer = std::function<void(const RobotEvent&)>;
    explicit EventRecorder(Buffer& buffer, Observer observer = {});
    void onRobot(const Message& message);
    void onLogistics(const LogisticsNotice& notice);
    void onPassage(const PassageNotice& notice);
private:
    Buffer& buffer_;
    Observer observer_;
    std::mutex mutex_;
    std::map<int, RobotSnapshot> previous_;
    std::atomic<std::uint64_t> sequence_{0};
    void record(RobotEvent event);
};

#endif
