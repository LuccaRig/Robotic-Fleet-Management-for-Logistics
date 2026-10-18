// Programa independente para gerar um banco SQLite de inspecao persistente.
// Execute a partir da raiz do repositorio. Nao apaga o banco entre execucoes.
#include "buffer.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>

int main() {
    try {
        const std::filesystem::path caminho = "logs/test_events_inspecao.db";
        std::filesystem::create_directories(caminho.parent_path());

        DataCollector collector(caminho.string());
        collector.start();
        Buffer buffer(collector, 3);

        buffer.addEvent(RobotEvent(1, -1, -1, {0, 0}, EventType::ROBOT_CREATED,
                                   "Robo 1 inicializado", ""));
        buffer.addEvent(RobotEvent(1, 7, 2, {3, 4}, EventType::ROBOT_MOVING,
                                   "Robo 1 em deslocamento", "Baia 2"));
        buffer.addEvent(RobotEvent(2, 7, 2, {8, 4}, EventType::CARGO_PICKUP,
                                   "Carga coletada", "Estoque"));
        buffer.addEvent(RobotEvent(1, 7, 2, {10, 5}, EventType::CARGO_DELIVERED,
                                   "Carga entregue", "Expedicao"));

        // Garante que todos os eventos aceitos foram processados antes de sair.
        collector.stop();

        std::cout << "Banco preservado: " << std::filesystem::absolute(caminho) << '\n';
        std::cout << "4 eventos enviados; buffer conserva " << buffer.size()
                  << " eventos recentes (capacidade " << buffer.capacity() << ").\n";
        std::cout << "Novas execucoes acrescentam registros ao mesmo banco.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Falha ao gerar banco de inspecao: " << e.what() << '\n';
        return 1;
    }
}
