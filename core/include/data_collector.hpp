#ifndef DATA_COLLECTOR_HPP
#define DATA_COLLECTOR_HPP

#include "robot_event.hpp"
#include <condition_variable>
#include <exception>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <sqlite3.h>

class ColetorDados {
public:
    explicit ColetorDados(const std::string& caminho_banco);
    ~ColetorDados();
    ColetorDados(const ColetorDados&) = delete;
    ColetorDados& operator=(const ColetorDados&) = delete;
    void iniciar();
    void parar();
    void enfileirarEvento(const EventoRobo& event);
    void verificarFalha() const;
    IdEntidade idExecucao() const;
private:
    enum class EstadoColetor { ENCERRADO, EM_EXECUCAO, ENCERRANDO, FALHOU };
    mutable std::mutex mutex_fila;
    std::condition_variable condicao_fila;
    std::queue<EventoRobo> fila_eventos;
    EstadoColetor estado = EstadoColetor::ENCERRADO;
    std::exception_ptr erro_thread;
    std::mutex mutex_ciclo_vida;
    std::thread thread_coletora;
    std::string caminho_banco;
    sqlite3* db = nullptr;
    IdEntidade run_id = 0;
    void openDatabase();
    void createTable();
    void closeDatabase() noexcept;
    void collectorLoop() noexcept;
    void writeEventToDatabase(const EventoRobo& event);
};

#endif
