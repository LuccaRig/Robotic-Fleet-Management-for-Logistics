#include <gtest/gtest.h>
#include "robot_agent.hpp"
#include "hardware.hpp"
#include "logistics.hpp"
#include <algorithm>
#include <stdexcept>

namespace {
Mensagem modo(ModoOperacao valor, std::uint64_t id = 1) {
    Mensagem mensagem;
    mensagem.tipo = TipoMensagem::DefinirModo;
    mensagem.id_robo = 1;
    mensagem.geracao = 1;
    mensagem.id_comando = id;
    mensagem.modo = valor;
    return mensagem;
}

Mensagem destinoManual(FaseMissao fase = FaseMissao::INDO_PARA_DESTINO_MANUAL) {
    auto mensagem = modo(ModoOperacao::MANUAL, 2);
    mensagem.tipo = TipoMensagem::DefinirDestinoManual;
    mensagem.fase = fase;
    mensagem.destino = {1, 0};
    mensagem.rota = {{1, 0}};
    mensagem.integrantes = {1};
    return mensagem;
}

Mensagem leitura(const AgenteRobo& robo, std::uint64_t sequencia) {
    auto mensagem = mensagemDoEstadoRobo(robo.capturarEstado());
    mensagem.tipo = TipoMensagem::AtualizarSensores;
    mensagem.sequencia_sensor = sequencia;
    return mensagem;
}
}

TEST(ManualModeTest, ExcludesAutomaticAssignmentsAndAllowsReturningToAutomatic) {
    std::vector<Mensagem> enviadas;
    AgenteRobo robo(1, 0, 0, 1, [&](Mensagem mensagem) { enviadas.push_back(mensagem); });
    robo.processarMensagem(modo(ModoOperacao::MANUAL));
    ASSERT_TRUE(enviadas.back().aceito);
    EXPECT_EQ(robo.capturarEstado().modo, ModoOperacao::MANUAL);
    EXPECT_FALSE(robo.capturarEstado().disponivel);

    auto tarefa = destinoManual();
    tarefa.tipo = TipoMensagem::AtribuirMissao;
    tarefa.fase = FaseMissao::INDO_PARA_COLETA;
    tarefa.id_missao = 7;
    robo.processarMensagem(tarefa);
    EXPECT_FALSE(enviadas.back().aceito);
    EXPECT_EQ(robo.capturarEstado().id_missao, 0);

    robo.processarMensagem(modo(ModoOperacao::AUTOMATICO, 3));
    EXPECT_TRUE(enviadas.back().aceito);
    EXPECT_TRUE(robo.capturarEstado().disponivel);
}

TEST(ManualModeTest, RequiresPermissionAndSensorsAndStaysManualAtDestination) {
    std::vector<Mensagem> enviadas;
    AgenteRobo robo(1, 0, 0, 1, [&](Mensagem mensagem) { enviadas.push_back(mensagem); });
    robo.processarMensagem(modo(ModoOperacao::MANUAL));
    robo.processarMensagem(destinoManual());
    robo.avancarNavegacao();
    ASSERT_EQ(enviadas.back().tipo, TipoMensagem::SolicitarAcessoTrecho);
    EXPECT_EQ(robo.capturarEstado().posicao, (Posicao{0, 0}));
    auto permissao = enviadas.back();
    permissao.tipo = TipoMensagem::AcessoTrechoConcedido;
    robo.processarMensagem(permissao);
    robo.avancarNavegacao();
    ASSERT_EQ(enviadas.back().tipo, TipoMensagem::DestinoNavegacao);
    EXPECT_EQ(robo.capturarEstado().posicao, (Posicao{0, 0}));
    auto sensor = leitura(robo, 1);
    sensor.posicao = {1, 0};
    sensor.passo = 1;
    robo.processarMensagem(sensor);
    robo.avancarNavegacao();
    robo.atualizarStatus();
    const auto estado = robo.capturarEstado();
    EXPECT_EQ(estado.posicao, (Posicao{1, 0}));
    EXPECT_EQ(estado.estado, EstadoRobo::DISPONIVEL);
    EXPECT_EQ(estado.fase, FaseMissao::NENHUMA);
    EXPECT_EQ(estado.modo, ModoOperacao::MANUAL);
    EXPECT_FALSE(estado.disponivel);
    EXPECT_EQ(estado.distancia, 1u);
}

TEST(ManualModeTest, RejectsModeChangesDuringNavigation) {
    std::vector<Mensagem> enviadas;
    AgenteRobo robo(1, 0, 0, 1, [&](Mensagem mensagem) { enviadas.push_back(mensagem); });
    robo.processarMensagem(modo(ModoOperacao::MANUAL));
    robo.processarMensagem(destinoManual());
    robo.processarMensagem(modo(ModoOperacao::AUTOMATICO, 3));
    EXPECT_FALSE(enviadas.back().aceito);
    EXPECT_EQ(robo.capturarEstado().modo, ModoOperacao::MANUAL);
    EXPECT_EQ(robo.capturarEstado().estado, EstadoRobo::INDO_PARA_DESTINO_MANUAL);
}

TEST(ManualModeTest, AbortsManualRouteOnFaultAndDoesNotResumeAfterRecovery) {
    std::vector<Mensagem> enviadas;
    AgenteRobo robo(1, 0, 0, 1, [&](Mensagem mensagem) { enviadas.push_back(mensagem); });
    robo.processarMensagem(modo(ModoOperacao::MANUAL));
    robo.processarMensagem(destinoManual());
    auto sensor = leitura(robo, 1);
    sensor.falha = true;
    robo.processarMensagem(sensor);
    EXPECT_EQ(robo.capturarEstado().estado, EstadoRobo::COM_FALHA);
    sensor.sequencia_sensor = 2;
    sensor.falha = false;
    robo.processarMensagem(sensor);
    robo.avancarNavegacao();
    EXPECT_EQ(robo.capturarEstado().fase, FaseMissao::NENHUMA);
    EXPECT_EQ(robo.capturarEstado().estado, EstadoRobo::DISPONIVEL);
    EXPECT_EQ(robo.capturarEstado().modo, ModoOperacao::MANUAL);
    EXPECT_EQ(std::count_if(enviadas.begin(), enviadas.end(), [](const Mensagem& mensagem) {
        return mensagem.tipo == TipoMensagem::DestinoNavegacao;
    }), 0);
}

TEST(ManualModeTest, AllowsLowBatteryChargingAndPreservesManualModeAfterCharge) {
    AgenteRobo robo(1, 0, 0);
    robo.processarMensagem(modo(ModoOperacao::MANUAL));
    robo.atualizarEstadoRobo(0, false);
    auto recarga = destinoManual(FaseMissao::INDO_PARA_RECARGA);
    recarga.rota.clear();
    recarga.destino = {0, 0};
    robo.processarMensagem(recarga);
    robo.avancarNavegacao();
    ASSERT_EQ(robo.capturarEstado().estado, EstadoRobo::RECARREGANDO);
    robo.atualizarEstadoRobo(100, false);
    robo.atualizarStatus();
    EXPECT_EQ(robo.capturarEstado().modo, ModoOperacao::MANUAL);
    EXPECT_EQ(robo.capturarEstado().estado, EstadoRobo::DISPONIVEL);
    EXPECT_FALSE(robo.capturarEstado().disponivel);
}

TEST(ManualModeTest, LogisticsSkipsManualAndPendingOperatorRequests) {
    MapaGalpao mapa(5, 3);
    mapa.adicionarBaia({1, {1, 1}, {}});
    mapa.adicionarDestino({1, {3, 1}});
    mapa.definirCelula(0, 1, EstadoCelula::ESTACAO_RECARGA);
    Carga carga;
    carga.id = 1;
    carga.id_baia = 1;
    carga.id_destino = 1;
    OrganizadorLogistica logistica(mapa, {carga});
    EstadoAtualRobo robo;
    robo.id = 1;
    robo.geracao = 1;
    robo.posicao = {1, 1};
    robo.conectado = true;
    robo.disponivel = true;
    robo.modo = ModoOperacao::MANUAL;
    EXPECT_TRUE(logistica.processarCiclo({}, {robo}).comandos.empty());
    robo.modo = ModoOperacao::AUTOMATICO;
    EXPECT_TRUE(logistica.processarCiclo({}, {robo}, {1}).comandos.empty());
    EXPECT_EQ(logistica.processarCiclo({}, {robo}).comandos.size(), 1u);
}

TEST(ManualModeTest, HardwarePreservesManualModeAndRejectsObsoleteMovementRevision) {
    MapaGalpao mapa(3, 2);
    AdaptadorHardwareLocal hardware(mapa);
    EstadoAtualRobo robo;
    robo.id = 1;
    robo.geracao = 1;
    hardware.registrarRobo(robo);
    auto manual = modo(ModoOperacao::MANUAL);
    const auto sensores = hardware.confirmarComandoAceito(manual);
    ASSERT_EQ(sensores.size(), 1u);
    EXPECT_EQ(sensores.front().modo, ModoOperacao::MANUAL);
    auto comando = destinoManual();
    hardware.confirmarComandoAceito(comando);
    comando.tipo = TipoMensagem::DestinoNavegacao;
    comando.passo = 1;
    comando.revisao = 1;
    EXPECT_TRUE(hardware.setpoint(comando).empty());
    comando.revisao = 2;
    const auto movimento = hardware.setpoint(comando);
    ASSERT_EQ(movimento.size(), 1u);
    EXPECT_EQ(movimento.front().posicao, (Posicao{1, 0}));
    EXPECT_EQ(movimento.front().modo, ModoOperacao::MANUAL);
}
