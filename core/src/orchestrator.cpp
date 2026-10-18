#include "orchestrator.hpp"
#include "ipc_channel.hpp"
#include <cerrno>
#include <condition_variable>
#include <csignal>
#include <deque>
#include <exception>
#include <fcntl.h>
#include <iostream>
#include <map>
#include <mutex>
#include <spawn.h>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

extern char** environ;

struct Orchestrator::Impl {
    struct Entry {
        RobotProcess process;
        std::shared_ptr<IpcChannel> channel;
        std::deque<Message> outgoing;
        std::map<std::uint64_t, std::chrono::steady_clock::time_point> pending;
        std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
        std::chrono::steady_clock::time_point seen = started;
        std::chrono::steady_clock::time_point stopping_at;
        int escalation = 0;
        std::uint64_t sent = 0;
        std::uint64_t received = 0;
        bool connected = true;
    };
    std::string executable;
    mutable std::mutex mutex;
    std::condition_variable cv;
    std::map<int, std::shared_ptr<Entry>> entries;
    Handler handler;
    std::function<bool(Pos)> validator;
    std::thread gateway;
    std::mutex lifecycle;
    std::uint64_t generation = 0;
    bool stopping = false;
    std::exception_ptr error;

    explicit Impl(std::string path) : executable(std::move(path)), gateway(&Impl::loop, this) {}
    void notify(const Message& message) {
        Handler callback;
        { std::lock_guard<std::mutex> lock(mutex); callback = handler; }
        if (callback) callback(message);
    }
    void disconnect(const std::shared_ptr<Entry>& entry, const std::string& reason) {
        Message message;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!entry->connected) return;
            entry->connected = false;
            entry->process.ready = false;
            entry->process.stopping = true;
            entry->stopping_at = std::chrono::steady_clock::now();
            entry->channel->interrupt();
            entry->outgoing.clear(); entry->pending.clear();
            message.type = MessageType::RobotDisconnected; message.robot_id = entry->process.id;
            message.generation = entry->process.generation; message.text = reason; message.occurred_at_ms = epochMilliseconds();
        }
        notify(message);
    }
    void loop() noexcept {
        try {
            while (true) {
                std::vector<std::shared_ptr<Entry>> current;
                {
                    std::unique_lock<std::mutex> lock(mutex);
                    if (stopping && entries.empty()) break;
                    for (const auto& pair : entries) current.push_back(pair.second);
                }
                for (const auto& entry : current) {
                    std::deque<Message> outgoing;
                    { std::lock_guard<std::mutex> lock(mutex); outgoing.swap(entry->outgoing); }
                    try {
                        for (const auto& message : outgoing) entry->channel->send(message);
                        while (entry->connected) {
                            const auto received = entry->channel->receive(std::chrono::milliseconds(0));
                            if (!received) break;
                            const auto& message = *received;
                            if (message.robot_id != entry->process.id || message.generation != entry->process.generation || message.sequence <= entry->received)
                                throw std::runtime_error("Identidade ou sequência IPC inválida");
                            entry->received = message.sequence;
                            {
                                std::lock_guard<std::mutex> lock(mutex);
                                entry->seen = std::chrono::steady_clock::now();
                                if (message.type == MessageType::RobotReady) entry->process.ready = true;
                                if (message.type == MessageType::CommandAck) entry->pending.erase(message.command_id);
                            }
                            notify(message);
                        }
                    } catch (const std::exception& e) { disconnect(entry, e.what()); }
                    bool expired = false;
                    {
                        std::lock_guard<std::mutex> lock(mutex);
                        const auto now = std::chrono::steady_clock::now();
                        expired = entry->connected && !entry->process.stopping &&
                            ((!entry->process.ready && now - entry->started > std::chrono::seconds(2)) ||
                             (entry->process.ready && now - entry->seen > std::chrono::seconds(3)));
                        for (const auto& pending : entry->pending) if (now > pending.second) expired = true;
                        if (entry->process.stopping) {
                            const auto elapsed = now - entry->stopping_at;
                            if (elapsed > std::chrono::seconds(3) && entry->escalation < 2) { kill(entry->process.pid, SIGKILL); entry->escalation = 2; }
                            else if (elapsed > std::chrono::seconds(2) && entry->escalation < 1) { kill(entry->process.pid, SIGTERM); entry->escalation = 1; }
                        }
                    }
                    if (expired) disconnect(entry, "Prazo de comunicação com o robô excedido");
                    int status = 0;
                    const auto pid = waitpid(entry->process.pid, &status, WNOHANG);
                    if (pid > 0 || (pid < 0 && errno == ECHILD)) {
                        disconnect(entry, "Processo do robô encerrado");
                        std::lock_guard<std::mutex> lock(mutex);
                        entries.erase(entry->process.id); cv.notify_all();
                    }
                }
                std::unique_lock<std::mutex> lock(mutex);
                cv.wait_for(lock, std::chrono::milliseconds(10));
            }
        } catch (...) {
            std::lock_guard<std::mutex> lock(mutex); error = std::current_exception();
            for (auto& pair : entries) { pair.second->channel->interrupt(); kill(pair.second->process.pid, SIGKILL); }
        }
    }
};

Orchestrator::Orchestrator(std::string executable) : impl_(std::make_unique<Impl>(std::move(executable))) {}
Orchestrator::~Orchestrator() { try { stop(); } catch (...) {} }
void Orchestrator::setMessageHandler(Handler handler) { std::lock_guard<std::mutex> lock(impl_->mutex); impl_->handler = std::move(handler); }
void Orchestrator::setPositionValidator(std::function<bool(Pos)> validator) { std::lock_guard<std::mutex> lock(impl_->mutex); impl_->validator = std::move(validator); }

bool Orchestrator::spawnRobot(int id, int x, int y) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (id < 0 || impl_->stopping || impl_->executable.empty() || impl_->entries.count(id) || (impl_->validator && !impl_->validator({x, y}))) return false;
    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) throw std::runtime_error("Não foi possível criar o canal do robô");
    if (fcntl(fds[0], F_SETFD, FD_CLOEXEC) < 0 || fcntl(fds[1], F_SETFD, FD_CLOEXEC) < 0) { close(fds[0]); close(fds[1]); throw std::runtime_error("Falha ao proteger descritores IPC"); }
    const auto generation = ++impl_->generation;
    std::vector<std::string> args{impl_->executable, "--robot-worker", "3", std::to_string(id), std::to_string(generation), std::to_string(x), std::to_string(y)};
    std::vector<char*> argv; for (auto& arg : args) argv.push_back(arg.data()); argv.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addclose(&actions, fds[0]);
    posix_spawn_file_actions_adddup2(&actions, fds[1], 3);
    if (fds[1] != 3) posix_spawn_file_actions_addclose(&actions, fds[1]);
    pid_t pid = -1;
    const int result = posix_spawn(&pid, impl_->executable.c_str(), &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions); close(fds[1]);
    if (result != 0) { close(fds[0]); return false; }
    auto entry = std::make_shared<Impl::Entry>();
    entry->process = {id, static_cast<int>(pid), generation, false, false};
    entry->channel = std::make_shared<IpcChannel>(fds[0]);
    impl_->entries.emplace(id, std::move(entry)); impl_->cv.notify_all(); return true;
}

bool Orchestrator::send(int id, Message message) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    const auto it = impl_->entries.find(id);
    if (it == impl_->entries.end() || !it->second->connected) return false;
    auto& entry = *it->second;
    if (!entry.process.ready && message.type != MessageType::Shutdown) return false;
    if (entry.outgoing.size() >= 1024) throw std::runtime_error("Fila de envio IPC esgotada");
    message.robot_id = id; message.generation = entry.process.generation; message.sequence = ++entry.sent;
    message.occurred_at_ms = epochMilliseconds();
    switch (message.type) {
    case MessageType::AssignMission: case MessageType::CancelMission: case MessageType::Couple:
    case MessageType::Uncouple: case MessageType::PauseMission: case MessageType::ResumeMission: case MessageType::Shutdown:
        message.command_id = message.sequence;
        entry.pending[message.command_id] = std::chrono::steady_clock::now() + std::chrono::seconds(3); break;
    default: break;
    }
    entry.outgoing.push_back(std::move(message)); impl_->cv.notify_all(); return true;
}

bool Orchestrator::terminateRobot(int id) {
    Message message; message.type = MessageType::Shutdown;
    const bool sent = send(id, message);
    std::lock_guard<std::mutex> lock(impl_->mutex);
    const auto it = impl_->entries.find(id);
    if (it == impl_->entries.end()) return false;
    if (!it->second->process.stopping) { it->second->process.stopping = true; it->second->stopping_at = std::chrono::steady_clock::now(); }
    impl_->cv.notify_all(); return sent;
}
std::vector<RobotProcess> Orchestrator::processes() const {
    std::lock_guard<std::mutex> lock(impl_->mutex); std::vector<RobotProcess> result;
    for (const auto& pair : impl_->entries) result.push_back(pair.second->process);
    return result;
}
std::size_t Orchestrator::getActiveRobotCount() const { return processes().size(); }
bool Orchestrator::robotExists(int id) const { for (const auto& p : processes()) if (p.id == id) return true; return false; }
void Orchestrator::printAllRobotsStatus() const { for (const auto& p : processes()) std::cout << "Robô " << p.id << " | PID " << p.pid << " | " << (p.ready ? "Pronto" : "Inicializando") << '\n'; }
void Orchestrator::checkError() const { std::lock_guard<std::mutex> lock(impl_->mutex); if (impl_->error) std::rethrow_exception(impl_->error); }
void Orchestrator::stop() {
    std::lock_guard<std::mutex> lifecycle(impl_->lifecycle);
    { std::lock_guard<std::mutex> lock(impl_->mutex); impl_->stopping = true; }
    for (const auto& p : processes()) terminateRobot(p.id);
    impl_->cv.notify_all();
    if (impl_->gateway.joinable()) impl_->gateway.join();
    for (const auto& p : processes()) { kill(p.pid, SIGKILL); while (waitpid(p.pid, nullptr, 0) < 0 && errno == EINTR) {} }
    { std::lock_guard<std::mutex> lock(impl_->mutex); impl_->entries.clear(); }
    checkError();
}
