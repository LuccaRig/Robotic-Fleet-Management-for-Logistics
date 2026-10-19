#ifndef HARDWARE_HPP
#define HARDWARE_HPP

#include "protocol.hpp"
#include "warehouse.hpp"
#include <chrono>
#include <functional>
#include <map>
#include <mutex>
#include <optional>

class AdaptadorHardware {
public:
    virtual ~AdaptadorHardware() = default;
    virtual void confirmarComandoAceito(const Mensagem& executarComando) = 0;
    virtual void setpoint(const Mensagem& mensagem) = 0;
    virtual void avancarCiclo() = 0;
};

// Simula movimentos e sensores, confirmando cada passo antes do próximo.
class AdaptadorHardwareLocal : public AdaptadorHardware {
public:
    using SensorSink = std::function<void(Mensagem)>;
    explicit AdaptadorHardwareLocal(const MapaGalpao& galpao, SensorSink sink);
    void registrarRobo(const EstadoAtualRobo& capturarEstado);
    bool contemRobo(int id) const;
    void desconectar(int id);
    bool removerRobo(int id);
    void confirmarComandoAceito(const Mensagem& executarComando) override;
    void setpoint(const Mensagem& mensagem) override;
    void avancarCiclo() override;
    void registrarObservacao(const EstadoAtualRobo& capturarEstado);
    bool injetarLeitura(int id, std::optional<int> bateria, std::optional<bool> falha);
    std::optional<Mensagem> lerLeitura(int id) const;
    void definirValidadorMovimento(std::function<bool(const Mensagem&)> validador);
private:
    struct Unidade {
        Mensagem leitura_sensores;
        std::vector<int> integrantes;
        bool paused = false;
        EstadoRobo observed = EstadoRobo::DISPONIVEL;
        std::chrono::steady_clock::time_point last_charge = std::chrono::steady_clock::now();
    };
    const MapaGalpao& galpao_;
    SensorSink sink_;
    mutable std::mutex mutex_;
    std::map<int, Unidade> units_;
    std::map<IdEntidade, std::map<int, Mensagem>> proposals_;
    std::function<bool(const Mensagem&)> validator_;
    Mensagem leitura_sensores(Unidade& unit);
    void deliver(std::vector<Mensagem> mensagens);
};

#endif
