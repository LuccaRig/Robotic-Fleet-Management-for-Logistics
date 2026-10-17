#include "warehouse.hpp"

WarehouseGrid::WarehouseGrid(int width, int height) 
    : width(width), height(height), grid(width * height, CellState::EMPTY) {}

WarehouseGrid::WarehouseGrid(const std::vector<std::vector<int>>& initial_layout) {
    //TODO: CHECAR SE A INICIALIZAÇÃO POSSUI ALGUM ROBÔ, POIS NÃO PODEM EXISTIR ROBÔS NO GRID INICIAL, POIS O ORQUESTRADOR É QUE VAI GERENCIAR OS ROBÔS

    if (initial_layout.empty() || initial_layout[0].empty()) {
        width = 0;
        height = 0;
        return;
    }

    height = initial_layout.size();
    width = initial_layout[0].size();
    grid.resize(width * height);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int val = initial_layout[y][x];
            grid[getIndex(x, y)] = static_cast<CellState>(val);
        }
    }
}

void WarehouseGrid::setCell(int x, int y, CellState state) {
    if (x >= 0 && x < width && y >= 0 && y < height) {
        grid[getIndex(x, y)] = state;
    }
}

CellState WarehouseGrid::getCell(int x, int y) const {
    if (x >= 0 && x < width && y >= 0 && y < height) {
        return grid[getIndex(x, y)];
    }
    return CellState::OBSTACLE; 
}

int WarehouseGrid::getWidth() const {
    return width;
}

int WarehouseGrid::getHeight() const {
    return height;
}

void WarehouseGrid::printGrid() const {
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            std::cout << static_cast<int>(getCell(x, y)) << " ";
        }
        std::cout << "\n";
    }
}

void WarehouseGrid::clear() {
    std::fill(grid.begin(), grid.end(), CellState::EMPTY);
}