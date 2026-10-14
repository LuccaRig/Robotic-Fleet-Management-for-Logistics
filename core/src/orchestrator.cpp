#include "orchestrator.hpp"
#include <iostream>


// TODO: Tem que adicionar a comunicação entre simulador e Orquestrador via MQTT 

Orchestrator::Orchestrator() {
    std::cout << "[Orchestrator] Inicializado para gerenciamento de processos.\n";
}

Orchestrator::~Orchestrator() {
    std::lock_guard<std::mutex> lock(orchestrator_mutex);
    robots.clear();
    std::cout << "[Orchestrator] Encerrado. Todos os processos de robôs foram finalizados.\n";
}

bool Orchestrator::spawnRobot(int id, int start_x, int start_y) {
    std::lock_guard<std::mutex> lock(orchestrator_mutex);

    if (robots.find(id) != robots.end()) {
        std::cout << "[Orchestrator] Erro: Já existe um processo de robô rodando com o ID " << id << ".\n";
        return false;
    }

    auto new_robot = std::make_unique<RobotAgent>(id, start_x, start_y);
    new_robot->start();

    robots[id] = std::move(new_robot);

    std::cout << "[Orchestrator] Sucesso: Processo do Robô " << id << " spawnado na posição (" << start_x << ", " << start_y << ").\n";
    return true;
}

bool Orchestrator::terminateRobot(int id) {
    std::lock_guard<std::mutex> lock(orchestrator_mutex);

    auto it = robots.find(id);
    if (it == robots.end()) {
        std::cout << "[Orchestrator] Erro: Robô " << id << " não encontrado para remoção.\n";
        return false;
    }

    std::cout << "[Orchestrator] Terminando processo do Robô " << id << "...\n";
    
    robots.erase(it);

    std::cout << "[Orchestrator] Sucesso: Processo do Robô " << id << " encerrado e removido.\n";
    return true;
}

void Orchestrator::printAllRobotsStatus() const {
    std::lock_guard<std::mutex> lock(orchestrator_mutex);

    std::cout << "\n=== PROCESSOS DE ROBÔS ATIVOS (ORQUESTRADOR) ===\n";
    if (robots.empty()) {
        std::cout << "Nenhum processo de robô ativo no momento.\n";
    } else {
        for (const auto& [id, robot] : robots) {
            std::cout << "Robô ID: " << id 
                      << " | Posição: (" << robot->getX() << ", " << robot->getY() << ")"
                      << " | Bateria: " << robot->getBateria() << "%"
                      << " | Falha: " << (robot->isFalha() ? "SIM" : "NÃO") << "\n";
        }
    }
    std::cout << "================================================\n";
}

RobotAgent* Orchestrator::getRobot(int id) {
    std::lock_guard<std::mutex> lock(orchestrator_mutex);
    auto it = robots.find(id);
    if (it != robots.end()) {
        return it->second.get();
    }
    return nullptr;
}

size_t Orchestrator::getActiveRobotCount() const {
    std::lock_guard<std::mutex> lock(orchestrator_mutex);
    return robots.size();
}

bool Orchestrator::robotExists(int id) const {
    std::lock_guard<std::mutex> lock(orchestrator_mutex);
    return robots.find(id) != robots.end();
}