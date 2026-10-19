#ifndef HARDWARE_HPP
#define HARDWARE_HPP

#include "protocol.hpp"
#include "warehouse.hpp"
#include <chrono>
#include <map>
#include <optional>
#include <vector>

class AdaptadorHardware {
public:
    virtual ~AdaptadorHardware() = default;
    virtual std::vector<Mensagem> confirmarComandoAceito(const Mensagem& executarComando) = 0;
    virtual std::vector<Mensagem> setpoint(const Mensagem& mensagem) = 0;
    virtual std::vector<Mensagem> avancarCiclo() = 0;
};

// Simula movimentos e sensores, confirmando cada passo antes do próximo.
class AdaptadorHardwareLocal : public AdaptadorHardware {
public:
    explicit AdaptadorHardwareLocal(const MapaGalpao& galpao);
    void registrarRobo(const EstadoAtualRobo& capturarEstado);
    bool contemRobo(int id) const;
    void desconectar(int id);
    bool removerRobo(int id);
    std::vector<Mensagem> confirmarComandoAceito(const Mensagem& executarComando) override;
    std::vector<Mensagem> setpoint(const Mensagem& mensagem) override;
    std::vector<Mensagem> avancarCiclo() override;
    void registrarObservacao(const EstadoAtualRobo& capturarEstado);
    std::optional<std::vector<Mensagem>> injetarLeitura(int id, std::optional<int> bateria, std::optional<bool> falha);
    std::optional<Mensagem> lerLeitura(int id) const;
private:
    struct Unidade {
        Mensagem leitura_sensores;
        std::vector<int> integrantes;
        bool paused = false;
        EstadoRobo observed = EstadoRobo::DISPONIVEL;
        std::chrono::steady_clock::time_point last_charge = std::chrono::steady_clock::now();
    };
    const MapaGalpao& galpao_;
    std::map<int, Unidade> units_;
    std::map<IdEntidade, std::map<int, Mensagem>> proposals_;
    Mensagem leitura_sensores(Unidade& unit);
};

#endif
