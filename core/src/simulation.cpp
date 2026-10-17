#include "simulation.hpp"
#include <iostream>
#include <chrono>
#include <algorithm>

Simulation::Simulation(WarehouseGrid initial_warehouse)
    : warehouse(std::move(initial_warehouse)), running(false) {}

Simulation::~Simulation() {
    stop();
}

void Simulation::start() {
    if (running) return;
    running = true;

    simulation_thread = std::thread(&Simulation::simulationLoop, this);
}

void Simulation::stop() {
    if (!running) return;
    running = false;

    if (simulation_thread.joinable()) {
        simulation_thread.join();
    }
}

void Simulation::addPickupPoint(int x, int y) {
    warehouse.setCell(x, y, CellState::PICKUP_POINT);
    std::cout << "[Simulation] Ponto de retirada de carga adicionado em (" << x << ", " << y << ").\n";
}

void Simulation::triggerRobotFailure(int robot_id) {
    
    std::string topico = "topico";
    std::string payload = "payload";

    publicarComandoMQTT(robot_id, topico, payload);
}

void Simulation::atualizarBateriaRoboPorMovimento(int robot_id, int novo_nivel_bateria) {
    
    std::string topico = "topico";
    std::string payload = "payload";
    
    publicarComandoMQTT(robot_id, topico, payload);
}

void Simulation::onRobotTelemetryReceived(int robot_id, int x, int y, int bateria, bool falha, const std::string& estado) {
    // OBS: Se a lógica da movimentação for cargo da simulação, então o robô não precisa enviar a posição dele, apenas o setpoint. 
    //A simulação vai mover o robô e mandar a posição que o robô está via mqtt.

    auto agora = std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::system_clock::now().time_since_epoch()).count();

    robos_telemetria[robot_id] = {
        robot_id, x, y, bateria, falha, estado, agora
    };

    warehouse.setCell(x, y, CellState::ROBOT);
}

void Simulation::simulationLoop() {
    while (running) {
        {
    
            processarLogicaFisica();
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

void Simulation::processarLogicaFisica() {
    // TODO: Aqui vai fazer a interação do robô com o ambiente
    // OBS: A movimentação do robô pode ser feito aqui enquanto o robô só envia o setpoint dele para a simulação, talvez fique mais simples
}

void Simulation::publicarComandoMQTT(int robot_id, const std::string& topico, const std::string& payload) {
    // TODO: Ponto de integração futuro com o cliente MQTT
}

void Simulation::printSimulationState() const {
    
    std::cout << "\n=== ESTADO DA SIMULAÇÃO (WAREHOUSE GRID) ===\n";
    warehouse.printGrid();

    std::cout << "\n--- STATUS DOS ROBÔS (VIA TELEMETRIA MQTT) ---\n";
    if (robos_telemetria.empty()) {
        std::cout << "Nenhum robô reportando telemetria.\n";
    } else {
        for (const auto& [id, robo] : robos_telemetria) {
            std::cout << "Robô [" << id 
                      << "] | Posição: (" << robo.x << ", " << robo.y << ")"
                      << " | Bateria: " << robo.bateria << "%" 
                      << " | Estado: " << robo.estado
                      << " | Falha: " << (robo.falha ? "SIM" : "NÃO") << "\n";
        }
    }
    std::cout << "===============================================\n";
}