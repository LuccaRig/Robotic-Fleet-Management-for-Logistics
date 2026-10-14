#ifndef ORCHESTRATOR_HPP
#define ORCHESTRATOR_HPP

#include "robot_agent.hpp"
#include <vector>
#include <memory>
#include <map>
#include <mutex>
#include <string>

class Orchestrator {
private:
    std::map<int, std::unique_ptr<RobotAgent>> robots;
    mutable std::mutex orchestrator_mutex;

public:
    Orchestrator();
    ~Orchestrator();

    bool spawnRobot(int id, int start_x, int start_y);
    bool terminateRobot(int id);
    void printAllRobotsStatus() const;
    RobotAgent* getRobot(int id);
    size_t getActiveRobotCount() const;
    bool robotExists(int id) const;
};

#endif // ORCHESTRATOR_HPP