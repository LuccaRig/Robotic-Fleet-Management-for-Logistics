#include "route_planner.hpp"
#include <algorithm>
#include <queue>

std::optional<std::vector<Pos>> RoutePlanner::route(Pos start, Pos target, int team_size) const {
    if (team_size < 1 || team_size > 4) return std::nullopt;
    auto allowed = [&](Pos p) { const auto* passage = warehouse_.passageAt(p); return warehouse_.traversable(p) && (!passage || passage->capacity >= team_size); };
    if (!allowed(start) || !allowed(target)) return std::nullopt;
    const int width = warehouse_.getWidth();
    const auto index = [width](Pos p) { return p.y * width + p.x; };
    const auto position = [width](int n) { return Pos{n % width, n / width}; };
    std::vector<int> parent(static_cast<std::size_t>(width) * warehouse_.getHeight(), -1);
    std::queue<Pos> queue; queue.push(start); parent[index(start)] = index(start);
    const Pos deltas[]{{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
    while (!queue.empty() && parent[index(target)] < 0) {
        const auto current = queue.front(); queue.pop();
        for (const auto delta : deltas) {
            Pos next{current.x + delta.x, current.y + delta.y};
            if (!allowed(next) || parent[index(next)] >= 0) continue;
            parent[index(next)] = index(current); queue.push(next);
        }
    }
    if (parent[index(target)] < 0) return std::nullopt;
    std::vector<Pos> path;
    for (int current = index(target); current != index(start); current = parent[current]) path.push_back(position(current));
    std::reverse(path.begin(), path.end()); return path;
}
std::optional<std::pair<Pos, std::vector<Pos>>> RoutePlanner::nearestCharger(Pos start) const {
    std::optional<std::pair<Pos, std::vector<Pos>>> result;
    for (const auto charger : warehouse_.chargers()) {
        auto path = route(start, charger);
        if (path && (!result || path->size() < result->second.size())) result = std::make_pair(charger, std::move(*path));
    }
    return result;
}
