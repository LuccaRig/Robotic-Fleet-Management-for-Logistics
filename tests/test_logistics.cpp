#include <gtest/gtest.h>
#include "logistics.hpp"
#include <algorithm>

namespace {
WarehouseGrid layout() { WarehouseGrid map(6,5); map.addBay({1,{1,1},{}}); map.addDestination({1,{4,1}}); map.setCell(0,0,CellState::CHARGING_STATION); return map; }
void registerRobot(FleetStateStore& fleet, int id) {
    fleet.registerRobot(id,1,{0,1}); RobotSnapshot s; s.id = id; s.generation = 1; s.sequence = 1; s.position = {0,1}; s.available = true; s.connected = true; fleet.update(s);
}
Cargo load(int size) { Cargo c; c.id = 1; c.size = size; c.bay_id = 1; c.destination_id = 1; return c; }
Message report(int id, RobotState state, MissionPhase phase, bool coupled = false) {
    Message m; m.type = MessageType::RobotSnapshot; m.robot_id = id; m.generation = 1; m.sequence = 2; m.mission_id = 1;
    m.state = state; m.phase = phase; m.coupled = coupled; m.position = {1,1}; return m;
}
}
TEST(LogisticsTest, WaitsForCompleteTeamAndAllArrivalReports) {
    auto map = layout(); FleetStateStore fleet; registerRobot(fleet,1); std::vector<Message> sent;
    LogisticsOrganizer organizer(map,fleet,{load(2)},[&](int,Message m) { sent.push_back(m); return true; });
    organizer.processOnce(); EXPECT_TRUE(sent.empty()); EXPECT_EQ(organizer.cargos()[0].state,CargoState::PENDING);
    registerRobot(fleet,2); organizer.processOnce(); ASSERT_EQ(sent.size(),2u); EXPECT_EQ(sent[0].type,MessageType::AssignMission);
    organizer.onMessage(report(1,RobotState::WAITING_TO_LOAD,MissionPhase::PICKUP_READY)); organizer.processOnce(); EXPECT_EQ(sent.size(),2u);
    organizer.onMessage(report(2,RobotState::WAITING_TO_LOAD,MissionPhase::PICKUP_READY)); organizer.processOnce(); ASSERT_EQ(sent.size(),4u); EXPECT_EQ(sent.back().type,MessageType::Couple);
    organizer.onMessage(report(1,RobotState::WAITING_TO_LOAD,MissionPhase::COUPLING,true)); organizer.processOnce(); EXPECT_EQ(sent.size(),4u);
    organizer.onMessage(report(2,RobotState::WAITING_TO_LOAD,MissionPhase::COUPLING,true)); organizer.processOnce(); ASSERT_EQ(sent.size(),6u); EXPECT_EQ(sent.back().phase,MissionPhase::TO_DELIVERY);
    for (int id : {1,2}) organizer.onMessage(report(id,RobotState::WAITING_TO_UNLOAD,MissionPhase::DELIVERY_READY,true)); organizer.processOnce(); ASSERT_EQ(sent.size(),8u);
    organizer.onMessage(report(1,RobotState::WAITING_TO_UNLOAD,MissionPhase::UNCOUPLING,false)); organizer.processOnce(); EXPECT_NE(organizer.cargos()[0].state,CargoState::DELIVERED);
    organizer.onMessage(report(2,RobotState::WAITING_TO_UNLOAD,MissionPhase::UNCOUPLING,false)); organizer.processOnce(); EXPECT_EQ(organizer.cargos()[0].state,CargoState::DELIVERED);
}
TEST(LogisticsTest, CancelsBeforeCouplingAndPreservesQueue) {
    auto map = layout(); FleetStateStore fleet; registerRobot(fleet,1); registerRobot(fleet,2);
    LogisticsOrganizer organizer(map,fleet,{load(2)},[](int,Message) { return true; }); organizer.processOnce();
    auto failure = report(1,RobotState::ERROR,MissionPhase::TO_PICKUP); failure.failure = true;
    auto s = snapshotFromMessage(failure); s.sequence = 3; fleet.update(s); organizer.onMessage(failure); organizer.processOnce();
    EXPECT_EQ(organizer.cargos()[0].state,CargoState::PENDING); EXPECT_EQ(organizer.missions()[0].phase,MissionPhase::NONE);
}
TEST(LogisticsTest, SuspendsPartiallyCoupledTeamInsteadOfReassigning) {
    auto map = layout(); FleetStateStore fleet; registerRobot(fleet,1); registerRobot(fleet,2);
    LogisticsOrganizer organizer(map,fleet,{load(2)},[](int,Message) { return true; }); organizer.processOnce();
    for (int id : {1,2}) organizer.onMessage(report(id,RobotState::WAITING_TO_LOAD,MissionPhase::PICKUP_READY)); organizer.processOnce();
    organizer.onMessage(report(1,RobotState::WAITING_TO_LOAD,MissionPhase::COUPLING,true));
    auto failure = report(2,RobotState::ERROR,MissionPhase::COUPLING); failure.failure = true; organizer.onMessage(failure); organizer.processOnce();
    EXPECT_EQ(organizer.cargos()[0].state,CargoState::SUSPENDED); EXPECT_EQ(organizer.missions().size(),1u);
    organizer.requestResume(1); organizer.processOnce(); EXPECT_EQ(organizer.cargos()[0].state,CargoState::SUSPENDED);
}
TEST(LogisticsTest, ResumeRequiresCurrentSensorsAndIgnoresObsoleteFaults) {
    auto map = layout(); FleetStateStore fleet; registerRobot(fleet,1);
    std::vector<LogisticsNotice::Kind> notices;
    LogisticsOrganizer organizer(map,fleet,{load(1)},[](int,Message) { return true; },[&](const LogisticsNotice& n) { notices.push_back(n.kind); });
    organizer.processOnce();
    organizer.onMessage(report(1,RobotState::WAITING_TO_LOAD,MissionPhase::PICKUP_READY)); organizer.processOnce();
    organizer.onMessage(report(1,RobotState::WAITING_TO_LOAD,MissionPhase::COUPLING,true)); organizer.processOnce();
    auto failure = report(1,RobotState::ERROR,MissionPhase::TO_DELIVERY,true);
    failure.type = MessageType::SensorUpdate; failure.sensor_sequence = 1; failure.revision = 3; failure.failure = true;
    organizer.onMessage(failure); organizer.processOnce(); ASSERT_EQ(organizer.cargos()[0].state,CargoState::SUSPENDED);
    auto recovered = failure; recovered.failure = false; recovered.sensor_sequence = 2; recovered.revision = 4;
    organizer.onMessage(recovered); organizer.processOnce();
    auto old = snapshotFromMessage(failure); old.failure = false; old.state = RobotState::SUSPENDED; old.sequence = 3;
    ASSERT_TRUE(fleet.update(old)); organizer.requestResume(1); organizer.processOnce();
    EXPECT_EQ(organizer.cargos()[0].state,CargoState::SUSPENDED);
    auto current = snapshotFromMessage(recovered); current.state = RobotState::SUSPENDED; current.sequence = 4;
    ASSERT_TRUE(fleet.update(current)); organizer.requestResume(1); organizer.processOnce();
    ASSERT_EQ(organizer.cargos()[0].state,CargoState::TRANSPORTING);
    organizer.onMessage(failure); organizer.processOnce();
    EXPECT_EQ(organizer.cargos()[0].state,CargoState::TRANSPORTING);
    EXPECT_EQ(std::count(notices.begin(),notices.end(),LogisticsNotice::Kind::Suspended),1);
}
