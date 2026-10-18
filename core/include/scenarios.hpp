#ifndef SCENARIOS_HPP
#define SCENARIOS_HPP

#include "runtime.hpp"

Scenario makeScenario(const std::string& name);
std::string scenarioNames();
class ScenarioScript {
public:
    explicit ScenarioScript(std::string name) : name_(std::move(name)) {}
    void tick(ControlRuntime& runtime);
    bool complete(const ControlRuntime& runtime) const;
private:
    std::string name_;
    int stage_ = 0;
    std::uint64_t generation_ = 0;
    bool queue_observed_ = false;
    std::set<int> passages_seen_;
};

#endif
