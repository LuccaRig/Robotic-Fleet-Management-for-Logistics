#include "robot_agent.hpp"

RobotAgent::RobotAgent(int id, int start_x, int start_y)
    : id(id),
      posicao_atual_x(start_x),
      posicao_atual_y(start_y),
      carga_atracada(false),
      bateria(100),            // Inicializa com carga máxima
      falha(false),
      setpoint_posicao_x(start_x),
      setpoint_posicao_y(start_y) {}

void RobotAgent::updateSetpoint(int target_x, int target_y) {
    // Atualiza os setpoints de forma assíncrona
    setpoint_posicao_x = target_x;
    setpoint_posicao_y = target_y;
}

void RobotAgent::ComandoDeNavegacao(bool pronto_para_acoplar) {
    //Tarefa periódica da movimentação do robô em direção ao setpoint


    if (posicao_atual_x < setpoint_posicao_x) {
        posicao_atual_x++;
    } else if (posicao_atual_x > setpoint_posicao_x) {
        posicao_atual_x--;
    }

    if (posicao_atual_y < setpoint_posicao_y) {
        posicao_atual_y++;
    } else if (posicao_atual_y > setpoint_posicao_y) {
        posicao_atual_y--;
    }


    if (pronto_para_acoplar) {
        // TODO: Acionar acoplarAndErguer() 
    }
}

void RobotAgent::setStatusDoRobo(int nova_bateria, bool novo_estado_falha) {
    if (nova_bateria >= 0 && nova_bateria <= 100) {
        bateria = nova_bateria;
    }
    falha = novo_estado_falha;
}

int RobotAgent::getId() const {
    return id;
}

int RobotAgent::getX() const {
    return posicao_atual_x;
}

int RobotAgent::getY() const {
    return posicao_atual_y;
}

int RobotAgent::getBateria() const {
    return bateria;
}

bool RobotAgent::isFalha() const {
    return falha;
}

bool RobotAgent::isCargaAtracada() const {
    return carga_atracada;
}