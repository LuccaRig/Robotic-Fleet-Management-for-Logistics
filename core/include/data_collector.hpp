
#ifndef DATA_COLLECTOR_HPP
#define DATA_COLLECTOR_HPP

#include <string>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

// Tipos de eventos registrados pelo coletor.
enum class EventType {
    ROBOT_CREATED,
    ROBOT_MOVING,
    ROBOT_ARRIVED,
    LOW_BATTERY,
    ROBOT_CHARGING,
    ROBOT_FAILURE,
    CARGO_PICKUP,
    CARGO_DELIVERED
};

// Estrutura que representa um evento.
struct RobotEvent {
    int robot_id = -1;

    EventType type = EventType::ROBOT_CREATED;

    std::string description;
    std::string timestamp;

    // Informacoes opcionais de transporte.
    int bay_id = -1;
    std::string destination;
    std::vector<int> team_robot_ids;
};

class DataCollector {
private:
    // Fila de eventos recebidos.
    std::queue<RobotEvent> event_queue;

    // Sincronizacao da fila.
    std::mutex queue_mutex;
    std::condition_variable queue_cv;

    // Thread responsavel pela coleta.
    std::thread collector_thread;

    // Controle de execucao.
    std::atomic<bool> running{false};

    // Caminho do arquivo de registros.
    std::string log_file_path;

    // Processamento interno.
    void collectorLoop();

    // Persistencia.
    void writeEventToDisk(const RobotEvent& event);

    // Geracao de timestamp.
    std::string getTimestamp() const;

public:
    explicit DataCollector(const std::string& file_path);

    ~DataCollector();

    // Controle do coletor.
    void start();
    void stop();

    // Recebimento generico de eventos.
    void enqueueEvent(const RobotEvent& event);

    // Registro de eventos individuais.
    void recordRobotEvent(
        int robot_id,
        EventType type,
        const std::string& description
    );

    // Registro de transporte de cargas.
    void recordCargoDelivery(
        int bay_id,
        const std::string& destination,
        const std::vector<int>& robot_ids
    );
};

#endif
