#include <gtest/gtest.h>
#include "route_planner.hpp"

TEST(WarehouseGridTest, RejectsInvalidLayoutAndKeepsRobotOccupationSeparate) {
    EXPECT_THROW(MapaGalpao(0,3),std::invalid_argument);
    EXPECT_THROW((MapaGalpao(std::vector<std::vector<int>>{{0,0},{0}})),std::invalid_argument);
    EXPECT_THROW((MapaGalpao(std::vector<std::vector<int>>{{1}})),std::invalid_argument);
    MapaGalpao map(3,3); EXPECT_THROW(map.definirCelula(1,1,EstadoCelula::ROBO),std::invalid_argument);
    EXPECT_EQ(map.obterCelula(-1,0),EstadoCelula::OBSTACULO);
}
TEST(RoutePlannerTest, AvoidsWallsAndInsufficientCapacity) {
    MapaGalpao map(7,5);
    for (int y = 0; y < 5; ++y) map.definirCelula(3,y,EstadoCelula::OBSTACULO);
    map.definirCelula(3,1,EstadoCelula::LIVRE); map.adicionarTrecho({1,1,{{3,1}}});
    map.definirCelula(3,4,EstadoCelula::LIVRE); map.adicionarTrecho({2,4,{{3,4}}});
    PlanejadorRotas planner(map); auto solo = planner.calcularRota({1,1},{5,1},1); auto team = planner.calcularRota({1,1},{5,1},4);
    ASSERT_TRUE(solo); ASSERT_TRUE(team); EXPECT_LT(solo->size(),team->size());
    for (auto point : *team) EXPECT_NE(point,(Posicao{3,1}));
    auto empty = planner.calcularRota({1,1},{1,1}); ASSERT_TRUE(empty); EXPECT_TRUE(empty->empty());
    EXPECT_FALSE(planner.calcularRota({-1,0},{1,1}));
}
TEST(RoutePlannerTest, DistinguishesUnreachableCharger) {
    MapaGalpao map(3,3); PlanejadorRotas planner(map); EXPECT_FALSE(planner.carregadorMaisProximo({1,1}));
    map.definirCelula(0,0,EstadoCelula::ESTACAO_RECARGA); ASSERT_TRUE(planner.carregadorMaisProximo({1,1}));
}
