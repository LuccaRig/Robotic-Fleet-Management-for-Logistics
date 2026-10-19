#ifndef FLEET_STATE_HPP
#define FLEET_STATE_HPP

#include "domain.hpp"
#include <map>
#include <optional>
#include <vector>

// Guarda o último estado confirmado de cada geração de robô.
class EstadoFrota {
public:
    bool registrarRobo(int id, std::uint64_t geracao, Posicao inicio) {
        auto it = robos_.find(id);
        if (it != robos_.end() && geracao <= it->second.geracao) return false;
        EstadoAtualRobo estado; estado.id = id; estado.geracao = geracao; estado.posicao = inicio;
        estado.destino = inicio; estado.atualizado_em_ms = instanteUnixMs();
        robos_[id] = estado; return true;
    }
    bool atualizar(EstadoAtualRobo estado) {
        auto it = robos_.find(estado.id);
        // Geração e sequências impedem que mensagens atrasadas restaurem estado antigo.
        if (it == robos_.end() || estado.geracao != it->second.geracao || estado.sequencia <= it->second.sequencia || estado.revisao < it->second.revisao || estado.sequencia_sensor < it->second.sequencia_sensor || (!it->second.conectado && it->second.sequencia > 0)) return false;
        it->second = std::move(estado); return true;
    }
    bool atualizarLeituraSensores(EstadoAtualRobo estado) {
        auto it = robos_.find(estado.id);
        if (it == robos_.end() || estado.geracao != it->second.geracao || estado.sequencia_sensor <= it->second.sequencia_sensor ||
            estado.revisao < it->second.revisao || !it->second.conectado) return false;
        estado.sequencia = it->second.sequencia;
        it->second = std::move(estado);
        return true;
    }
    bool desconectar(int id, std::uint64_t geracao) {
        auto it = robos_.find(id);
        if (it == robos_.end() || geracao != it->second.geracao) return false;
        it->second.conectado = false; it->second.disponivel = false; it->second.atualizado_em_ms = instanteUnixMs(); return true;
    }
    std::optional<EstadoAtualRobo> obter(int id) const {
        const auto it = robos_.find(id);
        if (it == robos_.end()) return std::nullopt;
        return it->second;
    }
    std::vector<EstadoAtualRobo> capturarEstado() const {
        std::vector<EstadoAtualRobo> result;
        for (const auto& pair : robos_) result.push_back(pair.second);
        return result;
    }
private:
    std::map<int, EstadoAtualRobo> robos_;
};

#endif
