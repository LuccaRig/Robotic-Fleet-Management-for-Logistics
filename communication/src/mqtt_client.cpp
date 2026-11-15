#include "mqtt_client.hpp"

#if defined(ENABLE_MQTT) && ENABLE_MQTT

#include <mosquitto.h>
#include <atomic>
#include <iostream>
#include <limits>
#include <utility>

namespace {
struct BibliotecaMqtt {
    int resultado = mosquitto_lib_init();

    ~BibliotecaMqtt() {
        if (resultado == MOSQ_ERR_SUCCESS) mosquitto_lib_cleanup();
    }
};
}

struct MqttPublisher::Impl {
    mosquitto* cliente = nullptr;
    std::atomic<bool> conectado{false};
    bool loop_iniciado = false;

    static void aoConectar(mosquitto*, void* dados, int resultado) {
        static_cast<Impl*>(dados)->conectado = resultado == 0;
        if (resultado != 0) {
            std::cerr << "MQTT: conexão recusada pelo broker (" << resultado << ")\n";
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

MqttPublisher::MqttPublisher() = default;
MqttPublisher::~MqttPublisher() = default;

void MqttPublisher::connect(const std::string& host, int port) {
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
    estado->cliente = mosquitto_new(nullptr, true, estado.get());
    if (!estado->cliente) {
        std::cerr << "MQTT: falha ao criar o cliente\n";
        return;
    }
    mosquitto_connect_callback_set(estado->cliente, Impl::aoConectar);
    mosquitto_disconnect_callback_set(estado->cliente, Impl::aoDesconectar);
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

void MqttPublisher::publish(const std::string& topic, const std::string& payload, bool retain) {
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

bool MqttPublisher::isConnected() const {
    return impl_ && impl_->conectado.load();
}

void MqttPublisher::disconnect() {
    impl_.reset();
}

#else

struct MqttPublisher::Impl {};

MqttPublisher::MqttPublisher() = default;
MqttPublisher::~MqttPublisher() = default;

void MqttPublisher::connect(const std::string&, int) {}
void MqttPublisher::publish(const std::string&, const std::string&, bool) {}
bool MqttPublisher::isConnected() const { return false; }
void MqttPublisher::disconnect() {}

#endif
