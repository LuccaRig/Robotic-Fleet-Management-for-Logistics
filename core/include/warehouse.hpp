#ifndef WAREHOUSE_HPP
#define WAREHOUSE_HPP

#include "logistics_types.hpp"
#include <cstdint>
#include <map>
#include <vector>

enum class CellState : std::uint8_t {
    EMPTY = 0, ROBOT = 1, CHARGING_STATION = 2, NARROW_TUNNEL = 3,
    PICKUP_POINT = 4, OBSTACLE = 5, DELIVERY_ZONE = 6
};

class WarehouseGrid {
public:
    WarehouseGrid(int width, int height);
    explicit WarehouseGrid(const std::vector<std::vector<int>>& layout);
    void setCell(int x, int y, CellState state);
    CellState getCell(int x, int y) const;
    bool traversable(Pos p) const;
    int getWidth() const { return width_; }
    int getHeight() const { return height_; }
    void printGrid() const;
    void clear();
    void addBay(LoadingBay bay);
    void addDestination(Destination destination);
    void addPassage(Passage passage);
    const std::map<int, LoadingBay>& bays() const { return bays_; }
    const std::map<int, Destination>& destinations() const { return destinations_; }
    const std::map<int, Passage>& passages() const { return passages_; }
    const Passage* passageAt(Pos p) const;
    std::vector<Pos> chargers() const;
    void validate() const;
private:
    int width_;
    int height_;
    std::vector<CellState> grid_;
    std::map<int, LoadingBay> bays_;
    std::map<int, Destination> destinations_;
    std::map<int, Passage> passages_;
    std::size_t index(int x, int y) const { return static_cast<std::size_t>(y) * width_ + x; }
};

#endif
