#include <gtest/gtest.h>
#include "ipc_channel.hpp"
#include "fleet_state.hpp"
#include <sys/socket.h>
#include <unistd.h>

TEST(ProtocolTest, RoundTripsAllMissionMetadata) {
    Message m; m.type = MessageType::AssignMission; m.robot_id = 7; m.generation = 9; m.command_id = 12; m.revision = 12;
    m.mission_id = 3; m.cargo_id = 4; m.route = {{2,3},{3,3}}; m.members = {1,2,3,4}; m.text = "Missão de coleta";
    const auto copy = decodeMessage(encodeMessage(m)); EXPECT_EQ(copy.robot_id,7); EXPECT_EQ(copy.generation,9u); EXPECT_EQ(copy.revision,12u);
    EXPECT_EQ(copy.route,m.route); EXPECT_EQ(copy.members,m.members); EXPECT_EQ(copy.text,m.text);
}
TEST(ProtocolTest, RejectsTruncationVersionAndOversize) {
    auto payload = encodeMessage(Message{}); payload.pop_back(); EXPECT_THROW(decodeMessage(payload),std::runtime_error);
    payload = encodeMessage(Message{}); payload[7] = 2; EXPECT_THROW(decodeMessage(payload),std::runtime_error);
    Message huge; huge.text.assign(MAX_MESSAGE_BYTES,'x'); EXPECT_THROW(encodeMessage(huge),std::runtime_error);
}
TEST(IpcTest, ReassemblesFragmentedFramesAndDetectsEof) {
    int sockets[2]; ASSERT_EQ(socketpair(AF_UNIX,SOCK_STREAM,0,sockets),0); IpcChannel receiver(sockets[0]);
    Message message; message.text = "Fragmentada"; auto bytes = encodeMessage(message); const auto size = static_cast<std::uint32_t>(bytes.size());
    std::vector<std::uint8_t> frame{std::uint8_t(size>>24),std::uint8_t(size>>16),std::uint8_t(size>>8),std::uint8_t(size)};
    frame.insert(frame.end(),bytes.begin(),bytes.end());
    ASSERT_EQ(write(sockets[1],frame.data(),2),2); EXPECT_FALSE(receiver.receive(std::chrono::milliseconds(1)));
    ASSERT_EQ(write(sockets[1],frame.data()+2,frame.size()-2),static_cast<ssize_t>(frame.size()-2));
    const auto decoded = receiver.receive(std::chrono::milliseconds(50)); ASSERT_TRUE(decoded); EXPECT_EQ(decoded->text,message.text);
    close(sockets[1]); EXPECT_THROW(receiver.receive(std::chrono::milliseconds(50)),std::runtime_error);
}
TEST(IpcTest, RejectsOversizedHeader) {
    int sockets[2]; ASSERT_EQ(socketpair(AF_UNIX,SOCK_STREAM,0,sockets),0); IpcChannel receiver(sockets[0]);
    const unsigned char header[]{0,1,0,1}; ASSERT_EQ(write(sockets[1],header,4),4);
    EXPECT_THROW(receiver.receive(std::chrono::milliseconds(50)),std::runtime_error); close(sockets[1]);
}
TEST(FleetStateTest, RequiresRegisteredGenerationAndIncreasingSequence) {
    FleetStateStore fleet; ASSERT_TRUE(fleet.registerRobot(1,3,{0,0}));
    RobotSnapshot s; s.id = 1; s.generation = 3; s.sequence = 1; s.connected = true; s.available = true;
    EXPECT_TRUE(fleet.update(s)); EXPECT_FALSE(fleet.update(s)); s.generation = 2; s.sequence = 99; EXPECT_FALSE(fleet.update(s));
    EXPECT_FALSE(fleet.disconnect(1,2)); EXPECT_TRUE(fleet.disconnect(1,3)); EXPECT_FALSE(fleet.get(1)->available);
}
TEST(FleetStateTest, RejectsStateRegressionEvenWithHigherTransportSequence) {
    FleetStateStore fleet; fleet.registerRobot(1,1,{0,0});
    RobotSnapshot s; s.id = 1; s.generation = 1; s.sequence = 1; s.revision = 5; s.sensor_sequence = 4; s.connected = true;
    ASSERT_TRUE(fleet.update(s)); s.sequence = 2; s.revision = 4; EXPECT_FALSE(fleet.update(s));
    s.revision = 5; s.sensor_sequence = 3; EXPECT_FALSE(fleet.update(s));
    fleet.disconnect(1,1); s.sensor_sequence = 5; EXPECT_FALSE(fleet.update(s));
}
TEST(IpcTest, ZeroTimeoutConsumesQueuedFrameBeforePeerExit) {
    int sockets[2]; ASSERT_EQ(socketpair(AF_UNIX,SOCK_STREAM,0,sockets),0);
    IpcChannel receiver(sockets[0]);
    {
        IpcChannel sender(sockets[1]); Message message; message.type = MessageType::RobotStopped; sender.send(message);
    }
    const auto message = receiver.receive(std::chrono::milliseconds(0));
    ASSERT_TRUE(message); EXPECT_EQ(message->type,MessageType::RobotStopped);
    EXPECT_THROW(receiver.receive(std::chrono::milliseconds(0)),std::runtime_error);
}
