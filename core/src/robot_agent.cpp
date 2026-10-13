#include "robot_agent.hpp"
#include <iostream>

// TODO: TEM QUE ANALISAR O RACE CONDITION EM TUDO

RobotAgent::RobotAgent(int id, int start_x, int start_y)
    : id(id),
      posicao_atual_x(start_x),
      posicao_atual_y(start_y),
      carga_atracada(false),
      bateria(100),
      falha(false),
      setpoint_posicao_x(start_x),
      setpoint_posicao_y(start_y),
      tarefa_atual("Nenhuma"),
      estado_atual(RobotState::IDLE),
      em_processo_de_carga(false),
      running(false) {}

RobotAgent::~RobotAgent() {
    stop();
}

void RobotAgent::start() {
    running = true;
    
    navigation_thread = std::thread(&RobotAgent::navigationLoop, this);
    
    status_thread = std::thread(&RobotAgent::statusLoop, this);
    
    std::cout << "[Processo Robo " << id << "] Iniciado com 2 threads (Nav e Status).\n";
}

void RobotAgent::stop() {
    running = false;
    if (navigation_thread.joinable()) navigation_thread.join();
    if (status_thread.joinable()) status_thread.join();
    std::cout << "[Processo Robo " << id << "] Encerrado com seguranca.\n";
}

void RobotAgent::navigationLoop() {
    while (running) {
        ComandoDeNavegacao();
        //TODO: Periodicidade mais robusta
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void RobotAgent::statusLoop() {
    while (running) {
        StatusDoRobo();
        //TODO: Periodicidade mais robusta
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

void RobotAgent::ComandoDeNavegacao() {
    if (falha || em_processo_de_carga || estado_atual == RobotState::IDLE) {
        return; 
    }

    int proximo_x = posicao_atual_x;
    int proximo_y = posicao_atual_y;

    if (posicao_atual_x < setpoint_posicao_x) proximo_x++;
    else if (posicao_atual_x > setpoint_posicao_x) proximo_x--;

    if (posicao_atual_y < setpoint_posicao_y) proximo_y++;
    else if (posicao_atual_y > setpoint_posicao_y) proximo_y--;

    // TODO: Implementar lógica de passagem estreita via sessão crítica
    if (verificarSegurancaPassagemEstreita(proximo_x, proximo_y)) {
        posicao_atual_x = proximo_x;
        posicao_atual_y = proximo_y;
    } else {
        return; 
    }

    if (posicao_atual_x == setpoint_posicao_x && posicao_atual_y == setpoint_posicao_y) {
        if (estado_atual == RobotState::NAVIGATING_TO_PICKUP) {
            std::cout << "[Robô " << id << "] Chegou ao destino. Iniciando acoplamento...\n";
            estado_atual = RobotState::WAITING_TO_LOAD;
            em_processo_de_carga = true;
        }
    }
}

void RobotAgent::StatusDoRobo() {
    if (bateria < 15 && estado_atual != RobotState::CHARGING && estado_atual != RobotState::ERROR) {
        std::cout << "[Robô " << id << " - Thread Status] ALERTA: Bateria baixa (" << bateria << "%). Ajustando destino.\n";
        estado_atual = RobotState::CHARGING;
    }

    // TODO: Publicar status via MQTT
}

void RobotAgent::setRobotStatus(int nova_bateria, bool novo_estado_falha) {
    if (nova_bateria >= 0 && nova_bateria <= 100) {
        bateria = nova_bateria;
    }
    falha = novo_estado_falha;

    if (falha) {
        estado_atual = RobotState::ERROR;
    }
}

void RobotAgent::atribuirTarefa(const std::string& descricao_tarefa, int destino_x, int destino_y) {
    tarefa_atual = descricao_tarefa;
    setpoint_posicao_x = destino_x;
    setpoint_posicao_y = destino_y;
    estado_atual = RobotState::NAVIGATING_TO_PICKUP;
}

bool RobotAgent::verificarSegurancaPassagemEstreita(int proximo_x, int proximo_y) {
    // TODO: Lógica de passagens estreitas
    return true;
}

int RobotAgent::getId() const { return id; }
int RobotAgent::getX() const { return posicao_atual_x; }
int RobotAgent::getY() const { return posicao_atual_y; }
int RobotAgent::getBateria() const { return bateria; }
RobotState RobotAgent::getEstado() const { return estado_atual; }
bool RobotAgent::isFalha() const { return falha; }