#ifndef SIMULATION_HPP
#define SIMULATION_HPP

#include "warehouse.hpp"
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <string>
#include <functional>
#include <map>

class Simulation {
private:
    WarehouseGrid warehouse;
    std::thread simulation_thread;
    std::atomic<bool> running;

    struct RobotTelemetry {
        int id;
        int x;
        int y;
        int bateria;
        bool falha;
        std::string estado;
        long long ultima_atualizacao_ms;
    };

    std::map<int, RobotTelemetry> robos_telemetria;

    void simulationLoop();
    void processarLogicaFisica();
    void publicarComandoMQTT(int robot_id, const std::string& topico, const std::string& payload);

public:
    explicit Simulation(WarehouseGrid initial_warehouse);
    ~Simulation();
    void start();
    void stop();


    void addPickupPoint(int x, int y);
    void triggerRobotFailure(int robot_id);
    void atualizarBateriaRoboPorMovimento(int robot_id, int novo_nivel_bateria);
    void onRobotTelemetryReceived(int robot_id, int x, int y, int bateria, bool falha, const std::string& estado);
    void printSimulationState() const;
};

#endif // SIMULATION_HPP