#ifndef ROUTE_PLANNER_HPP
#define ROUTE_PLANNER_HPP

#include "warehouse.hpp"
#include <optional>
#include <utility>

class RoutePlanner {
public:
    explicit RoutePlanner(const WarehouseGrid& warehouse) : warehouse_(warehouse) {}
    std::optional<std::vector<Pos>> route(Pos start, Pos target, int team_size = 1) const;
    std::optional<std::pair<Pos, std::vector<Pos>>> nearestCharger(Pos start) const;
private:
    const WarehouseGrid& warehouse_;
};

#endif
