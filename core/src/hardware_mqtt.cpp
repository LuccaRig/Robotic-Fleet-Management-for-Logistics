#include "hardware_mqtt.hpp"
#include "topics.hpp"
#include <iostream>
#include <set>
#include <stdexcept>
#include <thread>
#include <utility>

CanalSimulacaoMqtt::CanalSimulacaoMqtt(bool simulador) : simulador_(simulador) {}

void CanalSimulacaoMqtt::iniciar() {
#if !defined(ENABLE_MQTT) || !ENABLE_MQTT
    throw std::runtime_error("O simulador MQTT exige um executável compilado com ENABLE_MQTT=ON");
#endif
    cliente_.connect(simulador_ ? Topics::simulatorCommands() : Topics::simulatorEvents(),
        [this](std::string payload, bool retido) {
            if (retido) return;
            std::lock_guard<std::mutex> lock(mutex_);
            if (fila_.size() >= 4096) { esgotada_ = true; return; }
            fila_.push_back(std::move(payload));
        });
}

void CanalSimulacaoMqtt::parar() { cliente_.disconnect(); }
bool CanalSimulacaoMqtt::conectada() const { return cliente_.isConnected(); }

bool CanalSimulacaoMqtt::enviar(const Mensagem& mensagem) {
    if (!conectada()) return false;
    auto fisico = mensagem;
    fisico.rota.clear();
    const auto bytes = codificarMensagem(fisico);
    if (bytes.size() > 4096) throw std::runtime_error("Mensagem física MQTT excede 4096 bytes");
    cliente_.publish(simulador_ ? Topics::simulatorEvents() : Topics::simulatorCommands(),
                     std::string(bytes.begin(), bytes.end()), false);
    return true;
}

std::vector<Mensagem> CanalSimulacaoMqtt::receber() {
    std::vector<std::string> fila;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (esgotada_) throw std::runtime_error("Fila da simulação MQTT esgotada");
        fila.swap(fila_);
    }
    std::vector<Mensagem> mensagens;
    for (const auto& payload : fila) {
        try {
            mensagens.push_back(decodificarMensagem({payload.begin(), payload.end()}));
        } catch (const std::exception& erro) {
            std::cerr << "Simulação MQTT: " << erro.what() << '\n';
        }
    }
    return mensagens;
}

void executarSimuladorMqtt(const MapaGalpao& galpao, const std::vector<EstadoAtualRobo>& robos,
                          const std::function<bool()>& continuar) {
    AdaptadorHardwareLocal hardware(galpao);
    CanalSimulacaoMqtt canal(true);
    std::set<int> ids;
    std::map<int, std::uint64_t> intervencoes;
    std::map<int, Mensagem> removidos;
    auto geracao = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    const auto disponibilizar = [&](EstadoAtualRobo estado) {
        if (estado.id < 0 || hardware.contemRobo(estado.id) || !galpao.podeTransitar(estado.posicao) || galpao.trechoEm(estado.posicao)) return;
        estado.geracao = ++geracao;
        estado.destino = estado.posicao;
        hardware.registrarRobo(estado);
        ids.insert(estado.id);
        removidos.erase(estado.id);
    };
    for (const auto& robo : robos) disponibilizar(robo);
    canal.iniciar();
    auto proxima_publicacao = std::chrono::steady_clock::now();
    const auto publicar = [&](const std::vector<Mensagem>& leituras) {
        for (const auto& leitura : leituras) canal.enviar(leitura);
    };
    while (continuar()) {
        for (const auto& comando : canal.receber()) {
            try {
                if (comando.tipo == TipoMensagem::SaudacaoRobo) {
                    disponibilizar(estadoRoboDaMensagem(comando));
                    continue;
                }
                const auto leitura = hardware.lerLeitura(comando.id_robo);
                if (!leitura || leitura->geracao != comando.geracao) continue;
                if (comando.tipo == TipoMensagem::DestinoNavegacao) publicar(hardware.setpoint(comando));
                else if (comando.tipo == TipoMensagem::RoboDesconectado) hardware.desconectar(comando.id_robo);
                else if (comando.tipo == TipoMensagem::RoboEncerrado) {
                    auto anuncio = comando;
                    anuncio.aceito = hardware.removerRobo(comando.id_robo);
                    if (anuncio.aceito) {
                        ids.erase(comando.id_robo);
                        intervencoes.erase(comando.id_robo);
                        removidos[comando.id_robo] = anuncio;
                    }
                    canal.enviar(anuncio);
                } else if (comando.tipo == TipoMensagem::AtualizarSensores) {
                    auto& ultima = intervencoes[comando.id_robo];
                    if (!comando.id_comando || comando.id_comando <= ultima) continue;
                    ultima = comando.id_comando;
                    const auto leituras = hardware.injetarLeitura(comando.id_robo,
                        comando.texto == "bateria" ? std::optional<int>(comando.bateria) : std::nullopt,
                        comando.texto == "falha" ? std::optional<bool>(comando.falha) : std::nullopt);
                    if (leituras) publicar(*leituras);
                } else publicar(hardware.confirmarComandoAceito(comando));
            } catch (const std::exception& erro) {
                std::cerr << "Comando físico recusado: " << erro.what() << '\n';
            }
        }
        publicar(hardware.avancarCiclo());
        if (std::chrono::steady_clock::now() >= proxima_publicacao) {
            for (const auto& removido : removidos) canal.enviar(removido.second);
            for (const auto id : ids) {
                auto anuncio = *hardware.lerLeitura(id);
                anuncio.tipo = TipoMensagem::SaudacaoRobo;
                canal.enviar(anuncio);
                publicar(*hardware.injetarLeitura(id, std::nullopt, std::nullopt));
            }
            proxima_publicacao = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}
