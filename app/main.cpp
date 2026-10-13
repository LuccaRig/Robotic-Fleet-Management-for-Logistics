#include <iostream>
#include <thread>
#include <chrono>
#include "mqtt_client.hpp"
#include "warehouse.hpp"
#include "orchestrator.hpp"

int main() {
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