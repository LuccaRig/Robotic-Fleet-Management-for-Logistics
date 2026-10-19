#include <gtest/gtest.h>
#include "passages.hpp"
#include "hardware.hpp"

namespace {
Mensagem at(int id, Posicao p, std::uint64_t sequencia = 0) { Mensagem m; m.id_robo = id; m.geracao = 1; m.posicao = p; m.sequencia_sensor = sequencia; return m; }
}
TEST(PassageTest, PreservesCapacityAndQueuesOppositeFlowUntilExit) {
    MapaGalpao map(5,3); map.adicionarTrecho({1,2,{{2,1}}}); std::vector<Mensagem> liberacoes;
    ControlePassagens coordinator(map,[&](Mensagem m) { liberacoes.push_back(m); });
    for (int id : {1,2}) coordinator.confirmar(at(id,{1,1})); coordinator.confirmar(at(3,{3,1}));
    auto one = at(1,{1,1}); one.destino = {2,1}; coordinator.solicitarAcesso(one);
    auto two = one; two.id_robo = 2; coordinator.solicitarAcesso(two);
    auto opposite = at(3,{3,1}); opposite.destino = {2,1}; coordinator.solicitarAcesso(opposite);
    ASSERT_EQ(liberacoes.size(),2u); EXPECT_EQ(coordinator.capturarEstado()[0].ocupado,2);
    coordinator.confirmar(at(1,{2,1},1)); coordinator.cancelar(1,0); EXPECT_EQ(coordinator.capturarEstado()[0].ocupado,2);
    coordinator.confirmar(at(1,{3,1},2)); EXPECT_EQ(liberacoes.size(),2u);
    coordinator.confirmar(at(2,{2,1},1)); coordinator.confirmar(at(2,{3,1},2));
    ASSERT_EQ(liberacoes.size(),3u); EXPECT_EQ(liberacoes.back().id_robo,3); EXPECT_EQ(coordinator.capturarEstado()[0].maximo,2);
}
TEST(PassageTest, TeamReservationIsIndivisible) {
    MapaGalpao map(5,3); map.adicionarTrecho({1,2,{{2,1}}}); std::vector<Mensagem> liberacoes;
    ControlePassagens coordinator(map,[&](Mensagem m) { liberacoes.push_back(m); });
    coordinator.confirmar(at(1,{1,1})); coordinator.confirmar(at(2,{1,1}));
    auto m = at(1,{1,1}); m.acoplado = true; m.id_missao = 1; m.integrantes = {1,2}; m.quantidade_integrantes = 2; m.destino = {2,1}; coordinator.solicitarAcesso(m);
    EXPECT_EQ(coordinator.capturarEstado()[0].ocupado,2); m.id_robo = 2; coordinator.solicitarAcesso(m); EXPECT_EQ(coordinator.capturarEstado()[0].ocupado,2);
    m.id_missao = 2; m.integrantes.push_back(3); m.quantidade_integrantes = 3; EXPECT_THROW(coordinator.solicitarAcesso(m),std::runtime_error);
}
TEST(HardwareTest, GroupMovesOnlyAfterEveryProposalAndRejectsStaleRevision) {
    MapaGalpao map(4,3); std::vector<Mensagem> sensors;
    AdaptadorHardwareLocal hardware(map,[&](Mensagem m) { sensors.push_back(m); });
    for (int id : {1,2}) {
        EstadoAtualRobo s; s.id = id; s.geracao = 1; hardware.registrarRobo(s);
        Mensagem m; m.id_robo = id; m.geracao = 1; m.tipo = TipoMensagem::AtribuirMissao; m.id_comando = 1; m.id_missao = 1; m.fase = FaseMissao::INDO_PARA_COLETA; m.integrantes = {id}; hardware.confirmarComandoAceito(m);
        m.tipo = TipoMensagem::Acoplar; m.id_comando = 2; hardware.confirmarComandoAceito(m);
        m.tipo = TipoMensagem::AtribuirMissao; m.id_comando = 3; m.fase = FaseMissao::INDO_PARA_ENTREGA; m.integrantes = {1,2}; hardware.confirmarComandoAceito(m);
    }
    auto first = *hardware.lerLeitura(1); first.destino = {1,0}; first.passo = 1; hardware.setpoint(first); EXPECT_EQ(hardware.lerLeitura(1)->posicao,(Posicao{0,0}));
    auto second = *hardware.lerLeitura(2); second.destino = {1,0}; second.passo = 1; hardware.setpoint(second);
    EXPECT_EQ(hardware.lerLeitura(1)->posicao,(Posicao{1,0})); EXPECT_EQ(hardware.lerLeitura(2)->posicao,(Posicao{1,0})); EXPECT_EQ(hardware.lerLeitura(1)->bateria,99);
    auto pause = first; pause.tipo = TipoMensagem::PausarMissao; pause.id_comando = 4; hardware.confirmarComandoAceito(pause);
    first.passo = 2; first.posicao = {1,0}; first.destino = {2,0}; hardware.setpoint(first); EXPECT_EQ(hardware.lerLeitura(1)->posicao,(Posicao{1,0}));
}
