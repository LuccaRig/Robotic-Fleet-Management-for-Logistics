#include <iostream>
#include <thread>
#include <chrono>
#include "mqtt_client.hpp"
#include "warehouse.hpp"
#include "orchestrator.hpp"

int main() {
    std::cout << "[INFO] Iniciando Simulacao Logistica Multirobos..." << std::endl;

    // 1. Inicializa o cliente MQTT (Mosquitto)

    // 2. Inicializa o Galpão e o Orquestrador

    std::cout << "[INFO] Sistema pronto. Iniciando loop principal." << std::endl;

    // 3. Loop principal da simulação

    std::cout << "[INFO] Simulacao encerrada." << std::endl;
    
    return 0;
}