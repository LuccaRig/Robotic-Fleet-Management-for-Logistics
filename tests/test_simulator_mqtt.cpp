#include <gtest/gtest.h>
#include "hardware_mqtt.hpp"
#include "runtime.hpp"
#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

extern char** environ;

namespace {
class MqttSimulatorIntegration : public ::testing::Test {
protected:
    pid_t broker_ = -1, simulador_ = -1;
    std::filesystem::path configuracao_, banco_;
    std::optional<std::string> host_anterior_, porta_anterior_;

    static int iniciar(pid_t& pid, std::vector<std::string> argumentos) {
        std::vector<char*> argv;
        for (auto& argumento : argumentos) argv.push_back(argumento.data());
        argv.push_back(nullptr);
        return posix_spawn(&pid, argv.front(), nullptr, nullptr, argv.data(), environ);
    }

    void SetUp() override {
        if (const auto host = std::getenv("MQTT_HOST")) host_anterior_ = host;
        if (const auto porta = std::getenv("MQTT_PORT")) porta_anterior_ = porta;
        const int fd = socket(AF_INET, SOCK_STREAM, 0);
        ASSERT_GE(fd, 0);
        sockaddr_in endereco{};
        endereco.sin_family = AF_INET;
        endereco.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        const auto vinculado = bind(fd, reinterpret_cast<sockaddr*>(&endereco), sizeof(endereco));
        socklen_t tamanho = sizeof(endereco);
        const auto consultado = getsockname(fd, reinterpret_cast<sockaddr*>(&endereco), &tamanho);
        close(fd);
        ASSERT_EQ(vinculado, 0);
        ASSERT_EQ(consultado, 0);
        const auto porta = std::to_string(ntohs(endereco.sin_port));
        setenv("MQTT_HOST", "127.0.0.1", 1);
        setenv("MQTT_PORT", porta.c_str(), 1);
        std::filesystem::create_directories(TEST_DB_DIRECTORY);
        configuracao_ = std::filesystem::path(TEST_DB_DIRECTORY) / ("mqtt_" + std::to_string(getpid()) + ".conf");
        banco_ = std::filesystem::path(TEST_DB_DIRECTORY) / ("mqtt_" + std::to_string(getpid()) + ".db");
        { std::ofstream arquivo(configuracao_); arquivo << "listener " << porta << " 127.0.0.1\nallow_anonymous true\npersistence false\n"; }
        ASSERT_EQ(iniciar(broker_, {MOSQUITTO_EXECUTABLE_PATH, "-c", configuracao_.string()}), 0);
        const auto prazo = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        bool pronto = false;
        while (!pronto && std::chrono::steady_clock::now() < prazo) {
            const int tentativa = socket(AF_INET, SOCK_STREAM, 0);
            pronto = connect(tentativa, reinterpret_cast<sockaddr*>(&endereco), sizeof(endereco)) == 0;
            close(tentativa);
            if (!pronto) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        ASSERT_TRUE(pronto);
        ASSERT_EQ(iniciar(simulador_, {SIMULATOR_EXECUTABLE_PATH, "--simulador", "--cenario", "processos", "--mapa", "config/galpao.txt"}), 0);
    }

    void TearDown() override {
        for (const auto pid : {simulador_, broker_}) {
            if (pid <= 0) continue;
            kill(pid, SIGTERM);
            while (waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {}
        }
        if (!configuracao_.empty()) std::filesystem::remove(configuracao_);
        if (!banco_.empty()) std::filesystem::remove(banco_);
        if (host_anterior_) setenv("MQTT_HOST", host_anterior_->c_str(), 1); else unsetenv("MQTT_HOST");
        if (porta_anterior_) setenv("MQTT_PORT", porta_anterior_->c_str(), 1); else unsetenv("MQTT_PORT");
    }
};

class LeiturasSimulador {
public:
    CanalSimulacaoMqtt canal;
    std::map<int, Mensagem> anuncios, sensores;
    LeiturasSimulador() { canal.iniciar(); }
    bool esperar(const std::function<bool()>& condicao) {
        const auto prazo = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (std::chrono::steady_clock::now() < prazo) {
            for (const auto& mensagem : canal.receber()) {
                if (mensagem.tipo == TipoMensagem::SaudacaoRobo) anuncios[mensagem.id_robo] = mensagem;
                if (mensagem.tipo == TipoMensagem::AtualizarSensores) sensores[mensagem.id_robo] = mensagem;
            }
            if (condicao()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return false;
    }
};

TEST_F(MqttSimulatorIntegration, PreservesTeamBarrierAndIgnoresRepeatedSetpoints) {
    LeiturasSimulador simulacao;
    ASSERT_TRUE(simulacao.esperar([&] { return simulacao.anuncios.count(1); }));
    auto novo = simulacao.anuncios.at(1);
    novo.id_robo = 2;
    ASSERT_TRUE(simulacao.canal.enviar(novo));
    ASSERT_TRUE(simulacao.esperar([&] { return simulacao.sensores.size() == 2; }));
    EXPECT_NE(simulacao.anuncios.at(1).geracao, simulacao.anuncios.at(2).geracao);
    for (int id : {1, 2}) {
        auto comando = simulacao.anuncios.at(id);
        comando.tipo = TipoMensagem::AtribuirMissao;
        comando.id_comando = 1;
        comando.id_missao = 1;
        comando.integrantes = {1, 2};
        comando.fase = FaseMissao::ACOPLANDO;
        ASSERT_TRUE(simulacao.canal.enviar(comando));
        comando.id_comando = 2;
        comando.fase = FaseMissao::INDO_PARA_ENTREGA;
        ASSERT_TRUE(simulacao.canal.enviar(comando));
    }
    ASSERT_TRUE(simulacao.esperar([&] {
        return simulacao.sensores.at(1).revisao == 2 && simulacao.sensores.at(2).revisao == 2;
    }));
    auto primeiro = simulacao.sensores.at(1);
    primeiro.tipo = TipoMensagem::DestinoNavegacao;
    primeiro.destino = {3, 5};
    primeiro.passo = 1;
    ASSERT_TRUE(simulacao.canal.enviar(primeiro));
    ASSERT_TRUE(simulacao.esperar([&] { return simulacao.sensores.at(1).sequencia_sensor > primeiro.sequencia_sensor; }));
    EXPECT_EQ(simulacao.sensores.at(1).posicao, (Posicao{2, 5}));
    auto segundo = simulacao.sensores.at(2);
    segundo.tipo = TipoMensagem::DestinoNavegacao;
    segundo.destino = primeiro.destino;
    segundo.passo = 1;
    ASSERT_TRUE(simulacao.canal.enviar(segundo));
    ASSERT_TRUE(simulacao.esperar([&] {
        return simulacao.sensores.at(1).posicao == primeiro.destino && simulacao.sensores.at(2).posicao == segundo.destino;
    }));
    const auto sequencia = simulacao.sensores.at(2).sequencia_sensor;
    ASSERT_TRUE(simulacao.canal.enviar(primeiro));
    ASSERT_TRUE(simulacao.canal.enviar(segundo));
    ASSERT_TRUE(simulacao.esperar([&] { return simulacao.sensores.at(2).sequencia_sensor > sequencia; }));
    EXPECT_EQ(simulacao.sensores.at(1).bateria, 99);
    EXPECT_EQ(simulacao.sensores.at(2).bateria, 99);
}

TEST_F(MqttSimulatorIntegration, RechargesOnItsOwnCycleWithoutPollingRequests) {
    LeiturasSimulador simulacao;
    ASSERT_TRUE(simulacao.esperar([&] { return simulacao.sensores.count(1); }));
    auto comando = simulacao.sensores.at(1);
    comando.tipo = TipoMensagem::AtualizarSensores;
    comando.id_comando = 10;
    comando.texto = "bateria";
    comando.bateria = 20;
    ASSERT_TRUE(simulacao.canal.enviar(comando));
    ASSERT_TRUE(simulacao.esperar([&] { return simulacao.sensores.at(1).bateria == 20; }));
    comando.tipo = TipoMensagem::AtribuirMissao;
    comando.id_comando = 1;
    comando.fase = FaseMissao::INDO_PARA_RECARGA;
    comando.destino = {2, 1};
    ASSERT_TRUE(simulacao.canal.enviar(comando));
    ASSERT_TRUE(simulacao.esperar([&] { return simulacao.sensores.at(1).revisao == 1; }));
    for (std::uint64_t passo = 1; passo <= 4; ++passo) {
        auto movimento = simulacao.sensores.at(1);
        movimento.tipo = TipoMensagem::DestinoNavegacao;
        movimento.destino = {2, 5 - static_cast<int>(passo)};
        movimento.passo = passo;
        ASSERT_TRUE(simulacao.canal.enviar(movimento));
        ASSERT_TRUE(simulacao.esperar([&] { return simulacao.sensores.at(1).posicao == movimento.destino; }));
    }
    const auto bateria = simulacao.sensores.at(1).bateria;
    ASSERT_TRUE(simulacao.esperar([&] { return simulacao.sensores.at(1).bateria > bateria; }));
}

TEST_F(MqttSimulatorIntegration, CompletesConsecutiveMissionsWithRealRobotProcessesAndRemoteSensors) {
    Cenario cenario{"mqtt", carregarMapa("config/galpao.txt"), {}, {{1, {2, 5}}, {2, {3, 5}}}};
    Carga carga;
    carga.id = 1;
    carga.tamanho = 2;
    carga.id_baia = carga.id_destino = 1;
    cenario.cargos.push_back(carga);
    carga.id = 2;
    cenario.cargos.push_back(carga);
    AplicacaoCentral central(SIMULATOR_EXECUTABLE_PATH, banco_.string(), std::move(cenario), true);
    central.iniciar();
    const auto prazo = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (!central.concluido() && std::chrono::steady_clock::now() < prazo) {
        central.avancarCiclo();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(central.concluido());
    EXPECT_EQ(central.processos().size(), 2u);
    EXPECT_EQ(central.cargos().front().estado, EstadoCarga::ENTREGUE);
    EXPECT_EQ(central.cargos().back().estado, EstadoCarga::ENTREGUE);
    EXPECT_EQ(central.quantidadeEventos(TipoEvento::CARGA_ENTREGUE), 2u);
    for (const auto& robo : central.frota().capturarEstado()) {
        EXPECT_GT(robo.distancia, 0u);
        EXPECT_LT(robo.bateria, 100);
    }
    central.parar();
    EXPECT_TRUE(central.processos().empty());
}
}
