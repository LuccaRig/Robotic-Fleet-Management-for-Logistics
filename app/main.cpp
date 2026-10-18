#include <iostream>
#include <thread>
#include <chrono>
#include "mqtt_client.hpp"
#include "warehouse.hpp"
#include "orchestrator.hpp"

int main(int argc, char** argv) {
    if (argc == 7 && std::string(argv[1]) == "--robot-worker") {
        return runRobotWorker(std::stoi(argv[2]), std::stoi(argv[3]), std::stoull(argv[4]), {std::stoi(argv[5]), std::stoi(argv[6])});
    }
    std::cout << "[INFO] Iniciando Simulacao Logistica Multirobos..." << std::endl;

    // 1. Inicializa o cliente MQTT 

    // 2. Inicializa o Galpão e o Orquestrador

    std::vector<std::vector<int>> mapa_inicial = {
        {5, 5, 5, 5, 5},
        {5, 2, 0, 3, 5}, // 2 = Charging Station, 0 = Empty, 3 = Narrow Tunnel
        {5, 0, 0, 0, 5}, // 1 = Robot
        {5, 4, 0, 4, 5}, // 4 = Pickup Point
        {5, 5, 5, 5, 5}
    };

    WarehouseGrid warehouse(mapa_inicial);

    warehouse.printGrid();

    std::cout << "[INFO] Sistema pronto. Iniciando loop principal." << std::endl;

    // 3. Loop principal da simulação

    std::cout << "[INFO] Simulacao encerrada." << std::endl;
    
    return 0;
}