#include <gtest/gtest.h>
#include "logistics.hpp"
#include <algorithm>

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
}
TEST(LogisticsTest, WaitsForCompleteTeamAndAllArrivalReports) {
    auto map = layout(); EstadoFrota frota; registrarRobo(frota,1); std::vector<Mensagem> sent;
    OrganizadorLogistica organizer(map,frota,{load(2)},[&](int,Mensagem m) { sent.push_back(m); return true; });
    organizer.processarUmaVez(); EXPECT_TRUE(sent.empty()); EXPECT_EQ(organizer.cargos()[0].estado,EstadoCarga::PENDENTE);
    registrarRobo(frota,2); organizer.processarUmaVez(); ASSERT_EQ(sent.size(),2u); EXPECT_EQ(sent[0].tipo,TipoMensagem::AtribuirMissao);
    organizer.aoReceberMensagem(report(1,EstadoRobo::AGUARDANDO_EQUIPE,FaseMissao::COLETA_PRONTA)); organizer.processarUmaVez(); EXPECT_EQ(sent.size(),2u);
    organizer.aoReceberMensagem(report(2,EstadoRobo::AGUARDANDO_EQUIPE,FaseMissao::COLETA_PRONTA)); organizer.processarUmaVez(); ASSERT_EQ(sent.size(),4u); EXPECT_EQ(sent.back().tipo,TipoMensagem::Acoplar);
    organizer.aoReceberMensagem(report(1,EstadoRobo::AGUARDANDO_EQUIPE,FaseMissao::ACOPLANDO,true)); organizer.processarUmaVez(); EXPECT_EQ(sent.size(),4u);
    organizer.aoReceberMensagem(report(2,EstadoRobo::AGUARDANDO_EQUIPE,FaseMissao::ACOPLANDO,true)); organizer.processarUmaVez(); ASSERT_EQ(sent.size(),6u); EXPECT_EQ(sent.back().fase,FaseMissao::INDO_PARA_ENTREGA);
    for (int id : {1,2}) organizer.aoReceberMensagem(report(id,EstadoRobo::AGUARDANDO_DESCARGA,FaseMissao::ENTREGA_PRONTA,true)); organizer.processarUmaVez(); ASSERT_EQ(sent.size(),8u);
    organizer.aoReceberMensagem(report(1,EstadoRobo::AGUARDANDO_DESCARGA,FaseMissao::DESACOPLANDO,false)); organizer.processarUmaVez(); EXPECT_NE(organizer.cargos()[0].estado,EstadoCarga::ENTREGUE);
    organizer.aoReceberMensagem(report(2,EstadoRobo::AGUARDANDO_DESCARGA,FaseMissao::DESACOPLANDO,false)); organizer.processarUmaVez(); EXPECT_EQ(organizer.cargos()[0].estado,EstadoCarga::ENTREGUE);
}
TEST(LogisticsTest, CancelsBeforeCouplingAndPreservesQueue) {
    auto map = layout(); EstadoFrota frota; registrarRobo(frota,1); registrarRobo(frota,2);
    OrganizadorLogistica organizer(map,frota,{load(2)},[](int,Mensagem) { return true; }); organizer.processarUmaVez();
    auto falha = report(1,EstadoRobo::COM_FALHA,FaseMissao::INDO_PARA_COLETA); falha.falha = true;
    auto s = estadoRoboDaMensagem(falha); s.sequencia = 3; frota.atualizar(s); organizer.aoReceberMensagem(falha); organizer.processarUmaVez();
    EXPECT_EQ(organizer.cargos()[0].estado,EstadoCarga::PENDENTE); EXPECT_EQ(organizer.missoes()[0].fase,FaseMissao::NENHUMA);
}
TEST(LogisticsTest, SuspendsPartiallyCoupledTeamInsteadOfReassigning) {
    auto map = layout(); EstadoFrota frota; registrarRobo(frota,1); registrarRobo(frota,2);
    OrganizadorLogistica organizer(map,frota,{load(2)},[](int,Mensagem) { return true; }); organizer.processarUmaVez();
    for (int id : {1,2}) organizer.aoReceberMensagem(report(id,EstadoRobo::AGUARDANDO_EQUIPE,FaseMissao::COLETA_PRONTA)); organizer.processarUmaVez();
    organizer.aoReceberMensagem(report(1,EstadoRobo::AGUARDANDO_EQUIPE,FaseMissao::ACOPLANDO,true));
    auto falha = report(2,EstadoRobo::COM_FALHA,FaseMissao::ACOPLANDO); falha.falha = true; organizer.aoReceberMensagem(falha); organizer.processarUmaVez();
    EXPECT_EQ(organizer.cargos()[0].estado,EstadoCarga::SUSPENSO); EXPECT_EQ(organizer.missoes().size(),1u);
    organizer.solicitarRetomada(1); organizer.processarUmaVez(); EXPECT_EQ(organizer.cargos()[0].estado,EstadoCarga::SUSPENSO);
}
TEST(LogisticsTest, ResumeRequiresCurrentSensorsAndIgnoresObsoleteFaults) {
    auto map = layout(); EstadoFrota frota; registrarRobo(frota,1);
    std::vector<AvisoLogistica::TipoAviso> avisos;
    OrganizadorLogistica organizer(map,frota,{load(1)},[](int,Mensagem) { return true; },[&](const AvisoLogistica& n) { avisos.push_back(n.tipo); });
    organizer.processarUmaVez();
    organizer.aoReceberMensagem(report(1,EstadoRobo::AGUARDANDO_EQUIPE,FaseMissao::COLETA_PRONTA)); organizer.processarUmaVez();
    organizer.aoReceberMensagem(report(1,EstadoRobo::AGUARDANDO_EQUIPE,FaseMissao::ACOPLANDO,true)); organizer.processarUmaVez();
    auto falha = report(1,EstadoRobo::COM_FALHA,FaseMissao::INDO_PARA_ENTREGA,true);
    falha.tipo = TipoMensagem::AtualizarSensores; falha.sequencia_sensor = 1; falha.revisao = 3; falha.falha = true;
    organizer.aoReceberMensagem(falha); organizer.processarUmaVez(); ASSERT_EQ(organizer.cargos()[0].estado,EstadoCarga::SUSPENSO);
    auto recovered = falha; recovered.falha = false; recovered.sequencia_sensor = 2; recovered.revisao = 4;
    organizer.aoReceberMensagem(recovered); organizer.processarUmaVez();
    auto old = estadoRoboDaMensagem(falha); old.falha = false; old.estado = EstadoRobo::SUSPENSO; old.sequencia = 3;
    ASSERT_TRUE(frota.atualizar(old)); organizer.solicitarRetomada(1); organizer.processarUmaVez();
    EXPECT_EQ(organizer.cargos()[0].estado,EstadoCarga::SUSPENSO);
    auto atual = estadoRoboDaMensagem(recovered); atual.estado = EstadoRobo::SUSPENSO; atual.sequencia = 4;
    ASSERT_TRUE(frota.atualizar(atual)); organizer.solicitarRetomada(1); organizer.processarUmaVez();
    ASSERT_EQ(organizer.cargos()[0].estado,EstadoCarga::EM_TRANSPORTE);
    organizer.aoReceberMensagem(falha); organizer.processarUmaVez();
    EXPECT_EQ(organizer.cargos()[0].estado,EstadoCarga::EM_TRANSPORTE);
    EXPECT_EQ(std::count(avisos.begin(),avisos.end(),AvisoLogistica::TipoAviso::MissaoSuspensa),1);
}
