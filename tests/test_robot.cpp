#include <gtest/gtest.h>
#include "robot_agent.hpp"
#include <algorithm>

namespace {
Mensagem assign(int executarComando = 1) {
    Mensagem m; m.tipo = TipoMensagem::AtribuirMissao; m.id_robo = 1; m.geracao = 1;
    m.id_comando = executarComando; m.id_missao = 7; m.fase = FaseMissao::INDO_PARA_COLETA; m.destino = {1,0}; m.rota = {{1,0}}; m.integrantes = {1}; return m;
}
Mensagem sensorFor(const AgenteRobo& robo, std::uint64_t sequencia) {
    auto m = mensagemDoEstadoRobo(robo.capturarEstado()); m.tipo = TipoMensagem::AtualizarSensores; m.sequencia_sensor = sequencia; return m;
}
}
TEST(RobotAgentTest, MovementRequiresPermissionAndConfirmedSensor) {
    std::vector<Mensagem> sent; AgenteRobo robo(1,0,0,1,[&](Mensagem m) { sent.push_back(m); });
    robo.processarMensagem(assign()); robo.avancarNavegacao();
    ASSERT_EQ(sent.back().tipo,TipoMensagem::SolicitarAcessoTrecho); EXPECT_EQ(robo.obterX(),0);
    auto grant = sent.back(); grant.tipo = TipoMensagem::AcessoTrechoConcedido; robo.processarMensagem(grant); robo.avancarNavegacao();
    ASSERT_EQ(sent.back().tipo,TipoMensagem::DestinoNavegacao); EXPECT_EQ(robo.obterX(),0);
    auto leitura_sensores = sensorFor(robo,1); leitura_sensores.posicao = {1,0}; leitura_sensores.passo = 1; robo.processarMensagem(leitura_sensores); robo.avancarNavegacao(); robo.atualizarStatus();
    EXPECT_EQ(robo.obterX(),1); EXPECT_EQ(robo.obterEstado(),EstadoRobo::AGUARDANDO_EQUIPE); EXPECT_EQ(robo.capturarEstado().distancia,1u);
}
TEST(RobotAgentTest, RejectsObsoleteRevisionAndStopsOnFailure) {
    std::vector<Mensagem> sent; AgenteRobo robo(1,0,0,1,[&](Mensagem m) { sent.push_back(m); }); robo.processarMensagem(assign());
    auto old = sensorFor(robo,1); old.revisao = 0; old.posicao = {8,8}; robo.processarMensagem(old); EXPECT_EQ(robo.obterX(),0);
    auto fault = sensorFor(robo,2); fault.falha = true; robo.processarMensagem(fault);
    const auto count = sent.size(); robo.avancarNavegacao(); EXPECT_EQ(sent.size(),count); EXPECT_EQ(robo.obterEstado(),EstadoRobo::COM_FALHA);
    fault.sequencia_sensor = 3; fault.falha = false; robo.processarMensagem(fault); EXPECT_EQ(robo.obterEstado(),EstadoRobo::SUSPENSO);
}
TEST(RobotAgentTest, CountsAllPendingSensorSegments) {
    AgenteRobo robo(1,0,0);
    const Posicao path[]{{1,0},{1,1},{0,1},{0,0}};
    for (int i = 0; i < 4; ++i) { auto m = sensorFor(robo,i+1); m.posicao = path[i]; robo.processarMensagem(m); }
    robo.atualizarStatus(); EXPECT_EQ(robo.capturarEstado().distancia,4u);
}
TEST(RobotAgentTest, RejectsPrematureCouplingAndDuplicateCommandDoesNotResetRoute) {
    std::vector<Mensagem> sent; AgenteRobo robo(1,0,0,1,[&](Mensagem m) { sent.push_back(m); }); auto missao = assign(); robo.processarMensagem(missao);
    auto invalid = missao; invalid.tipo = TipoMensagem::Acoplar; invalid.id_comando = 2; robo.processarMensagem(invalid);
    EXPECT_FALSE(sent.back().aceito); EXPECT_EQ(robo.obterEstado(),EstadoRobo::INDO_PARA_COLETA);
    robo.processarMensagem(missao); EXPECT_FALSE(sent.back().aceito);
}
TEST(RobotAgentTest, RepeatedStartStopIsSafe) { AgenteRobo robo(1,0,0); robo.iniciar(); robo.iniciar(); robo.parar(); robo.parar(); robo.iniciar(); robo.parar(); }
TEST(RobotAgentTest, ChargeEndsOnlyAtFullBattery) {
    AgenteRobo robo(1,0,0); robo.atualizarEstadoRobo(10,false);
    auto m = assign(); m.id_missao = 0; m.fase = FaseMissao::INDO_PARA_RECARGA; m.rota.clear(); m.destino = {0,0}; robo.processarMensagem(m); robo.avancarNavegacao();
    EXPECT_EQ(robo.obterEstado(),EstadoRobo::RECARREGANDO); robo.atualizarEstadoRobo(95,false); robo.atualizarStatus(); EXPECT_EQ(robo.obterEstado(),EstadoRobo::RECARREGANDO);
    robo.atualizarEstadoRobo(100,false); robo.atualizarStatus(); EXPECT_EQ(robo.obterEstado(),EstadoRobo::DISPONIVEL);
}
TEST(RobotAgentTest, PublishesTransientFaultBeforeFollowingRecovery) {
    std::vector<Mensagem> sent; AgenteRobo robo(1,0,0,1,[&](Mensagem m) { sent.push_back(m); });
    robo.atualizarEstadoRobo(50,true); robo.atualizarEstadoRobo(50,false);
    ASSERT_GE(sent.size(),2u); EXPECT_TRUE(sent[sent.size()-2].falha); EXPECT_FALSE(sent.back().falha);
}
