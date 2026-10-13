#ifndef ROBOT_AGENT_HPP
#define ROBOT_AGENT_HPP

#include <thread>
#include <atomic>
#include <chrono>
#include <string>

enum class RobotState {
    IDLE,               
    NAVIGATING_TO_PICKUP, 
    WAITING_TO_LOAD,    
    NAVIGATING_TO_DELIVERY, 
    CHARGING,           
    ERROR               
};

class RobotAgent {
private:
    int id;
    int posicao_atual_x;
    int posicao_atual_y;
    bool carga_atracada;
    int bateria;            // 0 a 100
    bool falha;
    
    int setpoint_posicao_x;
    int setpoint_posicao_y;
    std::string tarefa_atual;
    RobotState estado_atual;
    bool em_processo_de_carga; 

    std::thread navigation_thread;
    std::thread status_thread;
    std::atomic<bool> running;

    void navigationLoop();
    void statusLoop();

    bool verificarSegurancaPassagemEstreita(int proximo_x, int proximo_y);

public:
    RobotAgent(int id, int start_x, int start_y);
    ~RobotAgent();

    void start();
    void stop();

    void atribuirTarefa(const std::string& descricao_tarefa, int destino_x, int destino_y);
    void ComandoDeNavegacao(); 
    void StatusDoRobo();       
    void setRobotStatus(int nova_bateria, bool novo_estado_falha);

    int getId() const;
    int getX() const;
    int getY() const;
    int getBateria() const;
    RobotState getEstado() const;
    bool isFalha() const;
};

#endif // ROBOT_AGENT_HPP