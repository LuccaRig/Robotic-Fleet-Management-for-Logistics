#ifndef HARDWARE_MQTT_HPP
#define HARDWARE_MQTT_HPP

#include "hardware.hpp"
#include "mqtt_client.hpp"
#include <functional>
#include <mutex>

class CanalSimulacaoMqtt {
public:
    explicit CanalSimulacaoMqtt(bool simulador = false);
    void iniciar();
    void parar();
    bool conectada() const;
    bool enviar(const Mensagem& mensagem);
    std::vector<Mensagem> receber();
private:
    bool simulador_;
    std::mutex mutex_;
    std::vector<std::string> fila_;
    bool esgotada_ = false;
    ClienteMqtt cliente_;
};

void executarSimuladorMqtt(const MapaGalpao& galpao, const std::vector<EstadoAtualRobo>& robos,
                          const std::function<bool()>& continuar);

#endif
