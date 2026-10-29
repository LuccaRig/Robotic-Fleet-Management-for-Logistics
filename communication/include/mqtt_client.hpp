#ifndef MQTT_CLIENT_HPP
#define MQTT_CLIENT_HPP

#include <string>

// Publicador de telemetria MQTT. Nunca lança excecoes: falhas de rede/broker
// sao registradas e ignoradas, para nunca bloquear o ciclo de controle IPC.
class MqttPublisher {
public:
    MqttPublisher();
    ~MqttPublisher();
    MqttPublisher(const MqttPublisher&) = delete;
    MqttPublisher& operator=(const MqttPublisher&) = delete;

    // Conecta ao broker. Falhas silenciosas: isConnected() retorna falso.
    void connect(const std::string& host, int port);

    // Publica um payload em um topico. QoS 1. retain = mensagem retida.
    // Falhas silenciosas.
    void publish(const std::string& topic, const std::string& payload, bool retain = true);

    bool isConnected() const;

    void disconnect();

private:
    struct Impl;
    Impl* impl_;
};

#endif // MQTT_CLIENT_HPP
