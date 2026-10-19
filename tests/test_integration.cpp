#include <gtest/gtest.h>
#include "runtime.hpp"
#include "scenarios.hpp"
#include <filesystem>
#include <thread>
#include <unistd.h>

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
    AplicacaoCentral runtime(SIMULATOR_EXECUTABLE_PATH,path,criarCenario(GetParam())); RoteiroCenario script(GetParam()); runtime.iniciar();
    const auto prazo = std::chrono::steady_clock::now() + std::chrono::seconds(120);
    while (!script.concluido(runtime) && std::chrono::steady_clock::now() < prazo) { runtime.avancarCiclo(); script.avancarCiclo(runtime); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    ASSERT_TRUE(script.concluido(runtime)); runtime.parar(); EXPECT_TRUE(runtime.processos().empty());
    sqlite3* db = nullptr; ASSERT_EQ(sqlite3_open_v2(path.c_str(),&db,SQLITE_OPEN_READONLY,nullptr),SQLITE_OK);
    sqlite3_stmt* statement = nullptr;
    ASSERT_EQ(sqlite3_prepare_v2(db,"SELECT COUNT(*) FROM runs WHERE status='encerrada' AND ended_at_ms IS NOT NULL",-1,&statement,nullptr),SQLITE_OK);
    ASSERT_EQ(sqlite3_step(statement),SQLITE_ROW); EXPECT_EQ(sqlite3_column_int(statement,0),1); sqlite3_finalize(statement);
    if (std::string(GetParam()) != "processos") {
        ASSERT_EQ(sqlite3_prepare_v2(db,"SELECT COUNT(*) FROM cargo_members",-1,&statement,nullptr),SQLITE_OK);
        ASSERT_EQ(sqlite3_step(statement),SQLITE_ROW); EXPECT_GT(sqlite3_column_int(statement,0),0); sqlite3_finalize(statement);
    }
    sqlite3_close(db);
}
INSTANTIATE_TEST_SUITE_P(Cenarios,Etapa1Integration,::testing::Values("base","cooperativo","disponibilidade","passagens","rota-equipe","falhas","recarga","processos"));
TEST(Etapa1Shutdown, StopsActiveWorkersAndDrainsAcceptedEvents) {
    std::filesystem::create_directories(TEST_DB_DIRECTORY);
    const auto path = std::string(TEST_DB_DIRECTORY) + "/encerramento_" + std::to_string(getpid()) + ".db"; std::filesystem::remove(path);
    {
        AplicacaoCentral runtime(SIMULATOR_EXECUTABLE_PATH,path,criarCenario("base")); runtime.iniciar();
        const auto prazo = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while ((!runtime.frota().obter(1) || !runtime.frota().obter(1)->conectado) && std::chrono::steady_clock::now() < prazo) { runtime.avancarCiclo(); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
        ASSERT_TRUE(runtime.frota().obter(1)); ASSERT_TRUE(runtime.frota().obter(1)->conectado);
        ASSERT_EQ(runtime.processos().size(),1u); EXPECT_NE(runtime.processos()[0].pid,getpid());
        runtime.parar(); EXPECT_TRUE(runtime.processos().empty()); EXPECT_GE(runtime.quantidadeEventos(TipoEvento::ROBO_ENCERRADO),1u);
    }
    std::filesystem::remove(path);
}
