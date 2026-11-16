#include <gtest/gtest.h>
#include "mqtt_integration.hpp"
#include <stdexcept>

namespace {
const std::string pedido = R"({"id":"modo-1","acao":"definir_modo","id_robo":1,"geracao":3,"modo":"manual"})";
}

TEST(MqttCommandsTest, DecodesModeAndCoordinateDestination) {
    const auto modo = decodificarComandoOperador(pedido);
    EXPECT_EQ(modo.id, "modo-1");
    EXPECT_EQ(modo.id_robo, 1);
    EXPECT_EQ(modo.geracao, 3u);
    EXPECT_EQ(modo.acao, AcaoOperador::DefinirModo);
    EXPECT_EQ(modo.modo, ModoOperacao::MANUAL);
    const auto destino = decodificarComandoOperador(R"({"id":"destino-1","acao":"definir_destino","id_robo":1,"geracao":3,"destino":{"x":5,"y":2}})");
    EXPECT_EQ(destino.acao, AcaoOperador::DefinirDestino);
    EXPECT_EQ(destino.destino, (Posicao{5, 2}));
    const auto extensao = decodificarComandoOperador(R"({"id":"operação 1","acao":"definir_modo","id_robo":1,"geracao":3,"modo":"manual","origem":"terminal"})");
    EXPECT_EQ(extensao.id, "operação 1");
    EXPECT_EQ(extensao.modo, ModoOperacao::MANUAL);
}

TEST(MqttCommandsTest, RejectsMalformedAmbiguousAndOutOfRangeCommands) {
    const std::vector<std::string> invalidos{
        "{}", "[]", pedido + " lixo",
        R"({"id":"a","acao":"definir_modo","id_robo":1.5,"geracao":3,"modo":"manual"})",
        R"({"id":"a","acao":"definir_modo","id_robo":1,"geracao":0,"modo":"manual"})",
        R"({"id":"a","acao":"definir_modo","id_robo":1,"geracao":3,"modo":"manual","modo":"automatico"})",
        R"({"id":"a","acao":"definir_modo","id_robo":1,"geracao":3,"modo":"manul"})",
        R"({"id":"","acao":"definir_modo","id_robo":1,"geracao":3,"modo":"manual"})",
        R"({"id":"a","acao":"definir_destino","id_robo":1,"geracao":3,"destino":{"x":-1,"y":2}})",
        R"({"id":"a","acao":"definir_destino","id_robo":1,"geracao":3,"destino":{"x":2147483648,"y":2}})",
        R"({"id":"a\u0000b","acao":"definir_modo","id_robo":1,"geracao":3,"modo":"manual"})",
        R"({"id":"a","acao":"definir_falha","id_robo":1,"geracao":3,"falha":1})",
        R"({"id":"a","acao":"definir_bateria","id_robo":1,"geracao":3,"bateria":101})",
        R"({"id":"a","acao":"definir_bateria","id_robo":1,"bateria":10})",
        R"({"id":"a","acao":"adicionar_robo","id_robo":2,"posicao":{"x":-1,"y":1}})",
        R"({"id":"a","acao":"adicionar_carga","carga":{"id":1,"tamanho":5,"id_baia":1,"id_destino":1}})",
        R"({"id":"a","acao":"adicionar_carga","carga":{"id":1,"tamanho":1,"id_baia":1}})",
        R"({"id":"lote","acao":"adicionar_lote_cargas","cargas":[]})",
        R"({"id":"lote","acao":"adicionar_lote_cargas","cargas":[{"id":1,"tamanho":1,"id_baia":1,"id_destino":1},{"id":1,"tamanho":2,"id_baia":1,"id_destino":1}]})",
        R"({"id":"a","acao":"retomar_missao","id_missao":0})"
    };
    for (const auto& payload : invalidos) {
        EXPECT_THROW(decodificarComandoOperador(payload), std::invalid_argument) << payload;
    }
    std::string id;
    EXPECT_THROW(decodificarComandoOperador(R"({"id":"identificavel","acao":"definir_modo","id_robo":1,"geracao":3,"modo":"inválido"})", &id), std::invalid_argument);
    EXPECT_EQ(id, "identificavel");
}

TEST(MqttCommandsTest, DecodesSimulatorAndLogisticsOperations) {
    const auto falha = decodificarComandoOperador(R"({"id":"f","acao":"definir_falha","id_robo":1,"geracao":3,"falha":true})");
    EXPECT_EQ(falha.acao, AcaoOperador::DefinirFalha);
    EXPECT_TRUE(falha.falha);
    const auto recuperacao = decodificarComandoOperador(R"({"id":"r","acao":"definir_falha","id_robo":1,"geracao":3,"falha":false})");
    EXPECT_FALSE(recuperacao.falha);
    const auto bateria = decodificarComandoOperador(R"({"id":"b","acao":"definir_bateria","id_robo":1,"geracao":3,"bateria":0})");
    EXPECT_EQ(bateria.acao, AcaoOperador::DefinirBateria);
    EXPECT_EQ(bateria.bateria, 0);
    const auto robo = decodificarComandoOperador(R"({"id":"n","acao":"adicionar_robo","id_robo":2,"posicao":{"x":3,"y":5}})");
    EXPECT_EQ(robo.acao, AcaoOperador::AdicionarRobo);
    EXPECT_EQ(robo.id_robo, 2);
    EXPECT_EQ(robo.posicao, (Posicao{3, 5}));
    const auto carga = decodificarComandoOperador(R"({"id":"c","acao":"adicionar_carga","carga":{"id":12,"tamanho":4,"id_baia":1,"id_destino":2}})");
    EXPECT_EQ(carga.acao, AcaoOperador::AdicionarCarga);
    EXPECT_EQ(carga.carga.id, 12);
    EXPECT_EQ(carga.carga.tamanho, 4);
    EXPECT_EQ(carga.carga.id_baia, 1);
    EXPECT_EQ(carga.carga.id_destino, 2);
    const auto lote = decodificarComandoOperador(R"({"id":"l","acao":"adicionar_lote_cargas","cargas":[{"id":12,"tamanho":4,"id_baia":1,"id_destino":2},{"id":13,"tamanho":1,"id_baia":2,"id_destino":1}]})");
    EXPECT_EQ(lote.acao, AcaoOperador::AdicionarLoteCargas);
    ASSERT_EQ(lote.cargas.size(), 2u);
    EXPECT_EQ(lote.cargas[0].id, 12);
    EXPECT_EQ(lote.cargas[1].id, 13);
    const auto missao = decodificarComandoOperador(R"({"id":"m","acao":"retomar_missao","id_missao":7})");
    EXPECT_EQ(missao.acao, AcaoOperador::RetomarMissao);
    EXPECT_EQ(missao.id_missao, 7);
}

TEST(MqttCommandsTest, DeduplicatesOperationsWithoutRobotGeneration) {
    const std::vector<std::string> pedidos{
        R"({"id":"c","acao":"adicionar_carga","carga":{"id":12,"tamanho":4,"id_baia":1,"id_destino":2}})",
        R"({"id":"lote","acao":"adicionar_lote_cargas","cargas":[{"id":12,"tamanho":4,"id_baia":1,"id_destino":2},{"id":13,"tamanho":2,"id_baia":2,"id_destino":1}]})",
        R"({"id":"n","acao":"adicionar_robo","id_robo":2,"posicao":{"x":3,"y":5}})",
        R"({"id":"m","acao":"retomar_missao","id_missao":7})"
    };
    IntegracaoMqtt mqtt;
    int executados = 0;
    const auto executar = [&](const ComandoOperador& comando) {
        ++executados;
        mqtt.responder(comando.id, true, "Aceito");
    };
    for (const auto& payload : pedidos) {
        mqtt.enfileirarComando(payload);
        mqtt.enfileirarComando(payload);
    }
    mqtt.processarComandos(executar);
    EXPECT_EQ(executados, 4);
    mqtt.enfileirarComando(R"({"id":"lote","acao":"adicionar_lote_cargas","cargas":[{"id":12,"tamanho":4,"id_baia":1,"id_destino":2},{"id":13,"tamanho":2,"id_baia":2,"id_destino":1}]})");
    mqtt.enfileirarComando(R"({"id":"lote","acao":"adicionar_lote_cargas","cargas":[{"id":12,"tamanho":1,"id_baia":1,"id_destino":2},{"id":13,"tamanho":2,"id_baia":2,"id_destino":1}]})");
    mqtt.enfileirarComando(R"({"id":"c","acao":"adicionar_carga","carga":{"id":12,"tamanho":3,"id_baia":1,"id_destino":2}})");
    mqtt.enfileirarComando(R"({"id":"n","acao":"adicionar_robo","id_robo":2,"posicao":{"x":4,"y":5}})");
    mqtt.enfileirarComando(R"({"id":"m","acao":"retomar_missao","id_missao":8})");
    mqtt.processarComandos(executar);
    EXPECT_EQ(executados, 4);
}

TEST(MqttCommandsTest, ExecutesOnlyOnCentralCycleAndDeduplicatesPendingAndFinishedRequests) {
    IntegracaoMqtt mqtt;
    int executados = 0;
    ComandoOperador comando;
    const auto executar = [&](const ComandoOperador& recebido) { ++executados; comando = recebido; };
    mqtt.enfileirarComando(pedido);
    mqtt.enfileirarComando(pedido);
    EXPECT_EQ(executados, 0);
    mqtt.processarComandos(executar);
    ASSERT_EQ(executados, 1);
    mqtt.enfileirarComando(pedido);
    mqtt.processarComandos(executar);
    EXPECT_EQ(executados, 1);
    mqtt.responder(comando.id, true, "Confirmado");
    mqtt.enfileirarComando(pedido);
    mqtt.enfileirarComando(R"({ "modo":"manual", "geracao":3, "id_robo":1, "acao":"definir_modo", "id":"modo-1" })");
    mqtt.enfileirarComando(R"({"id":"modo-1","acao":"definir_modo","id_robo":1,"geracao":3,"modo":"manual","origem":"terminal"})");
    mqtt.enfileirarComando(R"({"id":"modo-1","acao":"definir_modo","id_robo":1,"geracao":3,"modo":"automatico"})");
    mqtt.processarComandos(executar);
    EXPECT_EQ(executados, 1);
}

TEST(MqttCommandsTest, RejectsRetainedMessagesAndContinuesAfterInvalidRequest) {
    IntegracaoMqtt mqtt;
    int executados = 0;
    mqtt.enfileirarComando(pedido, true);
    mqtt.enfileirarComando("inválido");
    mqtt.processarComandos([&](const ComandoOperador&) { ++executados; });
    EXPECT_EQ(executados, 0);
    mqtt.enfileirarComando(pedido);
    mqtt.processarComandos([&](const ComandoOperador& comando) {
        ++executados;
        mqtt.responder(comando.id, false, "Robô ocupado");
    });
    EXPECT_EQ(executados, 1);
}

TEST(MqttCommandsTest, BoundsQueueAndPayloadSize) {
    IntegracaoMqtt mqtt;
    EXPECT_THROW(mqtt.enfileirarComando(std::string(16385, 'x')), std::invalid_argument);
    for (int i = 0; i < 256; ++i) mqtt.enfileirarComando(pedido);
    EXPECT_THROW(mqtt.enfileirarComando(pedido), std::runtime_error);
    int executados = 0;
    mqtt.processarComandos([&](const ComandoOperador&) { ++executados; });
    EXPECT_EQ(executados, 1);
}

TEST(MqttCommandsTest, PreservesPendingRequestsWhenDiscardingOldResponses) {
    IntegracaoMqtt mqtt;
    int repeticoes = 0;
    const auto executar = [&](const ComandoOperador& comando) {
        if (comando.id == "modo-1") ++repeticoes;
        else mqtt.responder(comando.id, true, "Confirmado");
    };
    mqtt.enfileirarComando(pedido);
    mqtt.processarComandos(executar);
    for (int i = 0; i < 1024; ++i) {
        auto outro = pedido;
        outro.replace(outro.find("modo-1"), std::string("modo-1").size(), "extra-" + std::to_string(i));
        mqtt.enfileirarComando(outro);
        mqtt.processarComandos(executar);
    }
    mqtt.enfileirarComando(pedido);
    mqtt.processarComandos(executar);
    EXPECT_EQ(repeticoes, 1);
}
