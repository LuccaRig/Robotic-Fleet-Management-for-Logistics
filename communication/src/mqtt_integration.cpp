#include "mqtt_integration.hpp"
#include "topics.hpp"
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <system_error>

namespace {
std::string escaparJson(const std::string& texto) {
    std::string resultado;
    const char hex[] = "0123456789abcdef";
    for (unsigned char caractere : texto) {
        if (caractere == '"' || caractere == '\\') {
            resultado += '\\';
            resultado += static_cast<char>(caractere);
        } else if (caractere < 0x20) {
            resultado += "\\u00";
            resultado += hex[caractere >> 4];
            resultado += hex[caractere & 0x0f];
        } else {
            resultado += static_cast<char>(caractere);
        }
    }
    return resultado;
}

std::string roboParaJson(const EstadoAtualRobo& robo) {
    std::ostringstream json;
    json.imbue(std::locale::classic());
    json << std::boolalpha
         << "{\"id\":" << robo.id
         << ",\"geracao\":" << robo.geracao
         << ",\"sequencia\":" << robo.sequencia
         << ",\"posicao\":{\"x\":" << robo.posicao.x << ",\"y\":" << robo.posicao.y << "}"
         << ",\"destino\":{\"x\":" << robo.destino.x << ",\"y\":" << robo.destino.y << "}"
         << ",\"estado\":\"" << escaparJson(nomeEstadoRobo(robo.estado)) << "\""
         << ",\"fase\":" << static_cast<int>(robo.fase)
         << ",\"bateria\":" << robo.bateria
         << ",\"falha\":" << robo.falha
         << ",\"acoplado\":" << robo.acoplado
         << ",\"conectado\":" << robo.conectado
         << ",\"disponivel\":" << robo.disponivel
         << ",\"id_missao\":" << robo.id_missao
         << ",\"id_carga\":" << robo.id_carga
         << ",\"id_equipe\":" << robo.id_equipe
         << ",\"id_baia\":" << robo.id_baia
         << ",\"passo\":" << robo.passo
         << ",\"sequencia_sensor\":" << robo.sequencia_sensor
         << ",\"revisao\":" << robo.revisao
         << ",\"distancia\":" << robo.distancia
         << ",\"atualizado_em_ms\":" << robo.atualizado_em_ms << "}";
    return json.str();
}
}

void IntegracaoMqtt::iniciar() {
#if defined(ENABLE_MQTT) && ENABLE_MQTT
    const char* host = std::getenv("MQTT_HOST");
    if (host && !*host) throw std::invalid_argument("MQTT_HOST não pode ser vazio");

    int porta = 1883;
    if (const char* valor = std::getenv("MQTT_PORT")) {
        const auto fim = valor + std::strlen(valor);
        const auto resultado = std::from_chars(valor, fim, porta);
        if (resultado.ec != std::errc{} || resultado.ptr != fim || porta < 1 || porta > 65535) {
            throw std::invalid_argument("MQTT_PORT deve ser um número inteiro entre 1 e 65535");
        }
    }
    publicador_.connect(host ? host : "localhost", porta);
#endif
}

void IntegracaoMqtt::publicar(const std::vector<EstadoAtualRobo>& frota) {
    if (!publicador_.isConnected()) return;
    try {
        std::ostringstream snapshot;
        snapshot << "{\"robos\":[";
        bool primeiro = true;
        for (const auto& robo : frota) {
            const auto json = roboParaJson(robo);
            if (!primeiro) snapshot << ',';
            primeiro = false;
            snapshot << json;
            publicador_.publish(Topics::robotTelemetry(robo.id), json);
        }
        snapshot << "]}";
        publicador_.publish(Topics::fleetSnapshot(), snapshot.str());
    } catch (const std::exception& erro) {
        std::cerr << "MQTT: falha ao publicar telemetria: " << erro.what() << '\n';
    }
}

void IntegracaoMqtt::parar() {
    publicador_.disconnect();
}
