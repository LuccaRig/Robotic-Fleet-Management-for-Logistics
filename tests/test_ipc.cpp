#include <gtest/gtest.h>
#include "ipc_channel.hpp"
#include "fleet_state.hpp"
#include <sys/socket.h>
#include <unistd.h>

TEST(ProtocolTest, RoundTripsAllMissionMetadata) {
    Mensagem m; m.tipo = TipoMensagem::AtribuirMissao; m.id_robo = 7; m.geracao = 9; m.id_comando = 12; m.revisao = 12;
    m.id_missao = 3; m.id_carga = 4; m.rota = {{2,3},{3,3}}; m.integrantes = {1,2,3,4}; m.texto = "Missão de coleta";
    const auto copy = decodificarMensagem(codificarMensagem(m)); EXPECT_EQ(copy.id_robo,7); EXPECT_EQ(copy.geracao,9u); EXPECT_EQ(copy.revisao,12u);
    EXPECT_EQ(copy.rota,m.rota); EXPECT_EQ(copy.integrantes,m.integrantes); EXPECT_EQ(copy.texto,m.texto);
}
TEST(ProtocolTest, RejectsTruncationVersionAndOversize) {
    auto payload = codificarMensagem(Mensagem{}); payload.pop_back(); EXPECT_THROW(decodificarMensagem(payload),std::runtime_error);
    payload = codificarMensagem(Mensagem{}); payload[7] = 2; EXPECT_THROW(decodificarMensagem(payload),std::runtime_error);
    Mensagem huge; huge.texto.assign(TAMANHO_MAXIMO_MENSAGEM,'x'); EXPECT_THROW(codificarMensagem(huge),std::runtime_error);
}
TEST(IpcTest, ReassemblesFragmentedFramesAndDetectsEof) {
    int sockets[2]; ASSERT_EQ(socketpair(AF_UNIX,SOCK_STREAM,0,sockets),0); CanalIpc receiver(sockets[0]);
    Mensagem mensagem; mensagem.texto = "Fragmentada"; auto bytes = codificarMensagem(mensagem); const auto tamanho = static_cast<std::uint32_t>(bytes.size());
    std::vector<std::uint8_t> frame{std::uint8_t(tamanho>>24),std::uint8_t(tamanho>>16),std::uint8_t(tamanho>>8),std::uint8_t(tamanho)};
    frame.insert(frame.end(),bytes.begin(),bytes.end());
    ASSERT_EQ(write(sockets[1],frame.data(),2),2); EXPECT_FALSE(receiver.receber(std::chrono::milliseconds(1)));
    ASSERT_EQ(write(sockets[1],frame.data()+2,frame.size()-2),static_cast<ssize_t>(frame.size()-2));
    const auto decoded = receiver.receber(std::chrono::milliseconds(50)); ASSERT_TRUE(decoded); EXPECT_EQ(decoded->texto,mensagem.texto);
    close(sockets[1]); EXPECT_THROW(receiver.receber(std::chrono::milliseconds(50)),std::runtime_error);
}
TEST(IpcTest, RejectsOversizedHeader) {
    int sockets[2]; ASSERT_EQ(socketpair(AF_UNIX,SOCK_STREAM,0,sockets),0); CanalIpc receiver(sockets[0]);
    const unsigned char header[]{0,1,0,1}; ASSERT_EQ(write(sockets[1],header,4),4);
    EXPECT_THROW(receiver.receber(std::chrono::milliseconds(50)),std::runtime_error); close(sockets[1]);
}
TEST(FleetStateTest, RequiresRegisteredGenerationAndIncreasingSequence) {
    EstadoFrota frota; ASSERT_TRUE(frota.registrarRobo(1,3,{0,0}));
    EstadoAtualRobo s; s.id = 1; s.geracao = 3; s.sequencia = 1; s.conectado = true; s.disponivel = true;
    EXPECT_TRUE(frota.atualizar(s)); EXPECT_FALSE(frota.atualizar(s)); s.geracao = 2; s.sequencia = 99; EXPECT_FALSE(frota.atualizar(s));
    EXPECT_FALSE(frota.desconectar(1,2)); EXPECT_TRUE(frota.desconectar(1,3)); EXPECT_FALSE(frota.obter(1)->disponivel);
}
TEST(FleetStateTest, RejectsStateRegressionEvenWithHigherTransportSequence) {
    EstadoFrota frota; frota.registrarRobo(1,1,{0,0});
    EstadoAtualRobo s; s.id = 1; s.geracao = 1; s.sequencia = 1; s.revisao = 5; s.sequencia_sensor = 4; s.conectado = true;
    ASSERT_TRUE(frota.atualizar(s)); s.sequencia = 2; s.revisao = 4; EXPECT_FALSE(frota.atualizar(s));
    s.revisao = 5; s.sequencia_sensor = 3; EXPECT_FALSE(frota.atualizar(s));
    frota.desconectar(1,1); s.sequencia_sensor = 5; EXPECT_FALSE(frota.atualizar(s));
}
TEST(FleetStateTest, LocalSensorUpdatesPreserveTransportSequenceAndRejectOldReadings) {
    EstadoFrota frota; ASSERT_TRUE(frota.registrarRobo(1,2,{0,0}));
    EstadoAtualRobo estado; estado.id = 1; estado.geracao = 2; estado.sequencia = 1; estado.conectado = true; estado.bateria = 80;
    ASSERT_TRUE(frota.atualizar(estado));
    auto leitura = estado; leitura.sequencia = 0; leitura.sequencia_sensor = 2; leitura.bateria = 40;
    ASSERT_TRUE(frota.atualizarLeituraSensores(leitura));
    ASSERT_TRUE(frota.obter(1)); EXPECT_EQ(frota.obter(1)->sequencia,1u); EXPECT_EQ(frota.obter(1)->bateria,40);
    auto status = leitura; status.sequencia = 2; status.estado = EstadoRobo::SUSPENSO;
    ASSERT_TRUE(frota.atualizar(status));
    leitura.sequencia_sensor = 1; leitura.bateria = 100; EXPECT_FALSE(frota.atualizarLeituraSensores(leitura));
    EXPECT_EQ(frota.obter(1)->bateria,40);
}
TEST(IpcTest, ZeroTimeoutConsumesQueuedFrameBeforePeerExit) {
    int sockets[2]; ASSERT_EQ(socketpair(AF_UNIX,SOCK_STREAM,0,sockets),0);
    CanalIpc receiver(sockets[0]);
    {
        CanalIpc emissor(sockets[1]); Mensagem mensagem; mensagem.tipo = TipoMensagem::RoboEncerrado; emissor.enviar(mensagem);
    }
    const auto mensagem = receiver.receber(std::chrono::milliseconds(0));
    ASSERT_TRUE(mensagem); EXPECT_EQ(mensagem->tipo,TipoMensagem::RoboEncerrado);
    EXPECT_THROW(receiver.receber(std::chrono::milliseconds(0)),std::runtime_error);
}
