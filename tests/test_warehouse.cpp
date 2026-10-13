#include <gtest/gtest.h>
#include "warehouse.hpp"

// Teste para verificar se o grid inicializa corretamente com a matriz arbitrária
TEST(WarehouseGridTest, InitializationAndGetCell) {
    std::vector<std::vector<int>> mapa_inicial = {
        {5, 5, 5},
        {5, 2, 0},
        {5, 4, 1}
    };

    WarehouseGrid warehouse(mapa_inicial);

    // Verifica dimensões
    EXPECT_EQ(warehouse.getWidth(), 3);
    EXPECT_EQ(warehouse.getHeight(), 3);

    // Verifica células específicas baseadas no enum CellState
    // 0 = EMPTY, 1 = ROBOT, 2 = CHARGING_STATION, 4 = PICKUP_POINT, 5 = OBSTACLE
    EXPECT_EQ(warehouse.getCell(1, 1), CellState::CHARGING_STATION);
    EXPECT_EQ(warehouse.getCell(2, 1), CellState::EMPTY);
    EXPECT_EQ(warehouse.getCell(2, 2), CellState::ROBOT);
    EXPECT_EQ(warehouse.getCell(1, 2), CellState::PICKUP_POINT);
    EXPECT_EQ(warehouse.getCell(0, 0), CellState::OBSTACLE);
}

// Teste de segurança para limites da matriz
TEST(WarehouseGridTest, OutOfBoundsSafety) {
    WarehouseGrid warehouse(3, 3);

    // Acessar fora dos limites deve retornar OBSTACLE por segurança
    EXPECT_EQ(warehouse.getCell(-1, 0), CellState::OBSTACLE);
    EXPECT_EQ(warehouse.getCell(5, 5), CellState::OBSTACLE);
}