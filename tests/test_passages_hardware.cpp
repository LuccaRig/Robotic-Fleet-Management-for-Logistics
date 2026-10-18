#include <gtest/gtest.h>
#include "passages.hpp"
#include "hardware.hpp"

namespace {
Message at(int id, Pos p, std::uint64_t sequence = 0) { Message m; m.robot_id = id; m.generation = 1; m.position = p; m.sensor_sequence = sequence; return m; }
}
TEST(PassageTest, PreservesCapacityAndQueuesOppositeFlowUntilExit) {
    WarehouseGrid map(5,3); map.addPassage({1,2,{{2,1}}}); std::vector<Message> grants;
    PassageCoordinator coordinator(map,[&](Message m) { grants.push_back(m); });
    for (int id : {1,2}) coordinator.confirm(at(id,{1,1})); coordinator.confirm(at(3,{3,1}));
    auto one = at(1,{1,1}); one.target = {2,1}; coordinator.request(one);
    auto two = one; two.robot_id = 2; coordinator.request(two);
    auto opposite = at(3,{3,1}); opposite.target = {2,1}; coordinator.request(opposite);
    ASSERT_EQ(grants.size(),2u); EXPECT_EQ(coordinator.snapshot()[0].occupied,2);
    coordinator.confirm(at(1,{2,1},1)); coordinator.cancel(1,0); EXPECT_EQ(coordinator.snapshot()[0].occupied,2);
    coordinator.confirm(at(1,{3,1},2)); EXPECT_EQ(grants.size(),2u);
    coordinator.confirm(at(2,{2,1},1)); coordinator.confirm(at(2,{3,1},2));
    ASSERT_EQ(grants.size(),3u); EXPECT_EQ(grants.back().robot_id,3); EXPECT_EQ(coordinator.snapshot()[0].maximum,2);
}
TEST(PassageTest, TeamReservationIsIndivisible) {
    WarehouseGrid map(5,3); map.addPassage({1,2,{{2,1}}}); std::vector<Message> grants;
    PassageCoordinator coordinator(map,[&](Message m) { grants.push_back(m); });
    coordinator.confirm(at(1,{1,1})); coordinator.confirm(at(2,{1,1}));
    auto m = at(1,{1,1}); m.coupled = true; m.mission_id = 1; m.members = {1,2}; m.team_size = 2; m.target = {2,1}; coordinator.request(m);
    EXPECT_EQ(coordinator.snapshot()[0].occupied,2); m.robot_id = 2; coordinator.request(m); EXPECT_EQ(coordinator.snapshot()[0].occupied,2);
    m.mission_id = 2; m.members.push_back(3); m.team_size = 3; EXPECT_THROW(coordinator.request(m),std::runtime_error);
}
TEST(HardwareTest, GroupMovesOnlyAfterEveryProposalAndRejectsStaleRevision) {
    WarehouseGrid map(4,3); std::vector<Message> sensors;
    LocalHardwareAdapter hardware(map,[&](Message m) { sensors.push_back(m); });
    for (int id : {1,2}) {
        RobotSnapshot s; s.id = id; s.generation = 1; hardware.registerRobot(s);
        Message m; m.robot_id = id; m.generation = 1; m.type = MessageType::AssignMission; m.command_id = 1; m.mission_id = 1; m.phase = MissionPhase::TO_PICKUP; m.members = {id}; hardware.commandAccepted(m);
        m.type = MessageType::Couple; m.command_id = 2; hardware.commandAccepted(m);
        m.type = MessageType::AssignMission; m.command_id = 3; m.phase = MissionPhase::TO_DELIVERY; m.members = {1,2}; hardware.commandAccepted(m);
    }
    auto first = *hardware.reading(1); first.target = {1,0}; first.step = 1; hardware.setpoint(first); EXPECT_EQ(hardware.reading(1)->position,(Pos{0,0}));
    auto second = *hardware.reading(2); second.target = {1,0}; second.step = 1; hardware.setpoint(second);
    EXPECT_EQ(hardware.reading(1)->position,(Pos{1,0})); EXPECT_EQ(hardware.reading(2)->position,(Pos{1,0})); EXPECT_EQ(hardware.reading(1)->battery,99);
    auto pause = first; pause.type = MessageType::PauseMission; pause.command_id = 4; hardware.commandAccepted(pause);
    first.step = 2; first.position = {1,0}; first.target = {2,0}; hardware.setpoint(first); EXPECT_EQ(hardware.reading(1)->position,(Pos{1,0}));
}
