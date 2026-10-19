#include <gtest/gtest.h>
#include "logistics.hpp"
#include <algorithm>
#include <initializer_list>

namespace {
MapaGalpao layout() { MapaGalpao map(6,5); map.adicionarBaia({1,{1,1},{}}); map.adicionarDestino({1,{4,1}}); map.definirCelula(0,0,EstadoCelula::ESTACAO_RECARGA); return map; }
void registrarRobo(EstadoFrota& frota, int id) {
    frota.registrarRobo(id,1,{0,1}); EstadoAtualRobo s; s.id = id; s.geracao = 1; s.sequencia = 1; s.posicao = {0,1}; s.disponivel = true; s.conectado = true; frota.atualizar(s);
}
Carga load(int tamanho) { Carga c; c.id = 1; c.tamanho = tamanho; c.id_baia = 1; c.id_destino = 1; return c; }
Mensagem report(int id, EstadoRobo estado, FaseMissao fase, bool acoplado = false) {
    Mensagem m; m.tipo = TipoMensagem::EstadoAtualRobo; m.id_robo = id; m.geracao = 1; m.sequencia = 2; m.id_missao = 1;
    m.estado = estado; m.fase = fase; m.acoplado = acoplado; m.posicao = {1,1}; return m;
}
void cycle(OrganizadorLogistica& organizador, std::initializer_list<Mensagem> mensagens = {}) {
    organizador.processarCiclo(std::vector<Mensagem>(mensagens));
}
Mensagem resume(IdEntidade id) { Mensagem m; m.tipo = TipoMensagem::RetomarMissao; m.id_missao = id; return m; }
}

TEST(LogisticsTest, WaitsForCompleteTeamAndAllArrivalReports) {
    auto map = layout(); EstadoFrota frota; registrarRobo(frota,1); std::vector<Mensagem> sent;
    OrganizadorLogistica organizer(map,frota,{load(2)},[&](int,Mensagem m) { sent.push_back(m); return true; });
    cycle(organizer); EXPECT_TRUE(sent.empty()); EXPECT_EQ(organizer.cargos()[0].estado,EstadoCarga::PENDENTE);
    registrarRobo(frota,2); cycle(organizer); ASSERT_EQ(sent.size(),2u); EXPECT_EQ(sent[0].tipo,TipoMensagem::AtribuirMissao);
    cycle(organizer,{report(1,EstadoRobo::AGUARDANDO_EQUIPE,FaseMissao::COLETA_PRONTA)}); EXPECT_EQ(sent.size(),2u);
    cycle(organizer,{report(2,EstadoRobo::AGUARDANDO_EQUIPE,FaseMissao::COLETA_PRONTA)}); ASSERT_EQ(sent.size(),4u); EXPECT_EQ(sent.back().tipo,TipoMensagem::Acoplar);
    cycle(organizer,{report(1,EstadoRobo::AGUARDANDO_EQUIPE,FaseMissao::ACOPLANDO,true)}); EXPECT_EQ(sent.size(),4u);
    cycle(organizer,{report(2,EstadoRobo::AGUARDANDO_EQUIPE,FaseMissao::ACOPLANDO,true)}); ASSERT_EQ(sent.size(),6u); EXPECT_EQ(sent.back().fase,FaseMissao::INDO_PARA_ENTREGA);
    cycle(organizer,{report(1,EstadoRobo::AGUARDANDO_DESCARGA,FaseMissao::ENTREGA_PRONTA,true), report(2,EstadoRobo::AGUARDANDO_DESCARGA,FaseMissao::ENTREGA_PRONTA,true)}); ASSERT_EQ(sent.size(),8u);
    cycle(organizer,{report(1,EstadoRobo::AGUARDANDO_DESCARGA,FaseMissao::DESACOPLANDO,false)}); EXPECT_NE(organizer.cargos()[0].estado,EstadoCarga::ENTREGUE);
    cycle(organizer,{report(2,EstadoRobo::AGUARDANDO_DESCARGA,FaseMissao::DESACOPLANDO,false)}); EXPECT_EQ(organizer.cargos()[0].estado,EstadoCarga::ENTREGUE);
}

TEST(LogisticsTest, CancelsBeforeCouplingAndPreservesQueue) {
    auto map = layout(); EstadoFrota frota; registrarRobo(frota,1); registrarRobo(frota,2);
    OrganizadorLogistica organizer(map,frota,{load(2)},[](int,Mensagem) { return true; }); cycle(organizer);
    auto falha = report(1,EstadoRobo::COM_FALHA,FaseMissao::INDO_PARA_COLETA); falha.falha = true;
    auto estado = estadoRoboDaMensagem(falha); estado.sequencia = 3; frota.atualizar(estado); cycle(organizer,{falha});
    EXPECT_EQ(organizer.cargos()[0].estado,EstadoCarga::PENDENTE); EXPECT_EQ(organizer.missoes()[0].fase,FaseMissao::NENHUMA);
}

TEST(LogisticsTest, SuspendsPartiallyCoupledTeamInsteadOfReassigning) {
    auto map = layout(); EstadoFrota frota; registrarRobo(frota,1); registrarRobo(frota,2);
    OrganizadorLogistica organizer(map,frota,{load(2)},[](int,Mensagem) { return true; }); cycle(organizer);
    cycle(organizer,{report(1,EstadoRobo::AGUARDANDO_EQUIPE,FaseMissao::COLETA_PRONTA), report(2,EstadoRobo::AGUARDANDO_EQUIPE,FaseMissao::COLETA_PRONTA)});
    auto falha = report(2,EstadoRobo::COM_FALHA,FaseMissao::ACOPLANDO); falha.falha = true;
    cycle(organizer,{report(1,EstadoRobo::AGUARDANDO_EQUIPE,FaseMissao::ACOPLANDO,true),falha});
    EXPECT_EQ(organizer.cargos()[0].estado,EstadoCarga::SUSPENSO); EXPECT_EQ(organizer.missoes().size(),1u);
    cycle(organizer,{resume(1)}); EXPECT_EQ(organizer.cargos()[0].estado,EstadoCarga::SUSPENSO);
}

TEST(LogisticsTest, ResumeRequiresCurrentSensorsAndIgnoresObsoleteFaults) {
    auto map = layout(); EstadoFrota frota; registrarRobo(frota,1);
    std::vector<AvisoLogistica::TipoAviso> avisos;
    OrganizadorLogistica organizer(map,frota,{load(1)},[](int,Mensagem) { return true; },[&](const AvisoLogistica& n) { avisos.push_back(n.tipo); });
    cycle(organizer);
    cycle(organizer,{report(1,EstadoRobo::AGUARDANDO_EQUIPE,FaseMissao::COLETA_PRONTA)});
    cycle(organizer,{report(1,EstadoRobo::AGUARDANDO_EQUIPE,FaseMissao::ACOPLANDO,true)});
    auto falha = report(1,EstadoRobo::COM_FALHA,FaseMissao::INDO_PARA_ENTREGA,true);
    falha.tipo = TipoMensagem::AtualizarSensores; falha.sequencia_sensor = 1; falha.revisao = 3; falha.falha = true;
    auto estado_falha = estadoRoboDaMensagem(falha); estado_falha.sequencia = 3; ASSERT_TRUE(frota.atualizar(estado_falha)); cycle(organizer,{falha}); ASSERT_EQ(organizer.cargos()[0].estado,EstadoCarga::SUSPENSO);
    auto recuperado = falha; recuperado.falha = false; recuperado.sequencia_sensor = 2; recuperado.revisao = 4;
    auto estado_recuperado = estadoRoboDaMensagem(recuperado); estado_recuperado.sequencia = 4;
    ASSERT_TRUE(frota.atualizar(estado_recuperado)); cycle(organizer,{recuperado});
    auto antigo = estadoRoboDaMensagem(falha); antigo.falha = false; antigo.estado = EstadoRobo::SUSPENSO; antigo.sequencia = 3;
    EXPECT_FALSE(frota.atualizar(antigo)); cycle(organizer,{resume(1)});
    EXPECT_EQ(organizer.cargos()[0].estado,EstadoCarga::SUSPENSO);
    auto atual = estadoRoboDaMensagem(recuperado); atual.estado = EstadoRobo::SUSPENSO; atual.sequencia = 5;
    ASSERT_TRUE(frota.atualizar(atual)); cycle(organizer,{resume(1)});
    ASSERT_EQ(organizer.cargos()[0].estado,EstadoCarga::EM_TRANSPORTE);
    EXPECT_EQ(std::count(avisos.begin(),avisos.end(),AvisoLogistica::TipoAviso::MissaoSuspensa),1);
}
