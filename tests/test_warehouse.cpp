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
    EXPECT_EQ(despachadas, (std::set<IdEntidade>{1, 2}));
}
