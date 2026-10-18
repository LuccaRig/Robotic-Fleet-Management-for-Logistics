#include <gtest/gtest.h>
#include "robot_agent.hpp"
#include <algorithm>

namespace {
Message assign(int command = 1) {
    Message m; m.type = MessageType::AssignMission; m.robot_id = 1; m.generation = 1;
    m.command_id = command; m.mission_id = 7; m.phase = MissionPhase::TO_PICKUP; m.target = {1,0}; m.route = {{1,0}}; m.members = {1}; return m;
}
Message sensorFor(const RobotAgent& robot, std::uint64_t sequence) {
    auto m = messageFromSnapshot(robot.snapshot()); m.type = MessageType::SensorUpdate; m.sensor_sequence = sequence; return m;
}
}
TEST(RobotAgentTest, MovementRequiresPermissionAndConfirmedSensor) {
    std::vector<Message> sent; RobotAgent robot(1,0,0,1,[&](Message m) { sent.push_back(m); });
    robot.handle(assign()); robot.navigationTick();
    ASSERT_EQ(sent.back().type,MessageType::PassageRequest); EXPECT_EQ(robot.getX(),0);
    auto grant = sent.back(); grant.type = MessageType::PassageGranted; robot.handle(grant); robot.navigationTick();
    ASSERT_EQ(sent.back().type,MessageType::NavigationSetpoint); EXPECT_EQ(robot.getX(),0);
    auto sensor = sensorFor(robot,1); sensor.position = {1,0}; sensor.step = 1; robot.handle(sensor); robot.navigationTick(); robot.statusTick();
    EXPECT_EQ(robot.getX(),1); EXPECT_EQ(robot.getEstado(),RobotState::WAITING_TO_LOAD); EXPECT_EQ(robot.snapshot().distance,1u);
}
TEST(RobotAgentTest, RejectsObsoleteRevisionAndStopsOnFailure) {
    std::vector<Message> sent; RobotAgent robot(1,0,0,1,[&](Message m) { sent.push_back(m); }); robot.handle(assign());
    auto old = sensorFor(robot,1); old.revision = 0; old.position = {8,8}; robot.handle(old); EXPECT_EQ(robot.getX(),0);
    auto fault = sensorFor(robot,2); fault.failure = true; robot.handle(fault);
    const auto count = sent.size(); robot.navigationTick(); EXPECT_EQ(sent.size(),count); EXPECT_EQ(robot.getEstado(),RobotState::ERROR);
    fault.sensor_sequence = 3; fault.failure = false; robot.handle(fault); EXPECT_EQ(robot.getEstado(),RobotState::SUSPENDED);
}
TEST(RobotAgentTest, CountsAllPendingSensorSegments) {
    RobotAgent robot(1,0,0);
    const Pos path[]{{1,0},{1,1},{0,1},{0,0}};
    for (int i = 0; i < 4; ++i) { auto m = sensorFor(robot,i+1); m.position = path[i]; robot.handle(m); }
    robot.statusTick(); EXPECT_EQ(robot.snapshot().distance,4u);
}
TEST(RobotAgentTest, RejectsPrematureCouplingAndDuplicateCommandDoesNotResetRoute) {
    std::vector<Message> sent; RobotAgent robot(1,0,0,1,[&](Message m) { sent.push_back(m); }); auto mission = assign(); robot.handle(mission);
    auto invalid = mission; invalid.type = MessageType::Couple; invalid.command_id = 2; robot.handle(invalid);
    EXPECT_FALSE(sent.back().accepted); EXPECT_EQ(robot.getEstado(),RobotState::NAVIGATING_TO_PICKUP);
    robot.handle(mission); EXPECT_FALSE(sent.back().accepted);
}
TEST(RobotAgentTest, RepeatedStartStopIsSafe) { RobotAgent robot(1,0,0); robot.start(); robot.start(); robot.stop(); robot.stop(); robot.start(); robot.stop(); }
TEST(RobotAgentTest, ChargeEndsOnlyAtFullBattery) {
    RobotAgent robot(1,0,0); robot.setRobotStatus(10,false);
    auto m = assign(); m.mission_id = 0; m.phase = MissionPhase::TO_CHARGE; m.route.clear(); m.target = {0,0}; robot.handle(m); robot.navigationTick();
    EXPECT_EQ(robot.getEstado(),RobotState::CHARGING); robot.setRobotStatus(95,false); robot.statusTick(); EXPECT_EQ(robot.getEstado(),RobotState::CHARGING);
    robot.setRobotStatus(100,false); robot.statusTick(); EXPECT_EQ(robot.getEstado(),RobotState::IDLE);
}
TEST(RobotAgentTest, PublishesTransientFaultBeforeFollowingRecovery) {
    std::vector<Message> sent; RobotAgent robot(1,0,0,1,[&](Message m) { sent.push_back(m); });
    robot.setRobotStatus(50,true); robot.setRobotStatus(50,false);
    ASSERT_GE(sent.size(),2u); EXPECT_TRUE(sent[sent.size()-2].failure); EXPECT_FALSE(sent.back().failure);
}
