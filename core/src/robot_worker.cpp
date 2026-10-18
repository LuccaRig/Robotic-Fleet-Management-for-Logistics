#include "orchestrator.hpp"
#include "ipc_channel.hpp"
#include "robot_agent.hpp"
#include <iostream>
#include <mutex>

int runRobotWorker(int fd, int id, std::uint64_t generation, Pos start) {
    try {
        IpcChannel channel(fd); std::mutex send_mutex; std::uint64_t sequence = 0; RobotAgent* active = nullptr;
        auto send = [&](Message m) {
            std::lock_guard<std::mutex> lock(send_mutex);
            if (active && m.type == MessageType::RobotSnapshot) m = messageFromSnapshot(active->snapshot());
            m.robot_id = id; m.generation = generation; m.sequence = ++sequence; m.occurred_at_ms = epochMilliseconds(); channel.send(m);
        };
        Message hello; hello.type = MessageType::RobotHello; hello.position = start; send(hello);
        RobotAgent robot(id, start.x, start.y, generation, send); active = &robot;
        Message ready = messageFromSnapshot(robot.snapshot()); ready.type = MessageType::RobotReady; send(ready);
        robot.start();
        while (true) {
            robot.checkError();
            const auto m = channel.receive(std::chrono::milliseconds(100));
            if (!m) continue;
            if (m->robot_id != id || m->generation != generation) throw std::runtime_error("Identidade do comando inválida");
            robot.handle(*m);
            if (m->type == MessageType::Shutdown) break;
        }
        robot.stop(); robot.statusTick();
        Message stopped = messageFromSnapshot(robot.snapshot()); stopped.type = MessageType::RobotStopped; stopped.state = RobotState::STOPPED; send(stopped); return 0;
    } catch (const std::exception& e) { std::cerr << "Processo do robô: " << e.what() << '\n'; return 1; }
}
