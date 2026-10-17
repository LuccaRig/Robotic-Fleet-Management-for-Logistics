
#include "data_collector.hpp"

#include <iostream>
#include <fstream>
#include <chrono>
#include <sstream>
#include <ctime>
#include <iomanip>
#include <stdexcept>

// Construtor
DataCollector::DataCollector(const std::string& file_path)
    : log_file_path(file_path) {
}

// Destrutor
DataCollector::~DataCollector() {
    stop();
}


// Inicializacao
void DataCollector::start() {
    std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex);

    {
        std::lock_guard<std::mutex> queue_lock(queue_mutex);

        if (state != CollectorState::STOPPED) {
            return;
        }

        state = CollectorState::RUNNING;
    }

    try {
        collector_thread = std::thread(
            &DataCollector::collectorLoop, this
        );
    }
    catch (...) {
        std::lock_guard<std::mutex> queue_lock(queue_mutex);
        state = CollectorState::STOPPED;
        throw;
    }
}


// Encerramento
void DataCollector::stop() {
    std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex);

    {
        std::lock_guard<std::mutex> queue_lock(queue_mutex);

        if (state == CollectorState::STOPPED) {
            return;
        }

        state = CollectorState::STOPPING;
    }

    queue_cv.notify_all();

    if (collector_thread.joinable()) {
        collector_thread.join();
    }

    {
        std::lock_guard<std::mutex> queue_lock(queue_mutex);
        state = CollectorState::STOPPED;
    }
}


// Insercao de eventos
void DataCollector::enqueueEvent(const RobotEvent& event) {
    {
        std::lock_guard<std::mutex> lock(queue_mutex);

        if (state != CollectorState::RUNNING) {
            throw std::runtime_error(
                "DataCollector nao esta recebendo eventos"
            );
        }

        event_queue.push(event);
    }

    queue_cv.notify_one();
}


// Thread consumidora
void DataCollector::collectorLoop() {
    while (true) {
        std::unique_lock<std::mutex> lock(queue_mutex);

        queue_cv.wait(lock, [this] {
            return !event_queue.empty() ||
                   state != CollectorState::RUNNING;
        });

        if (event_queue.empty() &&
            state == CollectorState::STOPPING) {
            break;
        }

        RobotEvent event = event_queue.front();
        event_queue.pop();

        lock.unlock();

        writeEventToDisk(event);
    }
}


// Gravacao em disco
void DataCollector::writeEventToDisk(const RobotEvent& event) {
    std::ofstream log_file(log_file_path, std::ios::app);

    if (!log_file.is_open()) {
        std::cerr << "Erro ao abrir o arquivo de log: "
                  << log_file_path << std::endl;
        return;
    }

    log_file << event.timestamp
             << " - Robot ID: " << event.robot_id
             << " - Type: " << static_cast<int>(event.type)
             << " - Description: " << event.description;

    if (event.bay_id != -1) {
        log_file << " - Bay ID: " << event.bay_id;
    }

    if (!event.destination.empty()) {
        log_file << " - Destination: " << event.destination;
    }

    if (!event.team_robot_ids.empty()) {
        log_file << " - Team Robots: ";

        for (const auto& id : event.team_robot_ids) {
            log_file << id << " ";
        }
    }

    log_file << '\n';
    log_file.flush();

    if (!log_file) {
        std::cerr << "Erro ao gravar evento no arquivo: "
                  << log_file_path << std::endl;
    }
}


// Data e hora
std::string DataCollector::getTimestamp() const {
    auto now = std::chrono::system_clock::now();

    std::time_t now_time =
        std::chrono::system_clock::to_time_t(now);

    std::tm time_info{};

    localtime_r(&now_time, &time_info);

    std::ostringstream timestamp;

    timestamp << std::put_time(
        &time_info, "%Y-%m-%d %H:%M:%S"
    );

    return timestamp.str();
}


// Registro de evento de robo
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


// Registro de entrega de carga
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
