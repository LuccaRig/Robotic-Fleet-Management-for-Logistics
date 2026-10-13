#include <gtest/gtest.h>
#include "robot_agent.hpp"
#include "warehouse.hpp"

// 1. Testes unitários isolados do RobotAgent
TEST(RobotAgentTest, InitializationAndAsyncSetpoint) {
    // Robô começa em (1, 1)
    RobotAgent robot(1, 1, 1);
    
    EXPECT_EQ(robot.getId(), 1);
    EXPECT_EQ(robot.getX(), 1);
    EXPECT_EQ(robot.getY(), 1);
    EXPECT_EQ(robot.getBateria(), 100);
    EXPECT_FALSE(robot.isFalha());

    // Atualiza o setpoint assincronamente para (4, 1)
    robot.updateSetpoint(4, 1);

    // Executa ciclos da tarefa periódica de navegação
    robot.ComandoDeNavegacao(); // Vai para (2, 1)
    EXPECT_EQ(robot.getX(), 2);
    
    robot.ComandoDeNavegacao(); // Vai para (3, 1)
    EXPECT_EQ(robot.getX(), 3);
}

// 2. Testes de Integração: RobotAgent + WarehouseGrid 
// OBS: ESSES TESTES NÃO ESTÃO BONS, TESTAR A INTEGRAÇÃO DO ROBO COM A WAREHOUSE NO ORQUESTRADOR MELHOR DEPOIS
TEST(RobotWarehouseIntegrationTest, RobotMovementWithinWarehouseBounds) {
    // Cria um armazém de 10x10
    WarehouseGrid warehouse(10, 10);

    // Instancia o robô em uma posição válida do armazém
    RobotAgent robot(1, 0, 0);

    // Define um destino dentro dos limites do armazém
    robot.updateSetpoint(3, 3);

    // Simula a movimentação periódica até o destino
    for (int step = 0; step < 3; ++step) {
        robot.ComandoDeNavegacao();
    }

    // Verifica se a posição final do robô está dentro dos limites do grid do warehouse
    EXPECT_GE(robot.getX(), 0);
    EXPECT_LT(robot.getX(), warehouse.getWidth());
    EXPECT_GE(robot.getY(), 0);
    EXPECT_LT(robot.getY(), warehouse.getHeight());

    // Simula o registro do robô na matriz do armazém
    warehouse.setCell(robot.getX(), robot.getY(), CellState::ROBOT);

    // Confirma que a célula do armazém foi marcada corretamente como ocupada pelo robô
    EXPECT_EQ(warehouse.getCell(robot.getX(), robot.getY()), CellState::ROBOT);
}

// 3. Teste de atualização de status do robô
TEST(RobotAgentTest, StatusTelemetryUpdate) {
    RobotAgent robot(2, 0, 0);

    // Simula a chegada de novos dados de bateria e falha via tarefa periódica de status
    robot.StatusDoRobo(45, true);

    EXPECT_EQ(robot.getBateria(), 45);
    EXPECT_TRUE(robot.isFalha());
}