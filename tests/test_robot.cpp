#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include "robot_agent.hpp"

// 1. Testes de inicialização e atribuição de tarefa
TEST(RobotAgentTest, InitializationAndTaskAssignment) {
    RobotAgent robot(1, 1, 1);
    
    EXPECT_EQ(robot.getId(), 1);
    EXPECT_EQ(robot.getX(), 1);
    EXPECT_EQ(robot.getY(), 1);
    EXPECT_EQ(robot.getBateria(), 100);
    EXPECT_FALSE(robot.isFalha());
    EXPECT_EQ(robot.getEstado(), RobotState::IDLE);

    // Atribui uma tarefa de teste
    robot.atribuirTarefa("Levar caixa da Baia 1", 4, 1);
    EXPECT_EQ(robot.getEstado(), RobotState::NAVIGATING_TO_PICKUP);
}

// 2. Teste de atualização de status e telemetria
TEST(RobotAgentTest, StatusTelemetryUpdate) {
    RobotAgent robot(2, 0, 0);

    // Atualiza bateria e define sem falha (Nome corrigido para setRobotStatus)
    robot.setRobotStatus(45, false);
    EXPECT_EQ(robot.getBateria(), 45);
    EXPECT_FALSE(robot.isFalha());

    // Atualiza com estado de falha
    robot.setRobotStatus(40, true);
    EXPECT_TRUE(robot.isFalha());
    EXPECT_EQ(robot.getEstado(), RobotState::ERROR);
}

// 3. Teste de transição automática para carregamento com bateria crítica
TEST(RobotAgentTest, CriticalBatteryTriggersChargingState) {
    RobotAgent robot(3, 2, 2);
    
    // Simula bateria abaixo de 15% via telemetria externa
    robot.setRobotStatus(10, false);
    
    // Inicia a thread para que a tarefa periódica de status processe o alerta
    robot.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    robot.stop();

    // O estado deve mudar automaticamente para CHARGING
    EXPECT_EQ(robot.getEstado(), RobotState::CHARGING);
}

// 4. Teste de execução assíncrona (Thread + Navegação)
TEST(RobotAgentTest, AsyncThreadMovementExecution) {
    RobotAgent robot(4, 0, 0);
    
    // Atribui um destino distante no eixo X
    robot.atribuirTarefa("Teste de Movimento Concorrente", 3, 0);

    // Inicia a thread do robô (o executionLoop rodará o ComandoDeNavegacao periodicamente)
    robot.start();

    // Aguarda o suficiente para o robô dar alguns passos (ex: 350ms)
    std::this_thread::sleep_for(std::chrono::milliseconds(350));

    // Para a thread com segurança
    robot.stop();

    // A posição X deve ter aumentado em direção ao destino (3)
    EXPECT_GT(robot.getX(), 0);
}