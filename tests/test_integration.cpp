#include <gtest/gtest.h>
#include "runtime.hpp"
#include "scenarios.hpp"
#include <filesystem>
#include <thread>
#include <unistd.h>
#include <iostream>
#include <stdexcept>

class Etapa1Integration : public ::testing::TestWithParam<const char*> {
protected:
    std::string path;
    void SetUp() override {
        std::filesystem::create_directories(TEST_DB_DIRECTORY);
        path = std::string(TEST_DB_DIRECTORY) + "/integracao_" + GetParam() + "_" + std::to_string(getpid()) + ".db";
        std::filesystem::remove(path);
    }
    void TearDown() override { std::filesystem::remove(path); }
};

TEST_P(Etapa1Integration, CompletesScenarioAndReapsEveryWorker) {
    std::cout << "\n[INTEGRAÇÃO] Executando cenário: [" << GetParam() << "] (PID: " << getpid() << ")\n" << std::unitbuf;
    
    AplicacaoCentral runtime(SIMULATOR_EXECUTABLE_PATH,path,criarCenario(GetParam())); RoteiroCenario script(GetParam()); runtime.iniciar();
    const auto prazo = std::chrono::steady_clock::now() + std::chrono::seconds(120);
    int ciclos = 0;
    
    while (!script.concluido(runtime) && std::chrono::steady_clock::now() < prazo) { 
        runtime.avancarCiclo(); 
        script.avancarCiclo(runtime); 
        ciclos++;
        if (ciclos % 50 == 0) {
            std::cout << " -> Cenário [" << GetParam() << "] em andamento... (Ciclos: " << ciclos << ")\n";
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10)); 
    }
    
    ASSERT_TRUE(script.concluido(runtime)); runtime.parar(); EXPECT_TRUE(runtime.processos().empty());
    std::cout << " -> Cenário [" << GetParam() << "] concluído com sucesso após " << ciclos << " ciclos. Validando base de dados...\n";

    sqlite3* db = nullptr; ASSERT_EQ(sqlite3_open_v2(path.c_str(),&db,SQLITE_OPEN_READONLY,nullptr),SQLITE_OK);
    sqlite3_stmt* statement = nullptr;
    ASSERT_EQ(sqlite3_prepare_v2(db,"SELECT COUNT(*) FROM runs WHERE status='encerrada' AND ended_at_ms IS NOT NULL",-1,&statement,nullptr),SQLITE_OK);
    ASSERT_EQ(sqlite3_step(statement),SQLITE_ROW); EXPECT_EQ(sqlite3_column_int(statement,0),1); sqlite3_finalize(statement);
    if (std::string(GetParam()) != "processos") {
        ASSERT_EQ(sqlite3_prepare_v2(db,"SELECT COUNT(*) FROM cargo_members",-1,&statement,nullptr),SQLITE_OK);
        ASSERT_EQ(sqlite3_step(statement),SQLITE_ROW); EXPECT_GT(sqlite3_column_int(statement,0),0); sqlite3_finalize(statement);
    }
    sqlite3_close(db);
    std::cout << "[SUCESSO] Cenário de integração [" << GetParam() << "] validado por completo!\n";
}

INSTANTIATE_TEST_SUITE_P(Cenarios,Etapa1Integration,::testing::Values("base","cooperativo","disponibilidade","passagens","rota-equipe","falhas","recarga","processos"));

TEST(Etapa1Shutdown, StopsActiveWorkersAndDrainsAcceptedEvents) {
    std::cout << "\n[TESTE] Etapa1Shutdown.StopsActiveWorkersAndDrainsAcceptedEvents iniciado...\n" << std::unitbuf;
    std::filesystem::create_directories(TEST_DB_DIRECTORY);
    const auto path = std::string(TEST_DB_DIRECTORY) + "/encerramento_" + std::to_string(getpid()) + ".db"; std::filesystem::remove(path);
    {
        AplicacaoCentral runtime(SIMULATOR_EXECUTABLE_PATH,path,criarCenario("base")); runtime.iniciar();
        const auto prazo = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while ((!runtime.frota().obter(1) || !runtime.frota().obter(1)->conectado) && std::chrono::steady_clock::now() < prazo) { runtime.avancarCiclo(); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
        ASSERT_TRUE(runtime.frota().obter(1)); ASSERT_TRUE(runtime.frota().obter(1)->conectado);
        ASSERT_EQ(runtime.processos().size(),1u); EXPECT_NE(runtime.processos()[0].pid,getpid());
        
        std::cout << " -> Parando runtime e drenando eventos aceitos.\n";
        runtime.parar(); EXPECT_TRUE(runtime.processos().empty()); EXPECT_GE(runtime.quantidadeEventos(TipoEvento::ROBO_ENCERRADO),1u);
    }
    std::filesystem::remove(path);
    std::cout << "[SUCESSO] Etapa1Shutdown.StopsActiveWorkersAndDrainsAcceptedEvents passou!\n";
}

TEST(Etapa2Manual, ControlsRealWorkerAndPreservesPassageReservations) {
    std::filesystem::create_directories(TEST_DB_DIRECTORY);
    const auto banco = std::string(TEST_DB_DIRECTORY) + "/manual_" + std::to_string(getpid()) + ".db";
    std::filesystem::remove(banco);
    AplicacaoCentral runtime(SIMULATOR_EXECUTABLE_PATH, banco, criarCenario("processos"));
    runtime.iniciar();
    const auto esperar = [&](auto condicao) {
        const auto prazo = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (std::chrono::steady_clock::now() < prazo) {
            runtime.avancarCiclo();
            if (condicao()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return false;
    };
    ASSERT_TRUE(esperar([&] {
        const auto robo = runtime.frota().obter(1);
        return robo && robo->conectado;
    }));
    ComandoOperador comando;
    comando.id = "manual-1";
    comando.id_robo = 1;
    comando.geracao = runtime.frota().obter(1)->geracao;
    comando.acao = AcaoOperador::DefinirDestino;
    comando.destino = {8, 5};
    EXPECT_THROW(runtime.executarComandoOperador(comando), std::runtime_error);
    comando.acao = AcaoOperador::DefinirModo;
    comando.modo = ModoOperacao::MANUAL;
    runtime.executarComandoOperador(comando);
    ASSERT_TRUE(esperar([&] {
        return runtime.frota().obter(1)->modo == ModoOperacao::MANUAL && runtime.quantidadeEventos(TipoEvento::MODO_OPERACAO_ALTERADO) == 1;
    }));
    EXPECT_FALSE(runtime.frota().obter(1)->disponivel);

    comando.id = "destino-1";
    comando.acao = AcaoOperador::DefinirDestino;
    comando.destino = {0, 0};
    EXPECT_THROW(runtime.executarComandoOperador(comando), std::runtime_error);
    comando.destino = {8, 5};
    ++comando.geracao;
    EXPECT_THROW(runtime.executarComandoOperador(comando), std::runtime_error);
    --comando.geracao;
    runtime.executarComandoOperador(comando);
    EXPECT_THROW(runtime.executarComandoOperador(comando), std::runtime_error);
    ASSERT_TRUE(esperar([&] {
        const auto robo = runtime.frota().obter(1);
        return robo->posicao == comando.destino && robo->estado == EstadoRobo::DISPONIVEL && robo->fase == FaseMissao::NENHUMA;
    }));
    EXPECT_EQ(runtime.frota().obter(1)->modo, ModoOperacao::MANUAL);
    EXPECT_FALSE(runtime.frota().obter(1)->disponivel);
    for (const auto& trecho : runtime.passagens()) {
        EXPECT_LE(trecho.maximo, trecho.capacidade);
        EXPECT_EQ(trecho.ocupado, 0);
        if (trecho.id == 1) EXPECT_GT(trecho.maximo, 0);
    }
    comando.id = "automatico-1";
    comando.acao = AcaoOperador::DefinirModo;
    comando.modo = ModoOperacao::AUTOMATICO;
    runtime.executarComandoOperador(comando);
    ASSERT_TRUE(esperar([&] {
        return runtime.frota().obter(1)->modo == ModoOperacao::AUTOMATICO && runtime.quantidadeEventos(TipoEvento::MODO_OPERACAO_ALTERADO) == 2;
    }));
    EXPECT_TRUE(runtime.frota().obter(1)->disponivel);
    EXPECT_EQ(runtime.quantidadeEventos(TipoEvento::MODO_OPERACAO_ALTERADO), 2u);
    EXPECT_EQ(runtime.quantidadeEventos(TipoEvento::DESTINO_MANUAL_DEFINIDO), 1u);
    runtime.parar();
    EXPECT_TRUE(runtime.processos().empty());
    std::filesystem::remove(banco);
}
