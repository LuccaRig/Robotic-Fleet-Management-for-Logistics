#include "logistics.hpp"
#include <algorithm>
#include <set>
#include <stdexcept>

OrganizadorLogistica::OrganizadorLogistica(const MapaGalpao& galpao, std::vector<Carga> carga)
    : galpao_(galpao), planejador_(galpao), baias_(galpao.baias()) {
    for (auto& pair : baias_) pair.second.fila_cargas.clear();
    for (auto& c : carga) adicionarCarga(std::move(c));
}
void OrganizadorLogistica::adicionarCarga(Carga carga) {
    adicionarCargas({std::move(carga)});
}
void OrganizadorLogistica::adicionarCargas(std::vector<Carga> cargas) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::set<IdEntidade> ids;
    for (const auto& carga : cargas) {
        if (carga.id <= 0 || carga.tamanho < 1 || carga.tamanho > 4 || !baias_.count(carga.id_baia) ||
            !galpao_.destinos().count(carga.id_destino) || cargas_.count(carga.id) || !ids.insert(carga.id).second) {
            throw std::invalid_argument("Cadastro de carga inválido ou ID duplicado");
        }
    }

    std::vector<IdEntidade> inseridas;
    inseridas.reserve(cargas.size());
    try {
        for (auto& carga : cargas) {
            const auto id = carga.id;
            adicionarCargaSemBloqueio(std::move(carga));
            inseridas.push_back(id);
        }
    } catch (...) {
        for (const auto id : inseridas) {
            const auto encontrada = cargas_.find(id);
            if (encontrada == cargas_.end()) continue;
            auto& fila = baias_.at(encontrada->second.id_baia).fila_cargas;
            fila.erase(std::find(fila.begin(), fila.end(), id));
            cargas_.erase(encontrada);
        }
        throw;
    }
}
void OrganizadorLogistica::adicionarCargaSemBloqueio(Carga carga) {
    if (carga.id <= 0 || carga.tamanho < 1 || carga.tamanho > 4 || !baias_.count(carga.id_baia) ||
        !galpao_.destinos().count(carga.id_destino) || cargas_.count(carga.id)) throw std::invalid_argument("Cadastro de carga inválido ou ID duplicado");
    carga.estado = EstadoCarga::PENDENTE;
    carga.id_missao = 0;
    carga.motivo.clear();
    auto& fila = baias_.at(carga.id_baia).fila_cargas;
    fila.push_back(carga.id);
    bool remover_da_fila_se_houver_erro = true;
    try {
        if (!cargas_.emplace(carga.id, std::move(carga)).second) {
            fila.pop_back();
            remover_da_fila_se_houver_erro = false;
            throw std::invalid_argument("Cadastro de carga inválido ou ID duplicado");
        }
        remover_da_fila_se_houver_erro = false;
    } catch (...) {
        if (remover_da_fila_se_houver_erro) fila.pop_back();
        throw;
    }
}
void OrganizadorLogistica::atualizarMissoes(const std::vector<Mensagem>& mensagens, const std::vector<EstadoAtualRobo>& frota) {
    for (const auto& mensagem : mensagens) registrarMensagem(mensagem, frota);
    for (auto& par : missoes_) avancarMissao(par.second);
}
void OrganizadorLogistica::retomarMissao(IdEntidade id, const std::vector<Mensagem>& mensagens, const std::vector<EstadoAtualRobo>& frota) {
    std::lock_guard<std::mutex> lock(mutex_);
    atualizarMissoes(mensagens, frota);
    const auto missao = missoes_.find(id);
    if (missao == missoes_.end()) throw std::invalid_argument("Missão inexistente");
    if (missao->second.fase != FaseMissao::SUSPENSO) throw std::runtime_error("Missão não está suspensa");
    retomar(missao->second, frota);
    if (missao->second.fase == FaseMissao::SUSPENSO) throw std::runtime_error(cargas_.at(missao->second.id_carga).motivo);
}
ResultadoCicloLogistica OrganizadorLogistica::processarCiclo(const std::vector<Mensagem>& mensagens, const std::vector<EstadoAtualRobo>& frota, const std::set<int>& bloqueados) {
    std::lock_guard<std::mutex> lock(mutex_);
    atualizarMissoes(mensagens, frota);
    agendarCargas(frota, bloqueados);
    ResultadoCicloLogistica resultado;
    resultado.comandos.swap(acoes_);
    resultado.eventos.swap(avisos_);
    return resultado;
}
void OrganizadorLogistica::agendarCargas(const std::vector<EstadoAtualRobo>& frota, const std::set<int>& bloqueados) {
    std::set<int> robos_reservados = bloqueados;
    for (const auto& pair : missoes_) if (pair.second.fase != FaseMissao::NENHUMA) robos_reservados.insert(pair.second.integrantes.begin(), pair.second.integrantes.end());
    bool houve_alocacao;
    do {
        houve_alocacao = false;
        for (auto& par_baia : baias_) {
            auto& baia = par_baia.second;
            for (const auto id_carga : baia.fila_cargas) {
                auto& carga = cargas_.at(id_carga);
                if (carga.estado == EstadoCarga::ENTREGUE) continue;

                MissaoEquipe* missao = nullptr;
                const bool nova_missao = carga.estado == EstadoCarga::PENDENTE;
                if (nova_missao) {
                    if (carga.id_missao != 0) throw std::logic_error("Carga pendente com missão associada");
                } else if (carga.estado == EstadoCarga::REUNINDO_EQUIPE) {
                    const auto encontrada = missoes_.find(carga.id_missao);
                    if (encontrada == missoes_.end()) throw std::logic_error("Missão da carga não encontrada");
                    missao = &encontrada->second;
                    if (missao->integrantes.size() >= static_cast<std::size_t>(carga.tamanho)) continue;
                } else {
                    const auto encontrada = missoes_.find(carga.id_missao);
                    if (encontrada != missoes_.end() &&
                        encontrada->second.integrantes.size() >= static_cast<std::size_t>(carga.tamanho)) continue;
                    break;
                }

                const auto destino = galpao_.destinos().at(carga.id_destino).posicao;
                auto rota_entrega = planejador_.calcularRota(baia.posicao, destino, carga.tamanho);
                const auto rota_carregador = planejador_.carregadorMaisProximo(destino);
                if (!rota_entrega || !rota_carregador) {
                    carga.motivo = "Sem rota compatível com a carga ou sem carregador alcançável";
                    break;
                }
                if (missao && missao->rota_entrega != *rota_entrega) {
                    throw std::logic_error("Rota da missão da carga foi alterada");
                }

                const auto integrantes_atuais = missao ? missao->integrantes.size() : 0;
                const auto faltantes = static_cast<std::size_t>(carga.tamanho) - integrantes_atuais;
                std::vector<EstadoAtualRobo> equipe_escolhida;
                for (const auto& robo : frota) {
                    if (!robo.disponivel || !disponivelParaCarga(robo) || robos_reservados.count(robo.id) || em_recarga_.count(robo.id)) continue;
                    const auto rota_coleta = planejador_.calcularRota(robo.posicao, baia.posicao);
                    if (!rota_coleta) continue;
                    const auto custo_bateria = rota_coleta->size() + rota_entrega->size() + rota_carregador->second.size() + 15;
                    if (static_cast<std::size_t>(robo.bateria) <= custo_bateria) continue;
                    equipe_escolhida.push_back(robo);
                    if (equipe_escolhida.size() == faltantes) break;
                }
                if (equipe_escolhida.empty()) {
                    carga.motivo = "Aguardando robôs livres com rota e bateria suficientes";
                    break;
                }

                if (nova_missao) {
                    MissaoEquipe nova; nova.id = ++proxima_missao_; nova.id_carga = carga.id; nova.rota_entrega = *rota_entrega;
                    missao = &missoes_.emplace(nova.id, std::move(nova)).first->second;
                    carga.estado = EstadoCarga::REUNINDO_EQUIPE;
                    carga.id_missao = missao->id;
                }
                for (const auto& robo : equipe_escolhida) {
                    missao->integrantes.push_back(robo.id);
                    missao->geracoes[robo.id] = robo.geracao;
                    robos_reservados.insert(robo.id);
                    houve_alocacao = true;
                    Mensagem m; m.tipo = TipoMensagem::AtribuirMissao; m.fase = FaseMissao::INDO_PARA_COLETA; m.id_missao = missao->id;
                    m.id_carga = carga.id; m.id_equipe = missao->id; m.id_baia = baia.id; m.destino = baia.posicao;
                    m.rota = *planejador_.calcularRota(robo.posicao, baia.posicao); m.integrantes = {robo.id};
                    acoes_.emplace_back(robo.id, std::move(m));
                }
                if (nova_missao) registrarAviso(AvisoLogistica::TipoAviso::MissaoAtribuida, *missao);
                if (missao->integrantes.size() < static_cast<std::size_t>(carga.tamanho)) {
                    carga.motivo = "Aguardando mais robôs para completar a equipe";
                    break;
                }
                carga.motivo.clear();
                break;
            }
        }
    } while (houve_alocacao);
    for (const auto& robo : frota) {
        if (em_recarga_.count(robo.id) && (!robo.conectado || (robo.estado == EstadoRobo::DISPONIVEL && robo.bateria == 100))) em_recarga_.erase(robo.id);
        if (!robo.conectado || !roboLivre(robo) || robos_reservados.count(robo.id) || em_recarga_.count(robo.id) || robo.bateria == 100) continue;
        if (robo.modo == ModoOperacao::MANUAL && robo.bateria > 15) continue;
        const auto charger = planejador_.carregadorMaisProximo(robo.posicao);
        if (!charger || charger->second.size() > static_cast<std::size_t>(robo.bateria)) continue;
        Mensagem m; m.tipo = TipoMensagem::AtribuirMissao; m.fase = FaseMissao::INDO_PARA_RECARGA;
        m.destino = charger->first; m.rota = charger->second; m.integrantes = {robo.id};
        em_recarga_.insert(robo.id); acoes_.emplace_back(robo.id, std::move(m));
    }
}
void OrganizadorLogistica::registrarMensagem(const Mensagem& m, const std::vector<EstadoAtualRobo>& frota) {
    if (m.tipo == TipoMensagem::RetomarMissao && m.id_robo == -1) {
        const auto it = missoes_.find(m.id_missao); if (it != missoes_.end()) retomar(it->second, frota); return;
    }
    if (m.tipo == TipoMensagem::RoboDesconectado) {
        for (auto& pair : missoes_) {
            auto& missao = pair.second;
            if (missao.geracoes.count(m.id_robo) && missao.geracoes.at(m.id_robo) == m.geracao)
                interromper(missao, "Integrante desconectado: " + std::to_string(m.id_robo));
        }
        em_recarga_.erase(m.id_robo); return;
    }
    if (m.tipo == TipoMensagem::ConfirmacaoComando && !m.aceito && m.id_missao == 0) em_recarga_.erase(m.id_robo);
    auto it = missoes_.find(m.id_missao);
    if (it == missoes_.end() || !it->second.geracoes.count(m.id_robo) || it->second.geracoes.at(m.id_robo) != m.geracao) return;
    auto& missao = it->second;
    if (m.tipo == TipoMensagem::ConfirmacaoComando) {
        if (!m.aceito) interromper(missao, "Comando rejeitado pelo robô " + std::to_string(m.id_robo));
        return;
    }
    if (m.tipo != TipoMensagem::EstadoAtualRobo && m.tipo != TipoMensagem::RoboPronto && m.tipo != TipoMensagem::AtualizarSensores) return;
    if (m.acoplado) { missao.ja_acoplou = true; missao.integrantes_acoplados.insert(m.id_robo); }
    if (m.falha || m.bateria <= 15) { interromper(missao, m.falha ? "Falha em integrante da equipe" : "Bateria insuficiente em integrante da equipe"); return; }
    missao.relatos[m.id_robo] = estadoRoboDaMensagem(m);
}
void OrganizadorLogistica::avancarMissao(MissaoEquipe& missao) {
    if (missao.fase == FaseMissao::NENHUMA || missao.fase == FaseMissao::SUSPENSO) return;
    auto all = [&](auto predicate) {
        for (const auto id : missao.integrantes) {
            const auto it = missao.relatos.find(id);
            if (it == missao.relatos.end() || !predicate(it->second)) return false;
        }
        return true;
    };
    auto& carga = cargas_.at(missao.id_carga);
    // Cada transição espera a confirmação de todos os integrantes antes de avançar a equipe.
    if (missao.fase == FaseMissao::INDO_PARA_COLETA &&
        missao.integrantes.size() == static_cast<std::size_t>(carga.tamanho) &&
        all([](const EstadoAtualRobo& s) { return s.estado == EstadoRobo::AGUARDANDO_EQUIPE && s.fase == FaseMissao::COLETA_PRONTA && !s.falha; })) {
        registrarAviso(AvisoLogistica::TipoAviso::EquipeReunida, missao); carga.estado = EstadoCarga::ACOPLANDO;
        executarComando(missao, TipoMensagem::Acoplar, FaseMissao::ACOPLANDO);
    } else if (missao.fase == FaseMissao::ACOPLANDO && all([](const EstadoAtualRobo& s) { return s.acoplado && s.estado == EstadoRobo::AGUARDANDO_EQUIPE && !s.falha; })) {
        missao.ja_acoplou = true; carga.estado = EstadoCarga::EM_TRANSPORTE;
        registrarAviso(AvisoLogistica::TipoAviso::EquipeAcoplada, missao);
        executarComando(missao, TipoMensagem::AtribuirMissao, FaseMissao::INDO_PARA_ENTREGA);
        registrarAviso(AvisoLogistica::TipoAviso::TransporteIniciado, missao);
    } else if (missao.fase == FaseMissao::INDO_PARA_ENTREGA && all([](const EstadoAtualRobo& s) { return s.acoplado && s.estado == EstadoRobo::AGUARDANDO_DESCARGA && s.fase == FaseMissao::ENTREGA_PRONTA && !s.falha; })) {
        carga.estado = EstadoCarga::DESACOPLANDO; executarComando(missao, TipoMensagem::Desacoplar, FaseMissao::DESACOPLANDO);
    } else if (missao.fase == FaseMissao::DESACOPLANDO && all([](const EstadoAtualRobo& s) { return !s.acoplado && s.estado == EstadoRobo::AGUARDANDO_DESCARGA && s.fase == FaseMissao::DESACOPLANDO && !s.falha; })) {
        carga.estado = EstadoCarga::ENTREGUE; carga.motivo.clear();
        auto& fila = baias_.at(carga.id_baia).fila_cargas;
        const auto carga_na_fila = std::find(fila.begin(), fila.end(), carga.id);
        if (carga_na_fila == fila.end()) throw std::logic_error("Carga não encontrada na fila da baía");
        fila.erase(carga_na_fila); registrarAviso(AvisoLogistica::TipoAviso::CargaEntregue, missao);
        executarComando(missao, TipoMensagem::CancelarMissao, FaseMissao::NENHUMA);
    }
}
void OrganizadorLogistica::interromper(MissaoEquipe& missao, const std::string& motivo) {
    if (missao.fase == FaseMissao::NENHUMA || missao.fase == FaseMissao::SUSPENSO) return;
    auto& carga = cargas_.at(missao.id_carga); carga.motivo = motivo;
    if (!missao.ja_acoplou && missao.fase == FaseMissao::INDO_PARA_COLETA) {
        carga.estado = EstadoCarga::PENDENTE; carga.id_missao = 0;
        executarComando(missao, TipoMensagem::CancelarMissao, FaseMissao::NENHUMA); registrarAviso(AvisoLogistica::TipoAviso::MissaoCancelada, missao);
    } else {
        missao.fase_suspensa = missao.fase; carga.estado = EstadoCarga::SUSPENSO;
        executarComando(missao, TipoMensagem::PausarMissao, FaseMissao::SUSPENSO); registrarAviso(AvisoLogistica::TipoAviso::MissaoSuspensa, missao);
    }
}
void OrganizadorLogistica::retomar(MissaoEquipe& missao, const std::vector<EstadoAtualRobo>& frota) {
    if (missao.fase != FaseMissao::SUSPENSO) return;
    auto& carga = cargas_.at(missao.id_carga);
    std::optional<Posicao> posicao;
    bool all_coupled = true;
    for (const auto id : missao.integrantes) {
        const auto encontrado = std::find_if(frota.begin(), frota.end(), [id](const EstadoAtualRobo& estado) { return estado.id == id; });
        if (encontrado == frota.end() || !encontrado->conectado || encontrado->geracao != missao.geracoes.at(id) || encontrado->falha || encontrado->bateria <= 15 || encontrado->estado != EstadoRobo::SUSPENSO) {
            carga.motivo = "Recupere todos os integrantes e aguarde confirmação de suspensão"; return;
        }
        const auto& robo = *encontrado;
        if (posicao && robo.posicao != *posicao) { carga.motivo = "Equipe fora da mesma posição lógica"; return; }
        posicao = robo.posicao; all_coupled = all_coupled && robo.acoplado;
    }
    if (missao.fase_suspensa == FaseMissao::ACOPLANDO) {
        carga.estado = EstadoCarga::ACOPLANDO; executarComando(missao, TipoMensagem::RetomarMissao, FaseMissao::ACOPLANDO);
    } else if (missao.fase_suspensa == FaseMissao::DESACOPLANDO) {
        carga.estado = EstadoCarga::DESACOPLANDO; executarComando(missao, TipoMensagem::RetomarMissao, FaseMissao::DESACOPLANDO);
    } else {
        if (!all_coupled) { carga.motivo = "Transporte exige que todos os integrantes estejam acoplados"; return; }
        auto rota = planejador_.calcularRota(*posicao, galpao_.destinos().at(carga.id_destino).posicao, carga.tamanho);
        if (!rota) { carga.motivo = "Sem rota para retomar o transporte"; return; }
        missao.rota_entrega = *rota; carga.estado = EstadoCarga::EM_TRANSPORTE;
        executarComando(missao, TipoMensagem::RetomarMissao, FaseMissao::INDO_PARA_ENTREGA);
    }
    carga.motivo.clear(); registrarAviso(AvisoLogistica::TipoAviso::MissaoRetomada, missao);
}
void OrganizadorLogistica::executarComando(MissaoEquipe& missao, TipoMensagem tipo, FaseMissao fase) {
    const auto& carga = cargas_.at(missao.id_carga);
    missao.fase = fase;
    for (const auto id : missao.integrantes) {
        Mensagem m; m.tipo = tipo; m.fase = fase; m.id_missao = missao.id; m.id_carga = carga.id;
        m.id_equipe = missao.id; m.id_baia = carga.id_baia; m.integrantes = missao.integrantes; m.quantidade_integrantes = carga.tamanho;
        if (tipo == TipoMensagem::AtribuirMissao || tipo == TipoMensagem::RetomarMissao) {
            m.destino = galpao_.destinos().at(carga.id_destino).posicao; m.rota = missao.rota_entrega;
        }
        acoes_.emplace_back(id, std::move(m));
    }
    missao.relatos.clear();
}
void OrganizadorLogistica::registrarAviso(AvisoLogistica::TipoAviso tipo, const MissaoEquipe& missao) { avisos_.push_back({tipo, cargas_.at(missao.id_carga), missao, instanteUnixMs()}); }
std::vector<Carga> OrganizadorLogistica::cargos() const { std::lock_guard<std::mutex> lock(mutex_); std::vector<Carga> result; for (const auto& c : cargas_) result.push_back(c.second); return result; }
std::vector<MissaoEquipe> OrganizadorLogistica::missoes() const { std::lock_guard<std::mutex> lock(mutex_); std::vector<MissaoEquipe> result; for (const auto& m : missoes_) result.push_back(m.second); return result; }
bool OrganizadorLogistica::todasCargasEntregues() const { std::lock_guard<std::mutex> lock(mutex_); for (const auto& c : cargas_) if (c.second.estado != EstadoCarga::ENTREGUE) return false; return true; }
