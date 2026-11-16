#include <gtest/gtest.h>
#include "route_planner.hpp"
#include "scenarios.hpp"
#include <filesystem>
#include <fstream>
#include <set>

TEST(WarehouseGridTest, RejectsInvalidLayoutAndKeepsRobotOccupationSeparate) {
    EXPECT_THROW(MapaGalpao(0,3),std::invalid_argument);
    EXPECT_THROW((MapaGalpao(std::vector<std::vector<int>>{{0,0},{0}})),std::invalid_argument);
    EXPECT_THROW((MapaGalpao(std::vector<std::vector<int>>{{1}})),std::invalid_argument);
    MapaGalpao map(3,3); EXPECT_THROW(map.definirCelula(1,1,EstadoCelula::ROBO),std::invalid_argument);
    EXPECT_EQ(map.obterCelula(-1,0),EstadoCelula::OBSTACULO);
}
TEST(RoutePlannerTest, AvoidsWallsAndInsufficientCapacity) {
    MapaGalpao map(7,5);
    for (int y = 0; y < 5; ++y) map.definirCelula(3,y,EstadoCelula::OBSTACULO);
    map.definirCelula(3,1,EstadoCelula::LIVRE); map.adicionarTrecho({1,1,{{3,1}}});
    map.definirCelula(3,4,EstadoCelula::LIVRE); map.adicionarTrecho({2,4,{{3,4}}});
    PlanejadorRotas planner(map); auto solo = planner.calcularRota({1,1},{5,1},1); auto team = planner.calcularRota({1,1},{5,1},4);
    ASSERT_TRUE(solo); ASSERT_TRUE(team); EXPECT_LT(solo->size(),team->size());
    for (auto point : *team) EXPECT_NE(point,(Posicao{3,1}));
    auto empty = planner.calcularRota({1,1},{1,1}); ASSERT_TRUE(empty); EXPECT_TRUE(empty->empty());
    EXPECT_FALSE(planner.calcularRota({-1,0},{1,1}));
}
TEST(RoutePlannerTest, DistinguishesUnreachableCharger) {
    MapaGalpao map(3,3); PlanejadorRotas planner(map); EXPECT_FALSE(planner.carregadorMaisProximo({1,1}));
    map.definirCelula(0,0,EstadoCelula::ESTACAO_RECARGA); ASSERT_TRUE(planner.carregadorMaisProximo({1,1}));
}

TEST(WarehouseGridTest, LoadsSharedScenarioMapsWithTheirMetadata) {
    const auto mapa = carregarMapa("config/galpao.txt");
    EXPECT_EQ(mapa.largura(), 21);
    EXPECT_EQ(mapa.altura(), 15);
    EXPECT_EQ(mapa.baias().size(), 2u);
    EXPECT_EQ(mapa.destinos().size(), 2u);
    EXPECT_EQ(mapa.carregadores().size(), 2u);
    EXPECT_EQ(mapa.passagens().size(), 6u);
    EXPECT_EQ(mapa.obterCelula(6, 2), EstadoCelula::OBSTACULO);
    EXPECT_EQ(mapa.passagens().at(1).cells.front(), (Posicao{6, 3}));
    EXPECT_EQ(mapa.passagens().at(3).capacidade, 3);
    EXPECT_EQ(mapa.passagens().at(13).capacidade, 4);
    const auto passagens = carregarMapa("config/galpao-passagens.txt");
    EXPECT_EQ(passagens.baias().size(), 7u);
    EXPECT_EQ(passagens.destinos().at(3).posicao, (Posicao{2, 3}));
}

TEST(ScenarioTest, ScaleScenarioUsesLargeMapAndFifteenRobots) {
    const auto cenario = criarCenario("escala");
    EXPECT_EQ(cenario.galpao.largura(), 32);
    EXPECT_EQ(cenario.galpao.altura(), 20);
    EXPECT_EQ(cenario.galpao.baias().size(), 4u);
    EXPECT_EQ(cenario.galpao.destinos().size(), 4u);
    EXPECT_EQ(cenario.galpao.carregadores().size(), 4u);
    EXPECT_EQ(cenario.galpao.passagens().size(), 4u);
    ASSERT_EQ(cenario.robos.size(), 15u);
    ASSERT_EQ(cenario.cargos.size(), 8u);
    int total_robos_necessarios = 0;
    for (const auto& carga : cenario.cargos) total_robos_necessarios += carga.tamanho;
    EXPECT_EQ(total_robos_necessarios, 15);

    std::set<int> ids_robos;
    for (const auto& robo : cenario.robos) {
        EXPECT_TRUE(ids_robos.insert(robo.id).second);
        EXPECT_EQ(cenario.galpao.obterCelula(robo.posicao.x, robo.posicao.y), EstadoCelula::LIVRE);
    }

    OrganizadorLogistica logistica(cenario.galpao, cenario.cargos);
    std::vector<EstadoAtualRobo> frota;
    for (const auto& inicial : cenario.robos) {
        EstadoAtualRobo robo;
        robo.id = inicial.id;
        robo.posicao = inicial.posicao;
        robo.conectado = robo.disponivel = true;
        frota.push_back(robo);
    }
    const auto ciclo = logistica.processarCiclo({}, frota);
    ASSERT_EQ(ciclo.comandos.size(), cenario.robos.size());
    std::set<int> despachados;
    for (const auto& comando : ciclo.comandos) {
        EXPECT_EQ(comando.second.tipo, TipoMensagem::AtribuirMissao);
        EXPECT_EQ(comando.second.fase, FaseMissao::INDO_PARA_COLETA);
        EXPECT_TRUE(despachados.insert(comando.first).second);
    }
    EXPECT_EQ(despachados, ids_robos);
}

TEST(ScenarioTest, PassageStressScenarioUsesOneHundredRobotsAndSixtyMixedCargos) {
    const auto cenario = criarCenario("estresse-passagens");
    PlanejadorRotas planner(cenario.galpao);
    EXPECT_EQ(cenario.galpao.largura(), 50);
    EXPECT_EQ(cenario.galpao.altura(), 30);
    ASSERT_EQ(cenario.galpao.passagens().size(), 4u);
    for (int id = 1; id <= 4; ++id) {
        EXPECT_EQ(cenario.galpao.passagens().at(id).capacidade, id);
    }

    ASSERT_EQ(cenario.robos.size(), 100u);
    std::set<int> ids_robos;
    for (const auto& robo : cenario.robos) {
        EXPECT_TRUE(ids_robos.insert(robo.id).second);
        EXPECT_EQ(cenario.galpao.obterCelula(robo.posicao.x, robo.posicao.y), EstadoCelula::LIVRE);
    }
    EXPECT_EQ(*ids_robos.begin(), 1);
    EXPECT_EQ(*ids_robos.rbegin(), 100);

    ASSERT_EQ(cenario.cargos.size(), 60u);
    std::set<int> tamanhos;
    std::map<int, int> frequencias;
    for (const auto& cargo : cenario.cargos) {
        tamanhos.insert(cargo.tamanho);
        ++frequencias[cargo.tamanho];
        EXPECT_EQ(cargo.id_baia, cargo.id_destino);
        EXPECT_GE(cargo.tamanho, 1);
        EXPECT_LE(cargo.tamanho, 4);
    }
    EXPECT_EQ(tamanhos, (std::set<int>{1, 2, 3, 4}));
    EXPECT_EQ(frequencias, (std::map<int, int>{{1, 15}, {2, 15}, {3, 15}, {4, 15}}));
    for (const auto& cargo : cenario.cargos) {
        const auto& origem = cenario.galpao.baias().at(cargo.id_baia).posicao;
        const auto& destino = cenario.galpao.destinos().at(cargo.id_destino).posicao;
        const auto rota = planner.calcularRota(origem, destino, cargo.tamanho);
        ASSERT_TRUE(rota);
        bool cruzou_passagem = false;
        for (const auto posicao : *rota) {
            if (const auto* passagem = cenario.galpao.trechoEm(posicao)) {
                cruzou_passagem = true;
                EXPECT_EQ(passagem->capacidade, cargo.tamanho);
            }
        }
        EXPECT_TRUE(cruzou_passagem) << "carga " << cargo.id;
    }
}

TEST(WarehouseGridTest, RejectsMalformedMapFiles) {
    std::filesystem::create_directories(TEST_DB_DIRECTORY);
    const auto caminho = std::string(TEST_DB_DIRECTORY) + "/mapa_invalido.txt";
    for (const std::string conteudo : {"0 3\n", "3 3 sobra\n", "3 3\nbaia 1 1\n", "3 3\nparede 0 0 3 2\n",
            "3 3\npassagem 1 5 1 1 1\n", "3 3\npassagem 1 1 2 1 1 1 1\n", "3 3\ncarregador 1 1 sobra\n", "3 3\ndesconhecido 1\n"}) {
        { std::ofstream arquivo(caminho); arquivo << conteudo; }
        EXPECT_THROW(carregarMapa(caminho), std::invalid_argument) << conteudo;
    }
    std::filesystem::remove(caminho);
    EXPECT_THROW(carregarMapa(caminho), std::runtime_error);
}

TEST(ScenarioTest, StartsCooperativeDemoWithIndependentQueuesAndEveryCargoSize) {
    const auto cenario = criarCenario("cooperativo");
    OrganizadorLogistica logistica(cenario.galpao, cenario.cargos);
    std::set<int> tamanhos;
    std::map<int, std::vector<IdEntidade>> filas;
    for (const auto& carga : cenario.cargos) {
        tamanhos.insert(carga.tamanho);
        filas[carga.id_baia].push_back(carga.id);
    }
    EXPECT_EQ(tamanhos, (std::set<int>{1, 2, 3, 4}));
    EXPECT_EQ(filas.size(), cenario.galpao.baias().size());
    EXPECT_EQ(filas.at(1), (std::vector<IdEntidade>{1, 3}));
    EXPECT_EQ(filas.at(2), (std::vector<IdEntidade>{2, 4}));
    std::vector<EstadoAtualRobo> frota;
    for (const auto& inicial : cenario.robos) {
        EstadoAtualRobo robo;
        robo.id = inicial.id;
        robo.posicao = inicial.posicao;
        robo.conectado = robo.disponivel = true;
        frota.push_back(robo);
    }
    const auto ciclo = logistica.processarCiclo({}, frota);
    std::set<IdEntidade> despachadas;
    for (const auto& comando : ciclo.comandos) despachadas.insert(comando.second.id_carga);
    EXPECT_EQ(despachadas, (std::set<IdEntidade>{1, 2, 3}));
}
