#include "warehouse.hpp"
#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>

WarehouseGrid::WarehouseGrid(int width, int height) : width_(width), height_(height) {
    if (width <= 0 || height <= 0 || static_cast<long long>(width) * height > 1000000) throw std::invalid_argument("Dimensões de mapa inválidas");
    grid_.assign(static_cast<std::size_t>(width) * height, CellState::EMPTY);
}
WarehouseGrid::WarehouseGrid(const std::vector<std::vector<int>>& layout)
    : WarehouseGrid(layout.empty() ? 0 : static_cast<int>(layout.front().size()), static_cast<int>(layout.size())) {
    for (int y = 0; y < height_; ++y) {
        if (layout[y].size() != static_cast<std::size_t>(width_)) throw std::invalid_argument("O mapa deve ser retangular");
        for (int x = 0; x < width_; ++x) {
            const int value = layout[y][x];
            if (value < 0 || value > 6 || value == 1) throw std::invalid_argument("Célula inicial inválida; robôs são gerenciados separadamente");
            grid_[index(x, y)] = static_cast<CellState>(value);
        }
    }
}
void WarehouseGrid::setCell(int x, int y, CellState state) {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) throw std::out_of_range("Posição fora do mapa");
    if (state == CellState::ROBOT || static_cast<unsigned>(state) > 6) throw std::invalid_argument("O mapa estático não armazena robôs");
    grid_[index(x, y)] = state;
}
CellState WarehouseGrid::getCell(int x, int y) const {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) return CellState::OBSTACLE;
    return grid_[index(x, y)];
}
bool WarehouseGrid::traversable(Pos p) const { return getCell(p.x, p.y) != CellState::OBSTACLE; }
void WarehouseGrid::printGrid() const {
    for (int y = 0; y < height_; ++y) { for (int x = 0; x < width_; ++x) std::cout << static_cast<int>(getCell(x, y)) << ' '; std::cout << '\n'; }
}
void WarehouseGrid::clear() { std::fill(grid_.begin(), grid_.end(), CellState::EMPTY); bays_.clear(); destinations_.clear(); passages_.clear(); }
void WarehouseGrid::addBay(LoadingBay bay) {
    if (bay.id < 0 || !traversable(bay.position) || bays_.count(bay.id)) throw std::invalid_argument("Baía inválida ou duplicada");
    setCell(bay.position.x, bay.position.y, CellState::PICKUP_POINT); bays_.emplace(bay.id, std::move(bay));
}
void WarehouseGrid::addDestination(Destination d) {
    if (d.id < 0 || !traversable(d.position) || destinations_.count(d.id)) throw std::invalid_argument("Destino inválido ou duplicado");
    setCell(d.position.x, d.position.y, CellState::DELIVERY_ZONE); destinations_.emplace(d.id, d);
}
void WarehouseGrid::addPassage(Passage p) {
    if (p.id < 0 || p.capacity < 1 || p.capacity > 4 || p.cells.empty() || passages_.count(p.id)) throw std::invalid_argument("Passagem inválida ou duplicada");
    for (auto cell : p.cells) if (!traversable(cell) || passageAt(cell)) throw std::invalid_argument("Região de passagem inválida");
    for (auto cell : p.cells) setCell(cell.x, cell.y, CellState::NARROW_TUNNEL);
    passages_.emplace(p.id, std::move(p));
}
const Passage* WarehouseGrid::passageAt(Pos p) const {
    for (const auto& pair : passages_) for (const auto cell : pair.second.cells) if (cell == p) return &pair.second;
    return nullptr;
}
std::vector<Pos> WarehouseGrid::chargers() const {
    std::vector<Pos> result;
    for (int y = 0; y < height_; ++y) for (int x = 0; x < width_; ++x) if (getCell(x, y) == CellState::CHARGING_STATION) result.push_back({x, y});
    return result;
}
void WarehouseGrid::validate() const {
    for (int y = 0; y < height_; ++y) for (int x = 0; x < width_; ++x)
        if (getCell(x, y) == CellState::NARROW_TUNNEL && !passageAt({x, y})) throw std::invalid_argument("Túnel sem capacidade cadastrada");
    for (const auto& a : passages_) for (const auto cell : a.second.cells) {
        for (const Pos delta : std::vector<Pos>{{1,0},{-1,0},{0,1},{0,-1}}) {
            const auto* b = passageAt({cell.x + delta.x, cell.y + delta.y});
            if (b && b->id != a.first) throw std::invalid_argument("Passagens distintas devem ter uma área livre entre si");
        }
    }
    for (const auto& b : bays_) if (!traversable(b.second.position) || passageAt(b.second.position)) throw std::invalid_argument("Baía em posição inválida");
    for (const auto& d : destinations_) if (!traversable(d.second.position) || passageAt(d.second.position)) throw std::invalid_argument("Destino em posição inválida");
}
