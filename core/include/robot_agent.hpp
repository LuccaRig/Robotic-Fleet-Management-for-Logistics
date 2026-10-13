#ifndef ROBOT_AGENT_HPP
#define ROBOT_AGENT_HPP

#include <cstdint>
#include <iostream>

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

public:
    // Construtor
    RobotAgent(int id, int start_x, int start_y);

    void updateSetpoint(int target_x, int target_y);
    void ComandoDeNavegacao(bool pronto_para_acoplar = false);
    void setStatusDoRobo(int nova_bateria, bool novo_estado_falha);

    //void acoplarAndErguer(int tamanho_grupo_n);

    int getId() const;
    int getX() const;
    int getY() const;
    int getBateria() const;
    bool isFalha() const;
    bool isCargaAtracada() const;
};

#endif // ROBOT_AGENT_HPP