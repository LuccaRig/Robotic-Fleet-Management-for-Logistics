#include <gtest/gtest.h>
#include "logistics.hpp"
#include <algorithm>
#include <initializer_list>
#include <iostream>

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
struct SaidasLogistica {
    std::vector<Mensagem> comandos;
    std::vector<AvisoLogistica::TipoAviso> eventos;
};
void cycle(OrganizadorLogistica& organizador, const EstadoFrota& frota, SaidasLogistica& saidas, std::initializer_list<Mensagem> mensagens = {}) {
    auto resultado = organizador.processarCiclo(std::vector<Mensagem>(mensagens), frota.capturarEstado());
    for (auto& comando : resultado.comandos) saidas.comandos.push_back(std::move(comando.second));
    for (const auto& evento : resultado.eventos) saidas.eventos.push_back(evento.tipo);
}
Mensagem resume(IdEntidade id) { Mensagem m; m.tipo = TipoMensagem::RetomarMissao; m.id_missao = id; return m; }
}

TEST(LogisticsTest, WaitsForCompleteTeamAndAllArrivalReports) {
    std::cout << "\n[TESTE] LogisticsTest.WaitsForCompleteTeamAndAllArrivalReports iniciado..." << std::unitbuf;
    auto map = layout(); EstadoFrota frota; registrarRobo(frota,1); SaidasLogistica saidas;
    OrganizadorLogistica organizer(map,{load(2)});
    
    std::cout << " -> Ciclo 1: Apenas robô 1 presente. Verificando pendência de carga.\n";
    cycle(organizer,frota,saidas); EXPECT_TRUE(saidas.comandos.empty()); EXPECT_EQ(organizer.cargos()[0].estado,EstadoCarga::PENDENTE);
    
    std::cout << " -> Ciclo 2: Adicionando robô 2 para completar equipe e atribuir missão.\n";
    registrarRobo(frota,2); cycle(organizer,frota,saidas); ASSERT_EQ(saidas.comandos.size(),2u); EXPECT_EQ(saidas.comandos[0].tipo,TipoMensagem::AtribuirMissao);
    ASSERT_EQ(saidas.eventos.size(),1u); EXPECT_EQ(saidas.eventos.front(),AvisoLogistica::TipoAviso::MissaoAtribuida);
    
    std::cout << " -> Relatando chegada dos robôs e acoplamento da carga.\n";
    cycle(organizer,frota,saidas,{report(1,EstadoRobo::AGUARDANDO_EQUIPE,FaseMissao::COLETA_PRONTA)}); EXPECT_EQ(saidas.comandos.size(),2u);
    cycle(organizer,frota,saidas,{report(2,EstadoRobo::AGUARDANDO_EQUIPE,FaseMissao::COLETA_PRONTA)}); ASSERT_EQ(saidas.comandos.size(),4u); EXPECT_EQ(saidas.comandos.back().tipo,TipoMensagem::Acoplar);
    cycle(organizer,frota,saidas,{report(1,EstadoRobo::AGUARDANDO_EQUIPE,FaseMissao::ACOPLANDO,true)}); EXPECT_EQ(saidas.comandos.size(),4u);
    cycle(organizer,frota,saidas,{report(2,EstadoRobo::AGUARDANDO_EQUIPE,FaseMissao::ACOPLANDO,true)}); ASSERT_EQ(saidas.comandos.size(),6u); EXPECT_EQ(saidas.comandos.back().fase,FaseMissao::INDO_PARA_ENTREGA);
    
    std::cout << " -> Realizando entrega e desacoplamento final.\n";
    cycle(organizer,frota,saidas,{report(1,EstadoRobo::AGUARDANDO_DESCARGA,FaseMissao::ENTREGA_PRONTA,true), report(2,EstadoRobo::AGUARDANDO_DESCARGA,FaseMissao::ENTREGA_PRONTA,true)}); ASSERT_EQ(saidas.comandos.size(),8u);
    cycle(organizer,frota,saidas,{report(1,EstadoRobo::AGUARDANDO_DESCARGA,FaseMissao::DESACOPLANDO,false)}); EXPECT_NE(organizer.cargos()[0].estado,EstadoCarga::ENTREGUE);
    cycle(organizer,frota,saidas,{report(2,EstadoRobo::AGUARDANDO_DESCARGA,FaseMissao::DESACOPLANDO,false)}); EXPECT_EQ(organizer.cargos()[0].estado,EstadoCarga::ENTREGUE);
    std::cout << "[SUCESSO] LogisticsTest.WaitsForCompleteTeamAndAllArrivalReports passou!\n";
}

TEST(LogisticsTest, CancelsBeforeCouplingAndPreservesQueue) {
    std::cout << "\n[TESTE] LogisticsTest.CancelsBeforeCouplingAndPreservesQueue iniciado..." << std::unitbuf;
    auto map = layout(); EstadoFrota frota; registrarRobo(frota,1); registrarRobo(frota,2);
    SaidasLogistica saidas; OrganizadorLogistica organizer(map,{load(2)}); cycle(organizer,frota,saidas);
    
    std::cout << " -> Simulando falha antes do acoplamento e verificando cancelamento.\n";
    auto falha = report(1,EstadoRobo::COM_FALHA,FaseMissao::INDO_PARA_COLETA); falha.falha = true;
    auto estado = estadoRoboDaMensagem(falha); estado.sequencia = 3; frota.atualizar(estado); cycle(organizer,frota,saidas,{falha});
    EXPECT_EQ(organizer.cargos()[0].estado,EstadoCarga::PENDENTE); EXPECT_EQ(organizer.missoes()[0].fase,FaseMissao::NENHUMA);
    std::cout << "[SUCESSO] LogisticsTest.CancelsBeforeCouplingAndPreservesQueue passou!\n";
}

TEST(LogisticsTest, SuspendsPartiallyCoupledTeamInsteadOfReassigning) {
    std::cout << "\n[TESTE] LogisticsTest.SuspendsPartiallyCoupledTeamInsteadOfReassigning iniciado..." << std::unitbuf;
    auto map = layout(); EstadoFrota frota; registrarRobo(frota,1); registrarRobo(frota,2);
    SaidasLogistica saidas; OrganizadorLogistica organizer(map,{load(2)}); cycle(organizer,frota,saidas);
    cycle(organizer,frota,saidas,{report(1,EstadoRobo::AGUARDANDO_EQUIPE,FaseMissao::COLETA_PRONTA), report(2,EstadoRobo::AGUARDANDO_EQUIPE,FaseMissao::COLETA_PRONTA)});
    
    std::cout << " -> Simulando falha parcial e verificando suspensão da missão.\n";
    auto falha = report(2,EstadoRobo::COM_FALHA,FaseMissao::ACOPLANDO); falha.falha = true;
    cycle(organizer,frota,saidas,{report(1,EstadoRobo::AGUARDANDO_EQUIPE,FaseMissao::ACOPLANDO,true),falha});
    EXPECT_EQ(organizer.cargos()[0].estado,EstadoCarga::SUSPENSO); EXPECT_EQ(organizer.missoes().size(),1u);
    cycle(organizer,frota,saidas,{resume(1)}); EXPECT_EQ(organizer.cargos()[0].estado,EstadoCarga::SUSPENSO);
    std::cout << "[SUCESSO] LogisticsTest.SuspendsPartiallyCoupledTeamInsteadOfReassigning passou!\n";
}

TEST(LogisticsTest, ResumeRequiresCurrentSensorsAndIgnoresObsoleteFaults) {
    std::cout << "\n[TESTE] LogisticsTest.ResumeRequiresCurrentSensorsAndIgnoresObsoleteFaults iniciado..." << std::unitbuf;
    auto map = layout(); EstadoFrota frota; registrarRobo(frota,1);
    SaidasLogistica saidas; OrganizadorLogistica organizer(map,{load(1)});
    cycle(organizer,frota,saidas);
    cycle(organizer,frota,saidas,{report(1,EstadoRobo::AGUARDANDO_EQUIPE,FaseMissao::COLETA_PRONTA)});
    cycle(organizer,frota,saidas,{report(1,EstadoRobo::AGUARDANDO_EQUIPE,FaseMissao::ACOPLANDO,true)});
    
    std::cout << " -> Testando recuperação de falhas obsoletas e validação de sensores correntes.\n";
    auto falha = report(1,EstadoRobo::COM_FALHA,FaseMissao::INDO_PARA_ENTREGA,true);
    falha.tipo = TipoMensagem::AtualizarSensores; falha.sequencia_sensor = 1; falha.revisao = 3; falha.falha = true;
    auto estado_falha = estadoRoboDaMensagem(falha); estado_falha.sequencia = 3; ASSERT_TRUE(frota.atualizar(estado_falha)); cycle(organizer,frota,saidas,{falha}); ASSERT_EQ(organizer.cargos()[0].estado,EstadoCarga::SUSPENSO);
    auto recuperado = falha; recuperado.falha = false; recuperado.sequencia_sensor = 2; recuperado.revisao = 4;
    auto estado_recuperado = estadoRoboDaMensagem(recuperado); estado_recuperado.sequencia = 4;
    ASSERT_TRUE(frota.atualizar(estado_recuperado)); cycle(organizer,frota,saidas,{recuperado});
    auto antigo = estadoRoboDaMensagem(falha); antigo.falha = false; antigo.estado = EstadoRobo::SUSPENSO; antigo.sequencia = 3;
    EXPECT_FALSE(frota.atualizar(antigo)); cycle(organizer,frota,saidas,{resume(1)});
    EXPECT_EQ(organizer.cargos()[0].estado,EstadoCarga::SUSPENSO);
    auto atual = estadoRoboDaMensagem(recuperado); atual.estado = EstadoRobo::SUSPENSO; atual.sequencia = 5;
    ASSERT_TRUE(frota.atualizar(atual)); cycle(organizer,frota,saidas,{resume(1)});
    ASSERT_EQ(organizer.cargos()[0].estado,EstadoCarga::EM_TRANSPORTE);
    EXPECT_EQ(std::count(saidas.eventos.begin(),saidas.eventos.end(),AvisoLogistica::TipoAviso::MissaoSuspensa),1);
    std::cout << "[SUCESSO] LogisticsTest.ResumeRequiresCurrentSensorsAndIgnoresObsoleteFaults passou!\n";
}