#ifndef TOPICS_HPP
#define TOPICS_HPP

#include <string>

namespace Topics {

inline const char* root() { return "fleet/telemetry"; }

inline std::string robotTelemetry(int id) {
    return std::string(root()) + "/robots/" + std::to_string(id);
}

inline const char* fleetSnapshot() { return "fleet/telemetry/fleet"; }

inline const char* events() { return "fleet/events"; }

inline const char* commands() { return "fleet/commands"; }

inline const char* commandResults() { return "fleet/commands/results"; }

inline const char* simulatorCommands() { return "fleet/simulation/commands"; }

inline const char* simulatorEvents() { return "fleet/simulation/events"; }

}

#endif
