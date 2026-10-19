#include "hardware.hpp"
#include <algorithm>
#include <stdexcept>

AdaptadorHardwareLocal::AdaptadorHardwareLocal(const MapaGalpao& galpao) : galpao_(galpao) {}

Mensagem AdaptadorHardwareLocal::leitura_sensores(Unidade& unidade) {
    unidade.leitura_sensores.tipo = TipoMensagem::AtualizarSensores;
    ++unidade.leitura_sensores.sequencia_sensor;
    unidade.leitura_sensores.ocorrido_em_ms = instanteUnixMs();
    return unidade.leitura_sensores;
}

void AdaptadorHardwareLocal::registrarRobo(const EstadoAtualRobo& estado) {
    if (units_.count(estado.id)) throw std::runtime_error("O robô ainda ocupa o ambiente; remova-o explicitamente antes de recriar");
    Unidade unidade;
    unidade.leitura_sensores = mensagemDoEstadoRobo(estado);
    unidade.integrantes = {estado.id};
    units_.emplace(estado.id, std::move(unidade));
}

void AdaptadorHardwareLocal::desconectar(int id) {
    const auto robo = units_.find(id);
    if (robo == units_.end()) return;
    robo->second.paused = true;
    proposals_.erase(robo->second.leitura_sensores.id_missao);
}

bool AdaptadorHardwareLocal::contemRobo(int id) const { return units_.count(id) != 0; }

bool AdaptadorHardwareLocal::removerRobo(int id) {
    const auto robo = units_.find(id);
    if (robo == units_.end() || robo->second.leitura_sensores.acoplado) return false;
    proposals_.erase(robo->second.leitura_sensores.id_missao);
    units_.erase(robo);
    return true;
}

std::vector<Mensagem> AdaptadorHardwareLocal::confirmarComandoAceito(const Mensagem& mensagem) {
    const auto encontrado = units_.find(mensagem.id_robo);
    if (encontrado == units_.end()) return {};
    auto& unidade = encontrado->second;
    if (mensagem.geracao != unidade.leitura_sensores.geracao) return {};
    unidade.leitura_sensores.revisao = mensagem.id_comando;

    if (mensagem.tipo == TipoMensagem::CancelarMissao) {
        proposals_.erase(unidade.leitura_sensores.id_missao);
        unidade.leitura_sensores.id_missao = 0;
        unidade.leitura_sensores.id_carga = 0;
        unidade.leitura_sensores.id_equipe = 0;
        unidade.leitura_sensores.fase = FaseMissao::NENHUMA;
        unidade.leitura_sensores.passo = 0;
        unidade.integrantes = {mensagem.id_robo};
        unidade.paused = false;
    } else if (mensagem.tipo == TipoMensagem::PausarMissao) {
        proposals_.erase(unidade.leitura_sensores.id_missao);
        unidade.paused = true;
        unidade.leitura_sensores.fase = FaseMissao::SUSPENSO;
    } else if (mensagem.tipo == TipoMensagem::AtribuirMissao || mensagem.tipo == TipoMensagem::RetomarMissao) {
        unidade.leitura_sensores.id_missao = mensagem.id_missao;
        unidade.leitura_sensores.id_carga = mensagem.id_carga;
        unidade.leitura_sensores.id_equipe = mensagem.id_equipe;
        unidade.leitura_sensores.id_baia = mensagem.id_baia;
        unidade.leitura_sensores.fase = mensagem.fase;
        unidade.leitura_sensores.passo = 0;
        unidade.integrantes = mensagem.integrantes;
        unidade.paused = false;
        if (mensagem.fase == FaseMissao::ACOPLANDO) unidade.leitura_sensores.acoplado = true;
        if (mensagem.fase == FaseMissao::DESACOPLANDO) unidade.leitura_sensores.acoplado = false;
    } else if (mensagem.tipo == TipoMensagem::Acoplar) {
        unidade.leitura_sensores.fase = FaseMissao::ACOPLANDO;
        unidade.leitura_sensores.acoplado = true;
    } else if (mensagem.tipo == TipoMensagem::Desacoplar) {
        unidade.leitura_sensores.fase = FaseMissao::DESACOPLANDO;
        unidade.leitura_sensores.acoplado = false;
    } else {
        return {};
    }
    return {leitura_sensores(unidade)};
}

std::vector<Mensagem> AdaptadorHardwareLocal::setpoint(const Mensagem& mensagem) {
    const auto encontrado = units_.find(mensagem.id_robo);
    if (encontrado == units_.end()) throw std::runtime_error("Setpoint de robô inexistente");
    auto& unidade = encontrado->second;
    if (mensagem.geracao != unidade.leitura_sensores.geracao || mensagem.id_missao != unidade.leitura_sensores.id_missao ||
        mensagem.fase != unidade.leitura_sensores.fase || mensagem.revisao != unidade.leitura_sensores.revisao || unidade.paused) return {};
    if (unidade.leitura_sensores.falha || unidade.leitura_sensores.bateria == 0) return {};
    if (mensagem.passo != unidade.leitura_sensores.passo + 1 || mensagem.posicao != unidade.leitura_sensores.posicao ||
        distanciaManhattan(unidade.leitura_sensores.posicao, mensagem.destino) != 1 || !galpao_.podeTransitar(mensagem.destino)) {
        throw std::runtime_error("Setpoint físico inválido");
    }

    if (!unidade.leitura_sensores.acoplado) {
        unidade.leitura_sensores.posicao = mensagem.destino;
        unidade.leitura_sensores.passo = mensagem.passo;
        --unidade.leitura_sensores.bateria;
        return {leitura_sensores(unidade)};
    }

    auto& propostas = proposals_[mensagem.id_missao];
    propostas[mensagem.id_robo] = mensagem;
    if (unidade.integrantes.empty()) throw std::runtime_error("Equipe acoplada sem integrantes");
    for (const auto id : unidade.integrantes) {
        const auto membro = units_.find(id);
        const auto proposta = propostas.find(id);
        if (membro == units_.end() || proposta == propostas.end() || membro->second.paused || membro->second.leitura_sensores.falha ||
            membro->second.leitura_sensores.bateria <= 15 || !membro->second.leitura_sensores.acoplado ||
            membro->second.leitura_sensores.id_missao != mensagem.id_missao || proposta->second.destino != mensagem.destino ||
            proposta->second.passo != mensagem.passo) return {};
    }

    std::vector<Mensagem> sensores;
    for (const auto id : unidade.integrantes) {
        auto& membro = units_.at(id);
        membro.leitura_sensores.posicao = mensagem.destino;
        membro.leitura_sensores.passo = mensagem.passo;
        --membro.leitura_sensores.bateria;
        sensores.push_back(leitura_sensores(membro));
    }
    proposals_.erase(mensagem.id_missao);
    return sensores;
}

void AdaptadorHardwareLocal::registrarObservacao(const EstadoAtualRobo& estado) {
    const auto robo = units_.find(estado.id);
    if (robo != units_.end() && robo->second.leitura_sensores.geracao == estado.geracao &&
        robo->second.leitura_sensores.id_missao == estado.id_missao) robo->second.observed = estado.estado;
}

std::optional<std::vector<Mensagem>> AdaptadorHardwareLocal::injetarLeitura(int id, std::optional<int> bateria, std::optional<bool> falha) {
    if (bateria && (*bateria < 0 || *bateria > 100)) return std::nullopt;
    const auto robo = units_.find(id);
    if (robo == units_.end()) return std::nullopt;
    if (bateria) robo->second.leitura_sensores.bateria = *bateria;
    if (falha) robo->second.leitura_sensores.falha = *falha;
    if (robo->second.leitura_sensores.falha || robo->second.leitura_sensores.bateria <= 15) {
        proposals_.erase(robo->second.leitura_sensores.id_missao);
    }
    return std::vector<Mensagem>{leitura_sensores(robo->second)};
}

std::vector<Mensagem> AdaptadorHardwareLocal::avancarCiclo() {
    std::vector<Mensagem> sensores;
    const auto agora = std::chrono::steady_clock::now();
    for (auto& par : units_) {
        auto& unidade = par.second;
        if (unidade.observed != EstadoRobo::RECARREGANDO || unidade.leitura_sensores.falha || unidade.leitura_sensores.bateria >= 100 ||
            galpao_.obterCelula(unidade.leitura_sensores.posicao.x, unidade.leitura_sensores.posicao.y) != EstadoCelula::ESTACAO_RECARGA ||
            agora - unidade.last_charge < std::chrono::milliseconds(500)) continue;
        unidade.last_charge = agora;
        unidade.leitura_sensores.bateria = std::min(100, unidade.leitura_sensores.bateria + 5);
        sensores.push_back(leitura_sensores(unidade));
    }
    return sensores;
}

std::optional<Mensagem> AdaptadorHardwareLocal::lerLeitura(int id) const {
    const auto robo = units_.find(id);
    if (robo == units_.end()) return std::nullopt;
    return robo->second.leitura_sensores;
}
