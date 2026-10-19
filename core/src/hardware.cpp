#include "hardware.hpp"
#include <algorithm>
#include <stdexcept>

AdaptadorHardwareLocal::AdaptadorHardwareLocal(const MapaGalpao& galpao, SensorSink sink) : galpao_(galpao), sink_(std::move(sink)) {}
Mensagem AdaptadorHardwareLocal::leitura_sensores(Unidade& unit) { unit.leitura_sensores.tipo = TipoMensagem::AtualizarSensores; ++unit.leitura_sensores.sequencia_sensor; unit.leitura_sensores.ocorrido_em_ms = instanteUnixMs(); return unit.leitura_sensores; }
void AdaptadorHardwareLocal::deliver(std::vector<Mensagem> mensagens) { for (auto& mensagem : mensagens) sink_(std::move(mensagem)); }
void AdaptadorHardwareLocal::registrarRobo(const EstadoAtualRobo& capturarEstado) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (units_.count(capturarEstado.id)) throw std::runtime_error("O robô ainda ocupa o ambiente; remova-o explicitamente antes de recriar");
    Unidade unit; unit.leitura_sensores = mensagemDoEstadoRobo(capturarEstado); unit.integrantes = {capturarEstado.id}; units_.emplace(capturarEstado.id, std::move(unit));
}
void AdaptadorHardwareLocal::desconectar(int id) { std::lock_guard<std::mutex> lock(mutex_); const auto it = units_.find(id); if (it != units_.end()) { it->second.paused = true; proposals_.erase(it->second.leitura_sensores.id_missao); } }
bool AdaptadorHardwareLocal::contemRobo(int id) const { std::lock_guard<std::mutex> lock(mutex_); return units_.count(id) != 0; }
bool AdaptadorHardwareLocal::removerRobo(int id) { std::lock_guard<std::mutex> lock(mutex_); auto it = units_.find(id); if (it == units_.end() || it->second.leitura_sensores.acoplado) return false; proposals_.erase(it->second.leitura_sensores.id_missao); units_.erase(it); return true; }
void AdaptadorHardwareLocal::definirValidadorMovimento(std::function<bool(const Mensagem&)> validador) { std::lock_guard<std::mutex> lock(mutex_); validator_ = std::move(validador); }
void AdaptadorHardwareLocal::confirmarComandoAceito(const Mensagem& m) {
    std::vector<Mensagem> feedback;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = units_.find(m.id_robo); if (it == units_.end()) return;
        auto& unit = it->second;
        if (m.geracao != unit.leitura_sensores.geracao) return;
        unit.leitura_sensores.revisao = m.id_comando;
        if (m.tipo == TipoMensagem::CancelarMissao) {
            proposals_.erase(unit.leitura_sensores.id_missao); unit.leitura_sensores.id_missao = 0; unit.leitura_sensores.id_carga = 0; unit.leitura_sensores.id_equipe = 0;
            unit.leitura_sensores.fase = FaseMissao::NENHUMA; unit.leitura_sensores.passo = 0; unit.integrantes = {m.id_robo}; unit.paused = false;
        } else if (m.tipo == TipoMensagem::PausarMissao) {
            proposals_.erase(unit.leitura_sensores.id_missao); unit.paused = true; unit.leitura_sensores.fase = FaseMissao::SUSPENSO;
        } else if (m.tipo == TipoMensagem::AtribuirMissao || m.tipo == TipoMensagem::RetomarMissao) {
            unit.leitura_sensores.id_missao = m.id_missao; unit.leitura_sensores.id_carga = m.id_carga; unit.leitura_sensores.id_equipe = m.id_equipe;
            unit.leitura_sensores.id_baia = m.id_baia; unit.leitura_sensores.fase = m.fase; unit.leitura_sensores.passo = 0; unit.integrantes = m.integrantes; unit.paused = false;
            if (m.fase == FaseMissao::ACOPLANDO) unit.leitura_sensores.acoplado = true;
            if (m.fase == FaseMissao::DESACOPLANDO) unit.leitura_sensores.acoplado = false;
        } else if (m.tipo == TipoMensagem::Acoplar) { unit.leitura_sensores.fase = FaseMissao::ACOPLANDO; unit.leitura_sensores.acoplado = true; }
        else if (m.tipo == TipoMensagem::Desacoplar) { unit.leitura_sensores.fase = FaseMissao::DESACOPLANDO; unit.leitura_sensores.acoplado = false; }
        else return;
        feedback.push_back(leitura_sensores(unit));
    }
    deliver(std::move(feedback));
}
void AdaptadorHardwareLocal::setpoint(const Mensagem& m) {
    std::vector<Mensagem> feedback;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = units_.find(m.id_robo); if (it == units_.end()) throw std::runtime_error("Setpoint de robô inexistente");
        auto& unit = it->second;
        if (m.geracao != unit.leitura_sensores.geracao || m.id_missao != unit.leitura_sensores.id_missao || m.fase != unit.leitura_sensores.fase || m.revisao != unit.leitura_sensores.revisao || unit.paused) return;
        if (unit.leitura_sensores.falha || unit.leitura_sensores.bateria == 0) return;
        if (m.passo != unit.leitura_sensores.passo + 1 || m.posicao != unit.leitura_sensores.posicao || distanciaManhattan(unit.leitura_sensores.posicao, m.destino) != 1 || !galpao_.podeTransitar(m.destino)) throw std::runtime_error("Setpoint físico inválido");
        if (validator_ && !validator_(m)) throw std::runtime_error("Movimento sem permissão de passagem");
        if (!unit.leitura_sensores.acoplado) {
            unit.leitura_sensores.posicao = m.destino; unit.leitura_sensores.passo = m.passo; --unit.leitura_sensores.bateria; feedback.push_back(leitura_sensores(unit));
        } else {
            auto& propostas = proposals_[m.id_missao]; propostas[m.id_robo] = m;
            if (unit.integrantes.empty()) throw std::runtime_error("Equipe acoplada sem integrantes");
            bool concluido = true;
            for (const auto id : unit.integrantes) {
                const auto robo = units_.find(id); const auto proposal = propostas.find(id);
                if (robo == units_.end() || proposal == propostas.end() || robo->second.paused || robo->second.leitura_sensores.falha || robo->second.leitura_sensores.bateria <= 15 || !robo->second.leitura_sensores.acoplado || robo->second.leitura_sensores.id_missao != m.id_missao || proposal->second.destino != m.destino || proposal->second.passo != m.passo) { concluido = false; break; }
            }
            if (concluido) {
                for (const auto id : unit.integrantes) {
                    auto& member = units_.at(id); member.leitura_sensores.posicao = m.destino; member.leitura_sensores.passo = m.passo;
                    --member.leitura_sensores.bateria; feedback.push_back(leitura_sensores(member));
                }
                proposals_.erase(m.id_missao);
            }
        }
    }
    deliver(std::move(feedback));
}
void AdaptadorHardwareLocal::registrarObservacao(const EstadoAtualRobo& s) {
    std::lock_guard<std::mutex> lock(mutex_); const auto it = units_.find(s.id);
    if (it != units_.end() && it->second.leitura_sensores.geracao == s.geracao && it->second.leitura_sensores.id_missao == s.id_missao) it->second.observed = s.estado;
}
bool AdaptadorHardwareLocal::injetarLeitura(int id, std::optional<int> bateria, std::optional<bool> falha) {
    if (bateria && (*bateria < 0 || *bateria > 100)) return false;
    std::vector<Mensagem> feedback;
    {
        std::lock_guard<std::mutex> lock(mutex_); auto it = units_.find(id); if (it == units_.end()) return false;
        if (bateria) it->second.leitura_sensores.bateria = *bateria;
        if (falha) it->second.leitura_sensores.falha = *falha;
        if (it->second.leitura_sensores.falha || it->second.leitura_sensores.bateria <= 15) proposals_.erase(it->second.leitura_sensores.id_missao);
        feedback.push_back(leitura_sensores(it->second));
    }
    deliver(std::move(feedback)); return true;
}
void AdaptadorHardwareLocal::avancarCiclo() {
    std::vector<Mensagem> feedback;
    {
        std::lock_guard<std::mutex> lock(mutex_); const auto now = std::chrono::steady_clock::now();
        for (auto& pair : units_) {
            auto& unit = pair.second;
            if (unit.observed != EstadoRobo::RECARREGANDO || unit.leitura_sensores.falha || unit.leitura_sensores.bateria >= 100 || galpao_.obterCelula(unit.leitura_sensores.posicao.x, unit.leitura_sensores.posicao.y) != EstadoCelula::ESTACAO_RECARGA || now - unit.last_charge < std::chrono::milliseconds(500)) continue;
            unit.last_charge = now; unit.leitura_sensores.bateria = std::min(100, unit.leitura_sensores.bateria + 5); feedback.push_back(leitura_sensores(unit));
        }
    }
    deliver(std::move(feedback));
}
std::optional<Mensagem> AdaptadorHardwareLocal::lerLeitura(int id) const {
    std::lock_guard<std::mutex> lock(mutex_); auto it = units_.find(id); if (it == units_.end()) return std::nullopt; return it->second.leitura_sensores;
}
