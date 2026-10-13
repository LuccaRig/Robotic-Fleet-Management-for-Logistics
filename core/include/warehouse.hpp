#ifndef WAREHOUSE_HPP
#define WAREHOUSE_HPP

#include <vector>
#include <cstdint>
#include <iostream>

enum class CellState : uint8_t {
    EMPTY = 0,          // Espaço livre 
    ROBOT,              // Robô ocupando a célula
    CHARGING_STATION,   // Ponto de abastecimento de energia
    NARROW_TUNNEL,      // Túnel estreito (restrição de n robô por vez)
    PICKUP_POINT,       // Ponto de retirada de carga
    OBSTACLE            // Parede ou obstáculo fixo
};

class WarehouseGrid {
private:
    int width;
    int height;
    std::vector<CellState> grid;

    inline int getIndex(int x, int y) const {
        return y * width + x;
    }

public:
    // Construtor 1: Cria um grid vazio com dimensões específicas
    WarehouseGrid(int width, int height);
    // Construtor 2: Inicializa o grid a partir de uma matriz 
    WarehouseGrid(const std::vector<std::vector<int>>& initial_layout);


    void setCell(int x, int y, CellState state);
    CellState getCell(int x, int y) const;
    int getWidth() const;
    int getHeight() const;
    void printGrid() const;
    void clear();
};

#endif // WAREHOUSE_HPP