#include <gtest/gtest.h>
#include "route_planner.hpp"

TEST(WarehouseGridTest, RejectsInvalidLayoutAndKeepsRobotOccupationSeparate) {
    EXPECT_THROW(WarehouseGrid(0,3),std::invalid_argument);
    EXPECT_THROW((WarehouseGrid(std::vector<std::vector<int>>{{0,0},{0}})),std::invalid_argument);
    EXPECT_THROW((WarehouseGrid(std::vector<std::vector<int>>{{1}})),std::invalid_argument);
    WarehouseGrid map(3,3); EXPECT_THROW(map.setCell(1,1,CellState::ROBOT),std::invalid_argument);
    EXPECT_EQ(map.getCell(-1,0),CellState::OBSTACLE);
}
TEST(RoutePlannerTest, AvoidsWallsAndInsufficientCapacity) {
    WarehouseGrid map(7,5);
    for (int y = 0; y < 5; ++y) map.setCell(3,y,CellState::OBSTACLE);
    map.setCell(3,1,CellState::EMPTY); map.addPassage({1,1,{{3,1}}});
    map.setCell(3,4,CellState::EMPTY); map.addPassage({2,4,{{3,4}}});
    RoutePlanner planner(map); auto solo = planner.route({1,1},{5,1},1); auto team = planner.route({1,1},{5,1},4);
    ASSERT_TRUE(solo); ASSERT_TRUE(team); EXPECT_LT(solo->size(),team->size());
    for (auto point : *team) EXPECT_NE(point,(Pos{3,1}));
    auto empty = planner.route({1,1},{1,1}); ASSERT_TRUE(empty); EXPECT_TRUE(empty->empty());
    EXPECT_FALSE(planner.route({-1,0},{1,1}));
}
TEST(RoutePlannerTest, DistinguishesUnreachableCharger) {
    WarehouseGrid map(3,3); RoutePlanner planner(map); EXPECT_FALSE(planner.nearestCharger({1,1}));
    map.setCell(0,0,CellState::CHARGING_STATION); ASSERT_TRUE(planner.nearestCharger({1,1}));
}
