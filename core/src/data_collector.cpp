
#include "data_collector.hpp"

#include <iostream>
#include <fstream>
#include <chrono>
#include <ctime>
#include <sstream>

DataCollector::DataCollector(const std::string& file_path)
    : log_file_path(file_path) {
}

DataCollector::~DataCollector() {
    stop();
}

void DataCollector::start() {
    // TODO:
    // Verificar se o coletor ja esta em execucao.
    // Atualizar running.
    // Iniciar collector_thread.
}

void DataCollector::stop() {
    // TODO:
    // Solicitar encerramento da thread.
    // Notificar queue_cv.
    // Aguardar o processamento dos eventos pendentes.
    // Executar join() na thread.
}

void DataCollector::enqueueEvent(const RobotEvent& event) {
    // TODO:
    // Bloquear queue_mutex.
    // Inserir event na event_queue.
    // Liberar o mutex.
    // Notificar queue_cv.
}

void DataCollector::collectorLoop() {
    // TODO:
    // Enquanto running ou houver eventos pendentes:
    //
    // 1. Aguardar novos eventos com queue_cv.
    // 2. Retirar um evento da fila.
    // 3. Liberar o mutex.
    // 4. Gravar o evento em disco.
}

void DataCollector::writeEventToDisk(
    const RobotEvent& event
) {
    // TODO:
    // Abrir log_file_path com std::ofstream.
    // Converter EventType para texto.
    // Registrar timestamp, robot_id e descricao.
    // Incluir dados de carga, quando existentes.
    // Verificar erros de escrita.
}

std::string DataCollector::getTimestamp() const {
    // TODO:
    // Obter horario atual.
    // Converter para uma string com data e hora.

    return "";
}

void DataCollector::recordRobotEvent(
    int robot_id,
    EventType type,
    const std::string& description
) {
    RobotEvent event;

    event.robot_id = robot_id;
    event.type = type;
    event.description = description;
    event.timestamp = getTimestamp();

    enqueueEvent(event);
}

void DataCollector::recordCargoDelivery(
    int bay_id,
    const std::string& destination,
    const std::vector<int>& robot_ids
) {
    RobotEvent event;

    event.robot_id = -1;
    event.type = EventType::CARGO_DELIVERED;
    event.description = "Carga entregue";
    event.timestamp = getTimestamp();

    event.bay_id = bay_id;
    event.destination = destination;
    event.team_robot_ids = robot_ids;

    enqueueEvent(event);
}
