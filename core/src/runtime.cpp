#include "runtime.hpp"
#include <csignal>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <unistd.h>

ControlRuntime::ControlRuntime(std::string executable, std::string database, Scenario scenario)
    : warehouse_(std::move(scenario.warehouse)), initial_(std::move(scenario.robots)), collector_(database), buffer_(collector_, 100),
      recorder_(buffer_, [this](const RobotEvent& event) { { std::lock_guard<std::mutex> lock(metrics_mutex_); ++metrics_[event.getType()]; } print("Evento: " + event.getDescription() + " | robô " + std::to_string(event.getRobotId()) + " | missão " + std::to_string(event.missionId())); }),
      manager_(std::move(executable)),
      passages_(warehouse_, [this](Message m) { manager_.send(m.robot_id, std::move(m)); }, [this](const PassageNotice& n) { recorder_.onPassage(n); }),
      hardware_(warehouse_, [this](Message m) { onSensor(std::move(m)); }),
      logistics_(warehouse_, manager_.fleet(), std::move(scenario.cargos), [this](int id, Message m) { return sendCommand(id, std::move(m)); }, [this](const LogisticsNotice& n) { recorder_.onLogistics(n); }) {
    warehouse_.validate();
    manager_.setPositionValidator([this](Pos p) { return warehouse_.traversable(p) && !warehouse_.passageAt(p); });
    hardware_.setMovementValidator([this](const Message& m) { return passages_.permits(m); });
    manager_.setMessageHandler([this](const Message& m) { onMessage(m); });
}
ControlRuntime::~ControlRuntime() { try { stop(); } catch (const std::exception& e) { print("Falha no encerramento: " + std::string(e.what())); } }
void ControlRuntime::start() {
    if (started_) return;
    collector_.start(); started_ = true; logistics_.start();
    for (const auto& robot : initial_) if (!addRobot(robot.id, robot.position)) throw std::runtime_error("Não foi possível disponibilizar o robô " + std::to_string(robot.id));
    print("Sistema iniciado. Execução " + std::to_string(collector_.runId()));
}
void ControlRuntime::stop() {
    if (stopped_) return;
    stopped_ = true;
    std::exception_ptr error;
    logistics_.stop();
    try { manager_.stop(); } catch (...) { error = std::current_exception(); }
    manager_.setMessageHandler({});
    try { collector_.stop(); } catch (...) { if (!error) error = std::current_exception(); }
    if (error) std::rethrow_exception(error);
}
bool ControlRuntime::sendCommand(int id, Message message) {
    const auto robot = manager_.fleet().get(id); if (!robot || !robot->connected) return false;
    message.robot_id = id; message.generation = robot->generation; message.command_id = ++commands_; message.revision = message.command_id;
    { std::lock_guard<std::mutex> lock(pending_mutex_); pending_[message.command_id] = message; }
    try {
        if (manager_.send(id, message)) return true;
    } catch (...) { std::lock_guard<std::mutex> lock(pending_mutex_); pending_.erase(message.command_id); throw; }
    std::lock_guard<std::mutex> lock(pending_mutex_); pending_.erase(message.command_id); return false;
}
void ControlRuntime::onSensor(Message sensor) {
    passages_.confirm(sensor);
    logistics_.onMessage(sensor);
    manager_.send(sensor.robot_id, std::move(sensor));
}
void ControlRuntime::onMessage(const Message& m) {
    if (m.type == MessageType::RobotReady) {
        hardware_.registerRobot(snapshotFromMessage(m)); passages_.confirm(m);
        print("Robô " + std::to_string(m.robot_id) + " pronto, geração " + std::to_string(m.generation));
    }
    if (m.type == MessageType::CommandAck) {
        std::optional<Message> original;
        {
            std::lock_guard<std::mutex> lock(pending_mutex_); auto it = pending_.find(m.command_id);
            if (it != pending_.end()) {
                if (it->second.robot_id == m.robot_id && it->second.generation == m.generation) original = it->second;
                pending_.erase(it);
            }
        }
        if (original && m.accepted) {
            hardware_.commandAccepted(*original);
            if (original->type == MessageType::CancelMission || original->type == MessageType::PauseMission) passages_.cancel(m.robot_id, original->mission_id);
        }
    } else if (m.type == MessageType::PassageRequest) {
        const auto reading = hardware_.reading(m.robot_id);
        if (reading && reading->generation == m.generation && reading->mission_id == m.mission_id && reading->revision == m.revision && !reading->failure && reading->position == m.position) passages_.request(m);
    } else if (m.type == MessageType::NavigationSetpoint) hardware_.setpoint(m);
    else if (m.type == MessageType::RobotSnapshot) hardware_.observe(snapshotFromMessage(m));
    else if (m.type == MessageType::RobotDisconnected) {
        const auto reading = hardware_.reading(m.robot_id); passages_.cancel(m.robot_id, reading ? reading->mission_id : 0);
        std::lock_guard<std::mutex> lock(pending_mutex_);
        for (auto it = pending_.begin(); it != pending_.end();) { if (it->second.robot_id == m.robot_id) it = pending_.erase(it); else ++it; }
    }
    recorder_.onRobot(m); logistics_.onMessage(m);
}
void ControlRuntime::tick() {
    manager_.checkError(); logistics_.checkError(); collector_.checkError(); hardware_.tick();
    for (auto it = removals_.begin(); it != removals_.end();) {
        if (manager_.robotExists(*it)) { ++it; continue; }
        if (!hardware_.removeRobot(*it)) throw std::runtime_error("Remoção física recusada");
        passages_.removeRobot(*it); it = removals_.erase(it);
    }
}
bool ControlRuntime::addRobot(int id, Pos position) { return !hardware_.contains(id) && manager_.spawnRobot(id, position.x, position.y); }
bool ControlRuntime::removeRobot(int id) {
    const auto robot = manager_.fleet().get(id);
    if (!robot || robot->coupled || robot->mission_id || removals_.count(id)) return false;
    removals_.insert(id); if (manager_.robotExists(id)) manager_.terminateRobot(id); return true;
}
bool ControlRuntime::interruptRobot(int id) { for (const auto& p : manager_.processes()) if (p.id == id) return kill(p.pid, SIGTERM) == 0; return false; }
bool ControlRuntime::setBattery(int id, int battery) { return hardware_.inject(id, battery, std::nullopt); }
bool ControlRuntime::setFailure(int id, bool failure) { return hardware_.inject(id, std::nullopt, failure); }
void ControlRuntime::resume(EntityId mission_id) { logistics_.requestResume(mission_id); }
bool ControlRuntime::complete() const {
    if (!logistics_.allDelivered()) return false;
    for (const auto& robot : manager_.fleet().snapshot()) if (robot.connected && (robot.mission_id || robot.coupled || robot.state != RobotState::IDLE)) return false;
    return true;
}
std::size_t ControlRuntime::eventCount(EventType type) const { std::lock_guard<std::mutex> lock(metrics_mutex_); const auto it = metrics_.find(type); return it == metrics_.end() ? 0 : it->second; }
void ControlRuntime::print(const std::string& message) const { std::lock_guard<std::mutex> lock(console_); std::cout << message << std::endl; }
void ControlRuntime::command(const std::string& line) {
    std::istringstream input(line); std::string action; input >> action;
    if (action.empty()) return;
    auto exhausted = [&] { std::string extra; if (input >> extra) throw std::invalid_argument("Argumentos adicionais inesperados"); };
    auto integer = [&] { int value; if (!(input >> value)) throw std::invalid_argument("Informe um número inteiro válido"); return value; };
    if (action == "sair") { exhausted(); exit_requested_ = true; }
    else if (action == "status") {
        exhausted();
        for (const auto& s : fleet().snapshot()) print("Robô " + std::to_string(s.id) + " | " + robotStateName(s.state) + " | posição " + std::to_string(s.position.x) + "," + std::to_string(s.position.y) + " | bateria " + std::to_string(s.battery) + "% | distância " + std::to_string(s.distance) + " | " + (s.connected ? "Conectado" : "Desconectado"));
        manager_.printAllRobotsStatus();
    } else if (action == "cargas") {
        exhausted(); for (const auto& c : cargos()) print("Carga " + std::to_string(c.id) + " | " + cargoStateName(c.state) + " | missão " + std::to_string(c.mission_id) + " | " + c.reason);
    } else if (action == "passagens") {
        exhausted(); for (const auto& p : passages()) print("Passagem " + std::to_string(p.id) + " | ocupação " + std::to_string(p.occupied) + "/" + std::to_string(p.capacity) + " | esperando " + std::to_string(p.waiting));
    } else if (action == "robo") {
        std::string operation; input >> operation; const auto id = integer(); bool ok = false;
        if (operation == "adicionar") { const auto x = integer(); const auto y = integer(); exhausted(); ok = addRobot(id, {x, y}); }
        else if (operation == "remover") { exhausted(); ok = removeRobot(id); }
        else if (operation == "interromper") { exhausted(); ok = interruptRobot(id); }
        else throw std::invalid_argument("Operação de robô desconhecida");
        if (!ok) throw std::runtime_error("Operação recusada: verifique ID, posição e missão do robô");
    } else if (action == "bateria" || action == "falha") {
        const auto id = integer(); const auto value = integer(); exhausted();
        if (action == "falha" && value != 0 && value != 1) throw std::invalid_argument("Falha deve ser 0 ou 1");
        const bool ok = action == "bateria" ? setBattery(id, value) : setFailure(id, value != 0);
        if (!ok) throw std::runtime_error("Atualização recusada");
    } else if (action == "retomar") { const auto id = integer(); exhausted(); resume(id); }
    else if (action == "ajuda") { exhausted(); print("Comandos: status; cargas; passagens; robo adicionar ID X Y; robo remover ID; robo interromper ID; bateria ID 0..100; falha ID 0|1; retomar MISSÃO; sair"); }
    else throw std::invalid_argument("Comando desconhecido. Digite ajuda");
}
