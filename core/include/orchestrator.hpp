#ifndef ORCHESTRATOR_HPP
#define ORCHESTRATOR_HPP

#include "protocol.hpp"
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct RobotProcess {
    int id = -1;
    int pid = -1;
    std::uint64_t generation = 0;
    bool ready = false;
    bool stopping = false;
};

class Orchestrator {
public:
    using Handler = std::function<void(const Message&)>;
    explicit Orchestrator(std::string executable = {});
    ~Orchestrator();
    Orchestrator(const Orchestrator&) = delete;
    Orchestrator& operator=(const Orchestrator&) = delete;
    bool spawnRobot(int id, int start_x, int start_y);
    bool terminateRobot(int id);
    bool send(int id, Message message);
    void setMessageHandler(Handler handler);
    void setPositionValidator(std::function<bool(Pos)> validator);
    std::vector<RobotProcess> processes() const;
    std::size_t getActiveRobotCount() const;
    bool robotExists(int id) const;
    void printAllRobotsStatus() const;
    void checkError() const;
    void stop();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

int runRobotWorker(int fd, int id, std::uint64_t generation, Pos start);

#endif
