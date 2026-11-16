#include "mqtt_client.hpp"
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <system_error>
#include <utility>

#if defined(ENABLE_MQTT) && ENABLE_MQTT

#include <mosquitto.h>
#include <atomic>
#include <exception>
#include <iostream>
#include <limits>

namespace {
struct BibliotecaMqtt {
    int resultado = mosquitto_lib_init();

    ~BibliotecaMqtt() {
        if (resultado == MOSQ_ERR_SUCCESS) mosquitto_lib_cleanup();
    }
};
}

struct ClienteMqtt::Impl {
    mosquitto* cliente = nullptr;
    std::atomic<bool> conectado{false};
    bool loop_iniciado = false;
    std::string topico;
    Receptor receptor;

    static void aoConectar(mosquitto* cliente, void* dados, int resultado) {
        auto& estado = *static_cast<Impl*>(dados);
        estado.conectado = false;
        if (resultado != 0) {
            std::cerr << "MQTT: conexão recusada pelo broker (" << resultado << ")\n";
            return;
        }
        if (!estado.topico.empty()) {
            const int assinatura = mosquitto_subscribe(cliente, nullptr, estado.topico.c_str(), 1);
            if (assinatura != MOSQ_ERR_SUCCESS) {
                std::cerr << "MQTT: falha ao assinar comandos (" << assinatura << ")\n";
            }
        }
        estado.conectado = true;
    }

    static void aoAssinar(mosquitto*, void*, int, int quantidade, const int* qos) {
        if (quantidade != 1 || !qos || qos[0] < 0 || qos[0] > 2) {
            std::cerr << "MQTT: assinatura de comandos recusada pelo broker\n";
        }
    }

    static void aoReceber(mosquitto*, void* dados, const mosquitto_message* mensagem) {
        auto& estado = *static_cast<Impl*>(dados);
        if (!mensagem || !estado.receptor || !mensagem->topic || estado.topico != mensagem->topic) return;
        if (mensagem->payloadlen < 0 || mensagem->payloadlen > 4096) {
            std::cerr << "MQTT: mensagem descartada por exceder 4096 bytes\n";
            return;
        }
        try {
            const auto* bytes = mensagem->payload ? static_cast<const char*>(mensagem->payload) : "";
            estado.receptor(std::string(bytes, static_cast<std::size_t>(mensagem->payloadlen)), mensagem->retain);
        } catch (const std::exception& erro) {
            std::cerr << "MQTT: comando descartado: " << erro.what() << '\n';
        } catch (...) {
            std::cerr << "MQTT: falha ao receber comando\n";
        }
    }

    static void aoDesconectar(mosquitto*, void* dados, int resultado) {
        static_cast<Impl*>(dados)->conectado = false;
        if (resultado != 0) {
            std::cerr << "MQTT: conexão interrompida; aguardando reconexão (" << resultado << ")\n";
        }
    }

    ~Impl() {
        if (!cliente) return;
        mosquitto_disconnect(cliente);
        if (loop_iniciado) mosquitto_loop_stop(cliente, false);
        mosquitto_destroy(cliente);
    }
};

ClienteMqtt::ClienteMqtt() = default;
ClienteMqtt::~ClienteMqtt() = default;

void ClienteMqtt::connect(const std::string& host, int port, const std::string& topico, Receptor receptor) {
    if (impl_) return;
    if (host.empty() || port < 1 || port > 65535) {
        std::cerr << "MQTT: endereço ou porta do broker inválidos\n";
        return;
    }

    static BibliotecaMqtt biblioteca;
    if (biblioteca.resultado != MOSQ_ERR_SUCCESS) {
        std::cerr << "MQTT: falha ao inicializar a biblioteca (" << biblioteca.resultado << ")\n";
        return;
    }

    auto estado = std::make_unique<Impl>();
    estado->topico = topico;
    estado->receptor = std::move(receptor);
    estado->cliente = mosquitto_new(nullptr, true, estado.get());
    if (!estado->cliente) {
        std::cerr << "MQTT: falha ao criar o cliente\n";
        return;
    }
    mosquitto_connect_callback_set(estado->cliente, Impl::aoConectar);
    mosquitto_disconnect_callback_set(estado->cliente, Impl::aoDesconectar);
    mosquitto_subscribe_callback_set(estado->cliente, Impl::aoAssinar);
    mosquitto_message_callback_set(estado->cliente, Impl::aoReceber);
    mosquitto_reconnect_delay_set(estado->cliente, 1, 10, true);

    int resultado = mosquitto_connect_async(estado->cliente, host.c_str(), port, 60);
    if (resultado != MOSQ_ERR_SUCCESS) {
        std::cerr << "MQTT: falha ao iniciar conexão com " << host << ":" << port
                  << " (" << resultado << ")\n";
        if (resultado != MOSQ_ERR_ERRNO && resultado != MOSQ_ERR_EAI) return;
    }

    resultado = mosquitto_loop_start(estado->cliente);
    if (resultado != MOSQ_ERR_SUCCESS) {
        std::cerr << "MQTT: falha ao iniciar o processamento de rede (" << resultado << ")\n";
        return;
    }
    estado->loop_iniciado = true;
    impl_ = std::move(estado);
}

void ClienteMqtt::publish(const std::string& topic, const std::string& payload, bool retain) {
    if (!isConnected()) return;
    if (payload.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        std::cerr << "MQTT: mensagem excede o tamanho permitido\n";
        return;
    }

    const int resultado = mosquitto_publish(impl_->cliente, nullptr, topic.c_str(),
        static_cast<int>(payload.size()), payload.data(), 1, retain);
    if (resultado != MOSQ_ERR_SUCCESS) {
        std::cerr << "MQTT: falha ao publicar em " << topic << " (" << resultado << ")\n";
    }
}

bool ClienteMqtt::isConnected() const {
    return impl_ && impl_->conectado.load();
}

void ClienteMqtt::disconnect() {
    impl_.reset();
}

#else

struct ClienteMqtt::Impl {};

ClienteMqtt::ClienteMqtt() = default;
ClienteMqtt::~ClienteMqtt() = default;

void ClienteMqtt::connect(const std::string&, int, const std::string&, Receptor) {}
void ClienteMqtt::publish(const std::string&, const std::string&, bool) {}
bool ClienteMqtt::isConnected() const { return false; }
void ClienteMqtt::disconnect() {}

#endif

void ClienteMqtt::connect(const std::string& topico, Receptor receptor) {
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
    connect(host ? host : "localhost", porta, topico, std::move(receptor));
#endif
}
