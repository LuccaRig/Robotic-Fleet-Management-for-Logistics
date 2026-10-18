#include "orchestrator.hpp"
#include "ipc_channel.hpp"
#include "robot_agent.hpp"
#include <iostream>

int runRobotWorker(int fd, int id, std::uint64_t generation, Pos start) {
    try {
        IpcChannel channel(fd);
        std::uint64_t sequence = 0;
        auto send = [&](Message m) { m.robot_id = id; m.generation = generation; m.sequence = ++sequence; m.occurred_at_ms = epochMilliseconds(); channel.send(m); };
        Message hello; hello.type = MessageType::RobotHello; hello.position = start; send(hello);
        RobotAgent robot(id, start.x, start.y); robot.start();
        Message ready; ready.type = MessageType::RobotReady; ready.position = start; send(ready);
        while (true) {
            auto m = channel.receive(std::chrono::milliseconds(500));
            if (!m) { Message status; status.type = MessageType::RobotSnapshot; status.position = start; send(status); continue; }
            if (m->type == MessageType::Shutdown) {
                Message ack; ack.type = MessageType::CommandAck; ack.command_id = m->command_id; send(ack); break;
            }
        }
        robot.stop(); Message stopped; stopped.type = MessageType::RobotStopped; send(stopped); return 0;
    } catch (const std::exception& e) { std::cerr << "Processo do robô: " << e.what() << '\n'; return 1; }
}
