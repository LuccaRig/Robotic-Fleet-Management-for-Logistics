#ifndef MQTT_INTEGRATION_HPP
#define MQTT_INTEGRATION_HPP

#include "domain.hpp"
#include "mqtt_client.hpp"
#include <vector>

class IntegracaoMqtt {
public:
    void iniciar();
    void publicar(const std::vector<EstadoAtualRobo>& frota);
    void parar();

private:
    MqttPublisher publicador_;
};

#endif
