#include <gtest/gtest.h>
#include "passages.hpp"
#include "hardware.hpp"

namespace {
Mensagem at(int id, Posicao p, std::uint64_t sequencia = 0) { Mensagem m; m.id_robo = id; m.geracao = 1; m.posicao = p; m.sequencia_sensor = sequencia; return m; }
void append(std::vector<Mensagem>& destino, ResultadoPassagens resultado) {
    destino.insert(destino.end(), resultado.permissoes.begin(), resultado.permissoes.end());
}
void append(std::vector<Mensagem>& destino, std::vector<Mensagem> origem) {
    destino.insert(destino.end(), origem.begin(), origem.end());
}
}

TEST(PassageTest, PreservesCapacityAndQueuesOppositeFlowUntilExit) {
    MapaGalpao map(5,3); map.adicionarTrecho({1,2,{{2,1}}}); std::vector<Mensagem> liberacoes;
    ControlePassagens coordinator(map);
    for (int id : {1,2}) coordinator.confirmar(at(id,{1,1})); coordinator.confirmar(at(3,{3,1}));
    auto one = at(1,{1,1}); one.destino = {2,1}; append(liberacoes,coordinator.solicitarAcesso(one));
    auto two = one; two.id_robo = 2; append(liberacoes,coordinator.solicitarAcesso(two));
    auto opposite = at(3,{3,1}); opposite.destino = {2,1}; append(liberacoes,coordinator.solicitarAcesso(opposite));
    ASSERT_EQ(liberacoes.size(),2u); EXPECT_EQ(coordinator.capturarEstado()[0].ocupado,2);
    coordinator.confirmar(at(1,{2,1},1)); coordinator.cancelar(1,0); EXPECT_EQ(coordinator.capturarEstado()[0].ocupado,2);
    coordinator.confirmar(at(1,{3,1},2)); EXPECT_EQ(liberacoes.size(),2u);
    coordinator.confirmar(at(2,{2,1},1));
    append(liberacoes,coordinator.confirmar(at(2,{3,1},2)));
    ASSERT_EQ(liberacoes.size(),3u); EXPECT_EQ(liberacoes.back().id_robo,3); EXPECT_EQ(coordinator.capturarEstado()[0].maximo,2);
}

TEST(PassageTest, TeamReservationIsIndivisible) {
    MapaGalpao map(5,3); map.adicionarTrecho({1,2,{{2,1}}}); std::vector<Mensagem> liberacoes;
    ControlePassagens coordinator(map);
    coordinator.confirmar(at(1,{1,1})); coordinator.confirmar(at(2,{1,1}));
    auto m = at(1,{1,1}); m.acoplado = true; m.id_missao = 1; m.integrantes = {1,2}; m.quantidade_integrantes = 2; m.destino = {2,1};
    append(liberacoes,coordinator.solicitarAcesso(m));
    EXPECT_EQ(coordinator.capturarEstado()[0].ocupado,2); m.id_robo = 2; append(liberacoes,coordinator.solicitarAcesso(m)); EXPECT_EQ(coordinator.capturarEstado()[0].ocupado,2);
    m.id_missao = 2; m.integrantes.push_back(3); m.quantidade_integrantes = 3; EXPECT_THROW(coordinator.solicitarAcesso(m),std::runtime_error);
}

TEST(PassageTest, ReturnsPermissionAndEntryExitEventsToCentralCycle) {
    MapaGalpao map(5,3); map.adicionarTrecho({1,1,{{2,1}}});
    ControlePassagens coordinator(map);
    coordinator.confirmar(at(1,{1,1}));
    auto request = at(1,{1,1}); request.destino = {2,1}; request.quantidade_integrantes = 1;
    const auto granted = coordinator.solicitarAcesso(request);
    ASSERT_EQ(granted.permissoes.size(),1u);
    EXPECT_EQ(granted.permissoes.front().tipo,TipoMensagem::AcessoTrechoConcedido);
    const auto entered = coordinator.confirmar(at(1,{2,1},1));
    ASSERT_EQ(entered.eventos.size(),1u); EXPECT_TRUE(entered.eventos.front().entrou);
    const auto exited = coordinator.confirmar(at(1,{3,1},2));
    ASSERT_EQ(exited.eventos.size(),1u); EXPECT_FALSE(exited.eventos.front().entrou);
    EXPECT_EQ(coordinator.capturarEstado()[0].ocupado,0);
}

TEST(HardwareTest, GroupMovesOnlyAfterEveryProposalAndRejectsStaleRevision) {
    MapaGalpao map(4,3); std::vector<Mensagem> sensors;
    AdaptadorHardwareLocal hardware(map);
    for (int id : {1,2}) {
        EstadoAtualRobo s; s.id = id; s.geracao = 1; hardware.registrarRobo(s);
        Mensagem m; m.id_robo = id; m.geracao = 1; m.tipo = TipoMensagem::AtribuirMissao; m.id_comando = 1; m.id_missao = 1; m.fase = FaseMissao::INDO_PARA_COLETA; m.integrantes = {id}; append(sensors,hardware.confirmarComandoAceito(m));
        m.tipo = TipoMensagem::Acoplar; m.id_comando = 2; append(sensors,hardware.confirmarComandoAceito(m));
        m.tipo = TipoMensagem::AtribuirMissao; m.id_comando = 3; m.fase = FaseMissao::INDO_PARA_ENTREGA; m.integrantes = {1,2}; append(sensors,hardware.confirmarComandoAceito(m));
    }
    auto first = *hardware.lerLeitura(1); first.destino = {1,0}; first.passo = 1; append(sensors,hardware.setpoint(first)); EXPECT_EQ(hardware.lerLeitura(1)->posicao,(Posicao{0,0}));
    auto second = *hardware.lerLeitura(2); second.destino = {1,0}; second.passo = 1; append(sensors,hardware.setpoint(second));
    EXPECT_EQ(hardware.lerLeitura(1)->posicao,(Posicao{1,0})); EXPECT_EQ(hardware.lerLeitura(2)->posicao,(Posicao{1,0})); EXPECT_EQ(hardware.lerLeitura(1)->bateria,99);
    auto pause = first; pause.tipo = TipoMensagem::PausarMissao; pause.id_comando = 4; append(sensors,hardware.confirmarComandoAceito(pause));
    first.passo = 2; first.posicao = {1,0}; first.destino = {2,0}; append(sensors,hardware.setpoint(first)); EXPECT_EQ(hardware.lerLeitura(1)->posicao,(Posicao{1,0}));
}
