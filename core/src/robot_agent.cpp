#include "robot_agent.hpp"
#include <algorithm>
#include <stdexcept>

AgenteRobo::AgenteRobo(int id, int x, int y, std::uint64_t geracao, Emissor emissor)
    : emissor_(std::move(emissor)), ultima_posicao_distancia_{x, y} {
    estado_.id = id; estado_.geracao = geracao; estado_.posicao = {x, y}; estado_.destino = {x, y};
    estado_.conectado = true; estado_.disponivel = true;
}
AgenteRobo::~AgenteRobo() { parar(); }
void AgenteRobo::iniciar() {
    std::lock_guard<std::mutex> lifecycle(ciclo_vida_);
    { std::lock_guard<std::mutex> lock(mutex_); if (executando_) return; executando_ = true; encerrando_ = false; erro_ = nullptr; }
    try { thread_navegacao_ = std::thread(&AgenteRobo::executarRotina, this, true); 
        thread_status_ = std::thread(&AgenteRobo::executarRotina, this, false); }
    catch (...) {
        { std::lock_guard<std::mutex> lock(mutex_); executando_ = false; }
        cv_.notify_all(); if (thread_navegacao_.joinable()) thread_navegacao_.join(); throw;
    }
}
void AgenteRobo::parar() {
    std::lock_guard<std::mutex> lifecycle(ciclo_vida_);
    { std::lock_guard<std::mutex> lock(mutex_); executando_ = false; encerrando_ = true; }
    cv_.notify_all();
    if (thread_navegacao_.joinable()) thread_navegacao_.join();
    if (thread_status_.joinable()) thread_status_.join();
}
EstadoAtualRobo AgenteRobo::capturarEstado() const { std::lock_guard<std::mutex> lock(mutex_); auto copia_estado = estado_; copia_estado.atualizado_em_ms = instanteUnixMs(); return copia_estado; }
void AgenteRobo::verificarFalha() const { std::lock_guard<std::mutex> lock(mutex_); if (erro_) std::rethrow_exception(erro_); }
void AgenteRobo::emitirMensagem(Mensagem mensagem) { if (emissor_) emissor_(std::move(mensagem)); }
void AgenteRobo::publicarEstado() { emitirMensagem(mensagemDoEstadoRobo(capturarEstado())); }
void AgenteRobo::executarRotina(bool navegacao) noexcept {
    try {
        const auto periodo = std::chrono::milliseconds(navegacao ? 100 : 500);
        auto proximo_instante = std::chrono::steady_clock::now();
        while (true) {
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait_until(lock, proximo_instante, [&] { return !executando_ || (!navegacao && (estado_alterado_ || !leituras_sensor_.empty())); });
                if (!executando_) break;
            }
            if (navegacao) avancarNavegacao(); else atualizarStatus();
            proximo_instante = std::chrono::steady_clock::now() + periodo;
        }
    } catch (...) {
        { std::lock_guard<std::mutex> lock(mutex_); erro_ = std::current_exception(); executando_ = false; }
        cv_.notify_all();
    }
}

void AgenteRobo::processarMensagem(const Mensagem& m) {
    Mensagem ack; bool confirmar_comando = false; bool leitura_alterada = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (m.id_robo != estado_.id || m.geracao != estado_.geracao) return;
        if (m.tipo == TipoMensagem::AcessoTrechoConcedido) {
            if (m.id_missao == estado_.id_missao && m.revisao == estado_.revisao && m.fase == estado_.fase && aguardando_passagem_ && m.passo == indice_rota_ + 1 && indice_rota_ < rota_.size() && m.destino == rota_[indice_rota_]) {
                passagem_liberada_ = m.aceito; aguardando_passagem_ = false;
            }
        } else if (m.tipo == TipoMensagem::AtualizarSensores) {
            if (m.bateria < 0 || m.bateria > 100 || m.sequencia_sensor <= estado_.sequencia_sensor) return;
            if (m.id_missao != estado_.id_missao || m.revisao != estado_.revisao) return;
            if (leituras_sensor_.size() >= 4096) throw std::runtime_error("Fila de leituras do robô esgotada");
            leituras_sensor_.push_back(m); estado_.sequencia_sensor = m.sequencia_sensor;
            if (m.falha && !estado_.falha) estado_antes_falha_ = estado_.estado;
            estado_.bateria = m.bateria; estado_.falha = m.falha; estado_.acoplado = m.acoplado;
            estado_.posicao = m.posicao;
            if (aguardando_posicao_ && m.passo == indice_rota_ + 1 && indice_rota_ < rota_.size() && rota_[indice_rota_] == m.posicao) {
                ++indice_rota_; estado_.passo = indice_rota_; aguardando_posicao_ = false;
            }
            if (m.falha) estado_.estado = EstadoRobo::COM_FALHA;
            else if (estado_.estado == EstadoRobo::COM_FALHA) {
                if (estado_.id_missao) estado_.estado = EstadoRobo::SUSPENSO;
                else if (estado_.fase == FaseMissao::INDO_PARA_RECARGA || estado_.fase == FaseMissao::RECARREGANDO) estado_.estado = estado_antes_falha_;
                else estado_.estado = EstadoRobo::DISPONIVEL;
            }
            if (estado_.estado == EstadoRobo::ACOPLANDO && m.acoplado) estado_.estado = EstadoRobo::AGUARDANDO_EQUIPE;
            if (estado_.estado == EstadoRobo::DESACOPLANDO && !m.acoplado) estado_.estado = EstadoRobo::AGUARDANDO_DESCARGA;
            estado_alterado_ = true; leitura_alterada = true;
        } else if (m.id_comando != 0) {
            comando_pendente_ = true; confirmar_comando = true; ack.tipo = TipoMensagem::ConfirmacaoComando; ack.id_comando = m.id_comando;
            ack.id_missao = m.id_missao; ack.fase = m.fase;
            if (m.id_comando <= ultimo_id_comando_) ack.aceito = m.id_comando == ultimo_id_comando_ && ultimo_comando_aceito_;
            else {
                ultimo_id_comando_ = m.id_comando; ack.aceito = true;
                const bool same_mission = m.id_missao == estado_.id_missao;
                const bool pickup = m.fase == FaseMissao::INDO_PARA_COLETA && !estado_.acoplado && estado_.estado == EstadoRobo::DISPONIVEL;
                const bool delivery = m.fase == FaseMissao::INDO_PARA_ENTREGA && estado_.acoplado && same_mission && estado_.estado == EstadoRobo::AGUARDANDO_EQUIPE;
                const bool charging = m.fase == FaseMissao::INDO_PARA_RECARGA && !estado_.acoplado && estado_.id_missao == 0;
                if (m.tipo == TipoMensagem::AtribuirMissao && !estado_.falha && (pickup || delivery || charging)) {
                    estado_.id_missao = m.id_missao; estado_.id_carga = m.id_carga; estado_.id_equipe = m.id_equipe; estado_.id_baia = m.id_baia;
                    estado_.fase = m.fase; estado_.destino = m.destino;
                    estado_.estado = charging ? EstadoRobo::INDO_PARA_RECARGA : (delivery ? EstadoRobo::INDO_PARA_ENTREGA : EstadoRobo::INDO_PARA_COLETA);
                    rota_ = m.rota; integrantes_ = m.integrantes; indice_rota_ = 0; estado_.passo = 0; aguardando_posicao_ = false; estado_.disponivel = false;
                } else if (m.tipo == TipoMensagem::CancelarMissao && same_mission && !estado_.acoplado) {
                    estado_.id_missao = 0; estado_.id_carga = 0; estado_.id_equipe = 0; estado_.id_baia = -1; estado_.fase = FaseMissao::NENHUMA;
                    estado_.estado = estado_.falha ? EstadoRobo::COM_FALHA : EstadoRobo::DISPONIVEL; rota_.clear(); aguardando_posicao_ = false;
                } else if (m.tipo == TipoMensagem::Acoplar && same_mission && !estado_.falha && !estado_.acoplado && estado_.estado == EstadoRobo::AGUARDANDO_EQUIPE) {
                    estado_.fase = FaseMissao::ACOPLANDO; estado_.estado = EstadoRobo::ACOPLANDO;
                } else if (m.tipo == TipoMensagem::Desacoplar && same_mission && !estado_.falha && estado_.acoplado && estado_.estado == EstadoRobo::AGUARDANDO_DESCARGA) {
                    estado_.fase = FaseMissao::DESACOPLANDO; estado_.estado = EstadoRobo::DESACOPLANDO;
                } else if (m.tipo == TipoMensagem::PausarMissao && same_mission) {
                    estado_.fase = FaseMissao::SUSPENSO; estado_.estado = estado_.falha ? EstadoRobo::COM_FALHA : EstadoRobo::SUSPENSO; aguardando_posicao_ = false;
                } else if (m.tipo == TipoMensagem::RetomarMissao && same_mission && !estado_.falha && estado_.bateria > 15 && estado_.estado == EstadoRobo::SUSPENSO) {
                    if (m.fase == FaseMissao::ACOPLANDO) {
                        estado_.fase = m.fase; estado_.estado = estado_.acoplado ? EstadoRobo::AGUARDANDO_EQUIPE : EstadoRobo::ACOPLANDO;
                    } else if (m.fase == FaseMissao::DESACOPLANDO) {
                        estado_.fase = m.fase; estado_.estado = estado_.acoplado ? EstadoRobo::DESACOPLANDO : EstadoRobo::AGUARDANDO_DESCARGA;
                    } else if (m.fase == FaseMissao::INDO_PARA_ENTREGA && estado_.acoplado) {
                        estado_.fase = FaseMissao::INDO_PARA_ENTREGA; estado_.estado = EstadoRobo::INDO_PARA_ENTREGA;
                        rota_ = m.rota; integrantes_ = m.integrantes; estado_.destino = m.destino; indice_rota_ = 0; estado_.passo = 0; aguardando_posicao_ = false;
                    } else ack.aceito = false;
                } else if (m.tipo != TipoMensagem::Encerrar) ack.aceito = false;
                if (ack.aceito && m.tipo != TipoMensagem::Encerrar) {
                    estado_.revisao = m.id_comando; aguardando_passagem_ = false; passagem_liberada_ = false;
                }
                ultimo_comando_aceito_ = ack.aceito; estado_alterado_ = true;
            }
        }
    }
    if (confirmar_comando) { emitirMensagem(ack); std::lock_guard<std::mutex> lock(mutex_); comando_pendente_ = false; }
    if (leitura_alterada) publicarEstado();
    cv_.notify_all();
}

void AgenteRobo::avancarNavegacao() {
    Mensagem movimento; bool enviar = false; bool houve_mudanca = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const bool em_navegacao = estado_.estado == EstadoRobo::INDO_PARA_COLETA || estado_.estado == EstadoRobo::INDO_PARA_ENTREGA || estado_.estado == EstadoRobo::INDO_PARA_RECARGA;
        if (encerrando_ || comando_pendente_ || estado_.falha || !em_navegacao || aguardando_posicao_ || aguardando_passagem_) return;
        if (estado_.bateria == 0 || (estado_.bateria <= 15 && estado_.estado != EstadoRobo::INDO_PARA_RECARGA)) return;
        if (indice_rota_ >= rota_.size()) {
            if (estado_.fase == FaseMissao::INDO_PARA_COLETA) { estado_.estado = EstadoRobo::AGUARDANDO_EQUIPE; estado_.fase = FaseMissao::COLETA_PRONTA; }
            else if (estado_.fase == FaseMissao::INDO_PARA_ENTREGA) { estado_.estado = EstadoRobo::AGUARDANDO_DESCARGA; estado_.fase = FaseMissao::ENTREGA_PRONTA; }
            else if (estado_.fase == FaseMissao::INDO_PARA_RECARGA) { estado_.estado = EstadoRobo::RECARREGANDO; estado_.fase = FaseMissao::RECARREGANDO; }
            houve_mudanca = true;
        } else {
            movimento = mensagemDoEstadoRobo(estado_); movimento.tipo = passagem_liberada_ ? TipoMensagem::DestinoNavegacao : TipoMensagem::SolicitarAcessoTrecho;
            movimento.destino = rota_[indice_rota_]; movimento.passo = indice_rota_ + 1; movimento.integrantes = integrantes_;
            movimento.quantidade_integrantes = estado_.acoplado ? std::max(1, static_cast<int>(integrantes_.size())) : 1;
            if (passagem_liberada_) { aguardando_posicao_ = true; passagem_liberada_ = false; }
            else aguardando_passagem_ = true;
            enviar = true;
        }
    }
    if (enviar) emitirMensagem(movimento); if (houve_mudanca) publicarEstado();
}
void AgenteRobo::atualizarStatus() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        while (!leituras_sensor_.empty()) {
            const auto& leitura_sensores = leituras_sensor_.front();
            estado_.distancia += static_cast<std::uint64_t>(distanciaManhattan(ultima_posicao_distancia_, leitura_sensores.posicao));
            ultima_posicao_distancia_ = leitura_sensores.posicao; leituras_sensor_.pop_front();
        }
        if (estado_.falha) estado_.estado = EstadoRobo::COM_FALHA;
        if (estado_.estado == EstadoRobo::RECARREGANDO && estado_.bateria >= 100 && !estado_.falha) {
            estado_.estado = EstadoRobo::DISPONIVEL; estado_.fase = FaseMissao::NENHUMA; estado_.id_missao = 0;
            rota_.clear(); aguardando_posicao_ = false; aguardando_passagem_ = false; passagem_liberada_ = false;
        }
        estado_.disponivel = estado_.estado == EstadoRobo::DISPONIVEL && !estado_.falha && !estado_.acoplado && estado_.bateria > 15;
        estado_alterado_ = false;
    }
    publicarEstado();
}
void AgenteRobo::atribuirMissao(const std::string&, int x, int y) {
    Mensagem m; const auto s = capturarEstado(); m.tipo = TipoMensagem::AtribuirMissao; m.id_robo = s.id; m.geracao = s.geracao;
    { std::lock_guard<std::mutex> lock(mutex_); m.id_comando = ultimo_id_comando_ + 1; }
    m.id_missao = 1; m.fase = FaseMissao::INDO_PARA_COLETA; m.destino = {x, y};
    Posicao p = s.posicao;
    while (p.x != x) { p.x += p.x < x ? 1 : -1; m.rota.push_back(p); }
    while (p.y != y) { p.y += p.y < y ? 1 : -1; m.rota.push_back(p); }
    processarMensagem(m);
}
void AgenteRobo::atualizarEstadoRobo(int bateria, bool falha) {
    auto s = capturarEstado(); Mensagem m = mensagemDoEstadoRobo(s); m.tipo = TipoMensagem::AtualizarSensores;
    { std::lock_guard<std::mutex> lock(mutex_); m.sequencia_sensor = std::max(++sequencia_sensor_local_, estado_.sequencia_sensor + 1); }
    m.bateria = bateria; m.falha = falha; processarMensagem(m);
}
