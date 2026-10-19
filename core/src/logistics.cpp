#include "logistics.hpp"
#include <algorithm>
#include <stdexcept>

OrganizadorLogistica::OrganizadorLogistica(const MapaGalpao& galpao, const EstadoFrota& frota, std::vector<Carga> carga, Emissor emissor, Observador observador)
    : galpao_(galpao), frota_(frota), planejador_(galpao), emissor_(std::move(emissor)), observador_(std::move(observador)), baias_(galpao.baias()) {
    for (auto& pair : baias_) pair.second.fila_cargas.clear();
    for (auto c : carga) {
        if (c.id <= 0 || c.tamanho < 1 || c.tamanho > 4 || !baias_.count(c.id_baia) || !galpao.destinos().count(c.id_destino) || cargas_.count(c.id)) throw std::invalid_argument("Cadastro de carga inválido");
        c.estado = EstadoCarga::PENDENTE; c.id_missao = 0; baias_.at(c.id_baia).fila_cargas.push_back(c.id); cargas_.emplace(c.id, std::move(c));
    }
}
OrganizadorLogistica::~OrganizadorLogistica() { parar(); }
void OrganizadorLogistica::iniciar() {
    std::lock_guard<std::mutex> lifecycle(ciclo_vida_);
    std::lock_guard<std::mutex> lock(mutex_fila_);
    if (executando_) return; executando_ = true; erro_ = nullptr;
    try { thread_ = std::thread(&OrganizadorLogistica::executarRotina, this); } catch (...) { executando_ = false; throw; }
}
void OrganizadorLogistica::parar() {
    std::lock_guard<std::mutex> lifecycle(ciclo_vida_);
    { std::lock_guard<std::mutex> lock(mutex_fila_); executando_ = false; }
    cv_.notify_all(); if (thread_.joinable()) thread_.join();
}
void OrganizadorLogistica::aoReceberMensagem(const Mensagem& m) {
    { std::lock_guard<std::mutex> lock(mutex_fila_); if (eventos_.size() >= 4096) throw std::runtime_error("Fila da logística esgotada"); eventos_.push_back(m); }
    cv_.notify_all();
}
void OrganizadorLogistica::solicitarRetomada(IdEntidade id) { Mensagem m; m.tipo = TipoMensagem::RetomarMissao; m.id_missao = id; aoReceberMensagem(m); }
void OrganizadorLogistica::executarRotina() noexcept {
    try {
        while (true) {
            {
                std::unique_lock<std::mutex> lock(mutex_fila_);
                cv_.wait_for(lock, std::chrono::milliseconds(200), [&] { return !executando_ || !eventos_.empty(); });
                if (!executando_) break;
            }
            processarUmaVez();
        }
    } catch (...) { std::lock_guard<std::mutex> lock(mutex_fila_); erro_ = std::current_exception(); executando_ = false; }
}
void OrganizadorLogistica::processarUmaVez() {
    std::deque<Mensagem> eventos;
    { std::lock_guard<std::mutex> lock(mutex_fila_); eventos.swap(eventos_); }
    std::vector<std::pair<int, Mensagem>> acoes;
    std::vector<AvisoLogistica> avisos;
    {
        std::lock_guard<std::mutex> lock(mutex_estado_);
        for (const auto& m : eventos) registrarMensagem(m);
        for (auto& pair : missoes_) avancarMissao(pair.second);
        agendarCargas(); acoes.swap(acoes_); avisos.swap(avisos_);
    }
    for (auto& action : acoes) if (!emissor_(action.first, action.second)) {
        Mensagem disconnected; disconnected.tipo = TipoMensagem::RoboDesconectado; disconnected.id_robo = action.first;
        const auto robo = frota_.obter(action.first); if (robo) disconnected.geracao = robo->geracao;
        disconnected.texto = "Não foi possível enviar comando"; aoReceberMensagem(disconnected);
    }
    for (const auto& item : avisos) if (observador_) observador_(item);
}
void OrganizadorLogistica::agendarCargas() {
    std::set<int> robos_reservados;
    for (const auto& pair : missoes_) if (pair.second.fase != FaseMissao::NENHUMA) robos_reservados.insert(pair.second.integrantes.begin(), pair.second.integrantes.end());
    const auto robos = frota_.capturarEstado();
    std::set<int> robos_que_precisam_recarga;
    for (auto& par_baia : baias_) {
        auto& baia = par_baia.second;
        if (baia.fila_cargas.empty()) continue;
        auto& carga = cargas_.at(baia.fila_cargas.front());
        if (carga.estado != EstadoCarga::PENDENTE) continue;
        const auto destino = galpao_.destinos().at(carga.id_destino).posicao;
        auto rota_entrega = planejador_.calcularRota(baia.posicao, destino, carga.tamanho);
        const auto rota_carregador = planejador_.carregadorMaisProximo(destino);
        if (!rota_entrega || !rota_carregador) { carga.motivo = "Sem rota compatível com a carga ou sem carregador alcançável"; continue; }
        std::vector<EstadoAtualRobo> equipe_escolhida;
        for (const auto& robo : robos) {
            if (!robo.conectado || !robo.disponivel || robo.falha || robo.acoplado || robos_reservados.count(robo.id) || em_recarga_.count(robo.id)) continue;
            const auto rota_coleta = planejador_.calcularRota(robo.posicao, baia.posicao);
            if (!rota_coleta) continue;
            const auto custo_bateria = rota_coleta->size() + rota_entrega->size() + rota_carregador->second.size() + 15;
            if (static_cast<std::size_t>(robo.bateria) <= custo_bateria) { robos_que_precisam_recarga.insert(robo.id); continue; }
            equipe_escolhida.push_back(robo); if (equipe_escolhida.size() == static_cast<std::size_t>(carga.tamanho)) break;
        }
        if (equipe_escolhida.size() != static_cast<std::size_t>(carga.tamanho)) { carga.motivo = "Aguardando equipe completa com rota e bateria suficientes"; continue; }
        MissaoEquipe missao; missao.id = ++proxima_missao_; missao.id_carga = carga.id; missao.rota_entrega = *rota_entrega;
        for (const auto& robo : equipe_escolhida) { missao.integrantes.push_back(robo.id); missao.geracoes[robo.id] = robo.geracao; robos_reservados.insert(robo.id); }
        carga.estado = EstadoCarga::REUNINDO_EQUIPE; carga.id_missao = missao.id; carga.motivo.clear();
        auto& missao_registrada = missoes_.emplace(missao.id, std::move(missao)).first->second;
        for (const auto& robo : equipe_escolhida) {
            Mensagem m; m.tipo = TipoMensagem::AtribuirMissao; m.fase = FaseMissao::INDO_PARA_COLETA; m.id_missao = missao_registrada.id;
            m.id_carga = carga.id; m.id_equipe = missao_registrada.id; m.id_baia = baia.id; m.destino = baia.posicao;
            m.rota = *planejador_.calcularRota(robo.posicao, baia.posicao); m.integrantes = {robo.id};
            acoes_.emplace_back(robo.id, std::move(m));
        }
        registrarAviso(AvisoLogistica::TipoAviso::MissaoAtribuida, missao_registrada);
    }
    for (const auto& robo : robos) {
        if (em_recarga_.count(robo.id) && (!robo.conectado || (robo.estado == EstadoRobo::DISPONIVEL && robo.bateria == 100))) em_recarga_.erase(robo.id);
        if (!robo.conectado || robo.falha || robo.acoplado || robo.id_missao || robos_reservados.count(robo.id) || em_recarga_.count(robo.id) || robo.estado != EstadoRobo::DISPONIVEL || robo.bateria == 100) continue;
        if (robo.bateria > 15 && !robos_que_precisam_recarga.count(robo.id)) continue;
        const auto charger = planejador_.carregadorMaisProximo(robo.posicao);
        if (!charger || charger->second.size() > static_cast<std::size_t>(robo.bateria)) continue;
        Mensagem m; m.tipo = TipoMensagem::AtribuirMissao; m.fase = FaseMissao::INDO_PARA_RECARGA;
        m.destino = charger->first; m.rota = charger->second; m.integrantes = {robo.id};
        em_recarga_.insert(robo.id); acoes_.emplace_back(robo.id, std::move(m));
    }
}
void OrganizadorLogistica::registrarMensagem(const Mensagem& m) {
    if (m.tipo == TipoMensagem::RetomarMissao && m.id_robo == -1) {
        const auto it = missoes_.find(m.id_missao); if (it != missoes_.end()) retomar(it->second); return;
    }
    const bool lerLeitura = m.tipo == TipoMensagem::AtualizarSensores || m.tipo == TipoMensagem::EstadoAtualRobo || m.tipo == TipoMensagem::RoboPronto;
    if (lerLeitura) {
        const auto anterior = saude_.find(m.id_robo);
        if (anterior != saude_.end()) {
            const auto& old = anterior->second;
            if (m.geracao < old.geracao || (m.geracao == old.geracao && (m.sequencia_sensor < old.sequencia_sensor || m.revisao < old.revisao))) return;
        }
        saude_[m.id_robo] = estadoRoboDaMensagem(m);
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
    if (m.tipo != TipoMensagem::AtualizarSensores) missao.relatos[m.id_robo] = estadoRoboDaMensagem(m);
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
    if (missao.fase == FaseMissao::INDO_PARA_COLETA && all([](const EstadoAtualRobo& s) { return s.estado == EstadoRobo::AGUARDANDO_EQUIPE && s.fase == FaseMissao::COLETA_PRONTA && !s.falha; })) {
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
        if (fila.empty() || fila.front() != carga.id) throw std::logic_error("Fila da baía inconsistente");
        fila.pop_front(); registrarAviso(AvisoLogistica::TipoAviso::CargaEntregue, missao);
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
void OrganizadorLogistica::retomar(MissaoEquipe& missao) {
    if (missao.fase != FaseMissao::SUSPENSO) return;
    auto& carga = cargas_.at(missao.id_carga);
    std::optional<Posicao> posicao;
    bool all_coupled = true;
    for (const auto id : missao.integrantes) {
        const auto robo = frota_.obter(id);
        const auto saude = saude_.find(id);
        const bool confirmed = robo && saude != saude_.end() && saude->second.geracao == robo->geracao &&
            robo->sequencia_sensor >= saude->second.sequencia_sensor && robo->revisao >= saude->second.revisao &&
            !saude->second.falha && saude->second.bateria > 15;
        if (!robo || !confirmed || !robo->conectado || robo->geracao != missao.geracoes.at(id) || robo->falha || robo->bateria <= 15 || robo->estado != EstadoRobo::SUSPENSO) {
            carga.motivo = "Recupere todos os integrantes e aguarde confirmação de suspensão"; return;
        }
        if (posicao && robo->posicao != *posicao) { carga.motivo = "Equipe fora da mesma posição lógica"; return; }
        posicao = robo->posicao; all_coupled = all_coupled && robo->acoplado;
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
std::vector<Carga> OrganizadorLogistica::cargos() const { std::lock_guard<std::mutex> lock(mutex_estado_); std::vector<Carga> result; for (const auto& c : cargas_) result.push_back(c.second); return result; }
std::vector<MissaoEquipe> OrganizadorLogistica::missoes() const { std::lock_guard<std::mutex> lock(mutex_estado_); std::vector<MissaoEquipe> result; for (const auto& m : missoes_) result.push_back(m.second); return result; }
bool OrganizadorLogistica::todasCargasEntregues() const { for (const auto& c : cargos()) if (c.estado != EstadoCarga::ENTREGUE) return false; return true; }
void OrganizadorLogistica::verificarFalha() const { std::lock_guard<std::mutex> lock(mutex_fila_); if (erro_) std::rethrow_exception(erro_); }
