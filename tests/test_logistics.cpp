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
void registrarRoboOcupado(EstadoFrota& frota, int id) {
    registrarRobo(frota, id);
    auto estado = *frota.obter(id);
    estado.sequencia = 2; estado.estado = EstadoRobo::INDO_PARA_ENTREGA; estado.fase = FaseMissao::INDO_PARA_ENTREGA;
    estado.id_missao = 99; estado.disponivel = false;
    frota.atualizar(estado);
}
void liberarRobo(EstadoFrota& frota, int id) {
    auto estado = *frota.obter(id);
    ++estado.sequencia; estado.estado = EstadoRobo::DISPONIVEL; estado.fase = FaseMissao::NENHUMA;
    estado.id_missao = 0; estado.id_carga = 0; estado.id_equipe = 0; estado.disponivel = true;
    frota.atualizar(estado);
}
void definirBateria(EstadoFrota& frota, int id, int bateria) {
    auto estado = *frota.obter(id);
    ++estado.sequencia; estado.bateria = bateria;
    frota.atualizar(estado);
}
Carga load(int tamanho) { Carga c; c.id = 1; c.tamanho = tamanho; c.id_baia = 1; c.id_destino = 1; return c; }
Mensagem report(int id, EstadoRobo estado, FaseMissao fase, bool acoplado = false, IdEntidade missao = 1) {
    Mensagem m; m.tipo = TipoMensagem::EstadoAtualRobo; m.id_robo = id; m.geracao = 1; m.sequencia = 2; m.id_missao = missao;
    m.estado = estado; m.fase = fase; m.acoplado = acoplado; m.posicao = {1,1}; return m;
}
struct SaidasLogistica {
    std::vector<std::pair<int, Mensagem>> comandos;
    std::vector<AvisoLogistica::TipoAviso> eventos;
};
void cycle(OrganizadorLogistica& organizador, const EstadoFrota& frota, SaidasLogistica& saidas, std::initializer_list<Mensagem> mensagens = {}) {
    auto resultado = organizador.processarCiclo(std::vector<Mensagem>(mensagens), frota.capturarEstado());
    for (auto& comando : resultado.comandos) saidas.comandos.push_back(std::move(comando));
    for (const auto& evento : resultado.eventos) saidas.eventos.push_back(evento.tipo);
}
Mensagem resume(IdEntidade id) { Mensagem m; m.tipo = TipoMensagem::RetomarMissao; m.id_missao = id; return m; }
}

TEST(LogisticsTest, WaitsForCompleteTeamAndAllArrivalReports) {
    std::cout << "\n[TESTE] LogisticsTest.WaitsForCompleteTeamAndAllArrivalReports iniciado..." << std::unitbuf;
    auto map = layout(); EstadoFrota frota; registrarRobo(frota,1); SaidasLogistica saidas;
    OrganizadorLogistica organizer(map,{load(2)});
    
    std::cout << " -> Ciclo 1: Robô 1 inicia a coleta enquanto aguarda completar a equipe.\n";
    cycle(organizer,frota,saidas); ASSERT_EQ(saidas.comandos.size(),1u);
    EXPECT_EQ(saidas.comandos[0].second.tipo,TipoMensagem::AtribuirMissao);
    EXPECT_EQ(saidas.comandos[0].first,1);
    EXPECT_EQ(organizer.cargos()[0].estado,EstadoCarga::REUNINDO_EQUIPE);
    
    std::cout << " -> Robô 1 aguarda na baia sem acoplar antes da chegada da equipe.\n";
    cycle(organizer,frota,saidas,{report(1,EstadoRobo::AGUARDANDO_EQUIPE,FaseMissao::COLETA_PRONTA)});
    EXPECT_EQ(saidas.comandos.size(),1u);

    std::cout << " -> Ciclo 2: Robô 2 completa a equipe e recebe a missão.\n";
    registrarRobo(frota,2); cycle(organizer,frota,saidas); ASSERT_EQ(saidas.comandos.size(),2u);
    EXPECT_EQ(saidas.comandos.back().second.tipo,TipoMensagem::AtribuirMissao);
    EXPECT_EQ(saidas.comandos.back().first,2);
    ASSERT_EQ(saidas.eventos.size(),1u); EXPECT_EQ(saidas.eventos.front(),AvisoLogistica::TipoAviso::MissaoAtribuida);
    
    std::cout << " -> Relatando chegada dos robôs e acoplamento da carga.\n";
    cycle(organizer,frota,saidas,{report(2,EstadoRobo::AGUARDANDO_EQUIPE,FaseMissao::COLETA_PRONTA)}); ASSERT_EQ(saidas.comandos.size(),4u); EXPECT_EQ(saidas.comandos.back().second.tipo,TipoMensagem::Acoplar);
    cycle(organizer,frota,saidas,{report(1,EstadoRobo::AGUARDANDO_EQUIPE,FaseMissao::ACOPLANDO,true)}); EXPECT_EQ(saidas.comandos.size(),4u);
    cycle(organizer,frota,saidas,{report(2,EstadoRobo::AGUARDANDO_EQUIPE,FaseMissao::ACOPLANDO,true)}); ASSERT_EQ(saidas.comandos.size(),6u); EXPECT_EQ(saidas.comandos.back().second.fase,FaseMissao::INDO_PARA_ENTREGA);
    
    std::cout << " -> Realizando entrega e desacoplamento final.\n";
    cycle(organizer,frota,saidas,{report(1,EstadoRobo::AGUARDANDO_DESCARGA,FaseMissao::ENTREGA_PRONTA,true), report(2,EstadoRobo::AGUARDANDO_DESCARGA,FaseMissao::ENTREGA_PRONTA,true)}); ASSERT_EQ(saidas.comandos.size(),8u);
    cycle(organizer,frota,saidas,{report(1,EstadoRobo::AGUARDANDO_DESCARGA,FaseMissao::DESACOPLANDO,false)}); EXPECT_NE(organizer.cargos()[0].estado,EstadoCarga::ENTREGUE);
    cycle(organizer,frota,saidas,{report(2,EstadoRobo::AGUARDANDO_DESCARGA,FaseMissao::DESACOPLANDO,false)}); EXPECT_EQ(organizer.cargos()[0].estado,EstadoCarga::ENTREGUE);
    std::cout << "[SUCESSO] LogisticsTest.WaitsForCompleteTeamAndAllArrivalReports passou!\n";
}

TEST(LogisticsTest, AddsNewlyFreeRobotsToPartialTeamBeforeCoupling) {
    auto map = layout(); EstadoFrota frota;
    registrarRobo(frota, 1); registrarRoboOcupado(frota, 2); registrarRoboOcupado(frota, 3);
    SaidasLogistica saidas; OrganizadorLogistica organizer(map,{load(3)});

    cycle(organizer, frota, saidas);
    ASSERT_EQ(saidas.comandos.size(), 1u);
    EXPECT_EQ(saidas.comandos.front().first, 1);
    cycle(organizer, frota, saidas,{report(1,EstadoRobo::AGUARDANDO_EQUIPE,FaseMissao::COLETA_PRONTA)});
    EXPECT_EQ(saidas.comandos.size(), 1u);

    liberarRobo(frota, 2);
    cycle(organizer, frota, saidas);
    ASSERT_EQ(saidas.comandos.size(), 2u);
    EXPECT_EQ(saidas.comandos.back().first, 2);
    cycle(organizer, frota, saidas,{report(2,EstadoRobo::AGUARDANDO_EQUIPE,FaseMissao::COLETA_PRONTA)});
    EXPECT_EQ(saidas.comandos.size(), 2u);

    liberarRobo(frota, 3);
    cycle(organizer, frota, saidas);
    ASSERT_EQ(saidas.comandos.size(), 3u);
    EXPECT_EQ(saidas.comandos.back().first, 3);
    cycle(organizer, frota, saidas,{report(3,EstadoRobo::AGUARDANDO_EQUIPE,FaseMissao::COLETA_PRONTA)});
    ASSERT_EQ(saidas.comandos.size(), 6u);
    for (std::size_t i = 3; i < saidas.comandos.size(); ++i)
        EXPECT_EQ(saidas.comandos[i].second.tipo, TipoMensagem::Acoplar);
    EXPECT_EQ(organizer.cargos()[0].estado, EstadoCarga::ACOPLANDO);
}

TEST(LogisticsTest, AssignsConcurrentCargoTeamsInFifoOrder) {
    auto map = layout(); EstadoFrota frota;
    for (int id = 1; id <= 6; ++id) registrarRobo(frota, id);
    auto primeira = load(4);
    auto segunda = load(4); segunda.id = 2;
    SaidasLogistica saidas; OrganizadorLogistica organizer(map,{primeira, segunda});

    cycle(organizer, frota, saidas);
    ASSERT_EQ(saidas.comandos.size(), 6u);
    for (std::size_t i = 0; i < 4; ++i) EXPECT_EQ(saidas.comandos[i].second.id_carga, 1);
    for (std::size_t i = 4; i < 6; ++i) EXPECT_EQ(saidas.comandos[i].second.id_carga, 2);
    ASSERT_EQ(organizer.missoes().size(), 2u);
    EXPECT_EQ(organizer.missoes()[0].integrantes.size(), 4u);
    EXPECT_EQ(organizer.missoes()[1].integrantes.size(), 2u);

    cycle(organizer, frota, saidas,{
        report(5,EstadoRobo::AGUARDANDO_EQUIPE,FaseMissao::COLETA_PRONTA,false,2),
        report(6,EstadoRobo::AGUARDANDO_EQUIPE,FaseMissao::COLETA_PRONTA,false,2)});
    EXPECT_EQ(saidas.comandos.size(), 6u);
    EXPECT_EQ(organizer.cargos()[1].estado, EstadoCarga::REUNINDO_EQUIPE);
}

TEST(LogisticsTest, SendsEveryIdleRobotBelowFullBatteryToChargingStation) {
    auto map = layout(); EstadoFrota frota;
    registrarRobo(frota, 1);
    definirBateria(frota, 1, 99);
    OrganizadorLogistica organizer(map, {});

    const auto resultado = organizer.processarCiclo({}, frota.capturarEstado());
    ASSERT_EQ(resultado.comandos.size(), 1u);
    EXPECT_EQ(resultado.comandos.front().first, 1);
    EXPECT_EQ(resultado.comandos.front().second.tipo, TipoMensagem::AtribuirMissao);
    EXPECT_EQ(resultado.comandos.front().second.fase, FaseMissao::INDO_PARA_RECARGA);
    EXPECT_EQ(resultado.comandos.front().second.destino, (Posicao{0, 0}));

    EXPECT_TRUE(organizer.processarCiclo({}, frota.capturarEstado()).comandos.empty());

    definirBateria(frota, 1, 100);
    EXPECT_TRUE(organizer.processarCiclo({}, frota.capturarEstado()).comandos.empty());
}

TEST(LogisticsTest, CancelsBeforeCouplingAndPreservesQueue) {
    std::cout << "\n[TESTE] LogisticsTest.CancelsBeforeCouplingAndPreservesQueue iniciado..." << std::unitbuf;
    auto map = layout(); EstadoFrota frota; registrarRobo(frota,1); registrarRobo(frota,2);
    SaidasLogistica saidas; OrganizadorLogistica organizer(map,{load(2)}); cycle(organizer,frota,saidas);
    
    std::cout << " -> Simulando falha antes do acoplamento e verificando cancelamento.\n";
    auto falha = report(1,EstadoRobo::COM_FALHA,FaseMissao::INDO_PARA_COLETA); falha.falha = true;
    auto estado = estadoRoboDaMensagem(falha); estado.sequencia = 3; frota.atualizar(estado); cycle(organizer,frota,saidas,{falha});
    EXPECT_EQ(organizer.cargos()[0].estado,EstadoCarga::REUNINDO_EQUIPE);
    EXPECT_EQ(organizer.missoes()[0].fase,FaseMissao::NENHUMA);
    ASSERT_EQ(organizer.missoes().size(),2u);
    EXPECT_EQ(organizer.missoes()[1].integrantes.size(),1u);
    EXPECT_EQ(saidas.comandos.back().first,2);
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
    EXPECT_THROW(organizer.retomarMissao(1, {}, frota.capturarEstado()), std::runtime_error);
    auto antigo = estadoRoboDaMensagem(falha); antigo.falha = false; antigo.estado = EstadoRobo::SUSPENSO; antigo.sequencia = 3;
    EXPECT_FALSE(frota.atualizar(antigo)); cycle(organizer,frota,saidas,{resume(1)});
    EXPECT_EQ(organizer.cargos()[0].estado,EstadoCarga::SUSPENSO);
    auto atual = estadoRoboDaMensagem(recuperado); atual.estado = EstadoRobo::SUSPENSO; atual.sequencia = 5;
    ASSERT_TRUE(frota.atualizar(atual));
    EXPECT_NO_THROW(organizer.retomarMissao(1, {}, frota.capturarEstado()));
    cycle(organizer,frota,saidas);
    ASSERT_EQ(organizer.cargos()[0].estado,EstadoCarga::EM_TRANSPORTE);
    EXPECT_EQ(std::count(saidas.eventos.begin(),saidas.eventos.end(),AvisoLogistica::TipoAviso::MissaoSuspensa),1);
    EXPECT_THROW(organizer.retomarMissao(1, {}, frota.capturarEstado()), std::runtime_error);
    EXPECT_THROW(organizer.retomarMissao(99, {}, frota.capturarEstado()), std::invalid_argument);
    std::cout << "[SUCESSO] LogisticsTest.ResumeRequiresCurrentSensorsAndIgnoresObsoleteFaults passou!\n";
}

TEST(LogisticsTest, AppendsDynamicCargoWithoutBypassingBlockedBayHead) {
    auto mapa = layout();
    EstadoFrota frota;
    registrarRobo(frota, 1);
    OrganizadorLogistica logistica(mapa, {load(2)});
    auto nova = load(1);
    nova.id = 2;
    nova.estado = EstadoCarga::ENTREGUE;
    nova.id_missao = 99;
    nova.motivo = "Antigo";
    logistica.adicionarCarga(nova);
    const auto inicial = logistica.processarCiclo({}, frota.capturarEstado());
    ASSERT_EQ(inicial.comandos.size(), 1u);
    EXPECT_EQ(inicial.comandos.front().second.id_carga, 1);
    ASSERT_EQ(logistica.cargos().size(), 2u);
    EXPECT_EQ(logistica.cargos()[1].estado, EstadoCarga::PENDENTE);
    EXPECT_EQ(logistica.cargos()[1].id_missao, 0);
    EXPECT_TRUE(logistica.cargos()[1].motivo.empty());
    EXPECT_THROW(logistica.adicionarCarga(nova), std::invalid_argument);
    nova.id = 3;
    nova.id_baia = 99;
    EXPECT_THROW(logistica.adicionarCarga(nova), std::invalid_argument);
    nova.id_baia = 1;
    nova.id_destino = 99;
    EXPECT_THROW(logistica.adicionarCarga(nova), std::invalid_argument);
    nova.id_destino = 1;
    nova.tamanho = 5;
    EXPECT_THROW(logistica.adicionarCarga(nova), std::invalid_argument);
    EXPECT_EQ(logistica.cargos().size(), 2u);
    registrarRobo(frota, 2);
    const auto resultado = logistica.processarCiclo({}, frota.capturarEstado());
    ASSERT_EQ(resultado.comandos.size(), 1u);
    EXPECT_EQ(resultado.comandos.front().second.id_carga, 1);
    EXPECT_EQ(resultado.comandos.front().first, 2);
}

TEST(LogisticsTest, AddsCargoBatchAtomicallyBeforeScheduling) {
    auto mapa = layout();
    EstadoFrota frota;
    registrarRobo(frota, 1);
    registrarRobo(frota, 2);
    OrganizadorLogistica logistica(mapa, {});
    auto primeira = load(1);
    primeira.id = 10;
    auto segunda = load(1);
    segunda.id = 11;
    auto invalida = load(1);
    invalida.id = 12;
    invalida.id_baia = 99;

    EXPECT_THROW(logistica.adicionarCargas({primeira, invalida}), std::invalid_argument);
    EXPECT_TRUE(logistica.cargos().empty());

    logistica.adicionarCargas({primeira, segunda});
    const auto resultado = logistica.processarCiclo({}, frota.capturarEstado());
    ASSERT_EQ(logistica.cargos().size(), 2u);
    ASSERT_EQ(resultado.comandos.size(), 2u);
    EXPECT_EQ(resultado.comandos[0].second.id_carga, 10);
    EXPECT_EQ(resultado.comandos[1].second.id_carga, 11);
}
