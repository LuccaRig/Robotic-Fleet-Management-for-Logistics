#include "event_recorder.hpp"

GravadorEventos::GravadorEventos(BufferEventos& buffer, Observador observador) : buffer_(buffer), observador_(std::move(observador)) {}
void GravadorEventos::registrar(EventoRobo event) { buffer_.adicionarEvento(event); if (observador_) observador_(event); }
void GravadorEventos::aoReceberEventoRobo(const Mensagem& m) {
    std::vector<EventoRobo> eventos;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto add = [&](TipoEvento tipo, const std::string& descricao, Posicao posicao) {
            EventoRobo e(m.id_robo, static_cast<int>(m.id_equipe), m.id_baia, posicao, tipo, descricao, "");
            e.rastrear(m.id_carga, m.id_missao, m.geracao, "robo:" + std::to_string(m.id_robo), m.sequencia ? m.sequencia : ++sequencia_, m.ocorrido_em_ms ? m.ocorrido_em_ms : instanteUnixMs());
            eventos.push_back(std::move(e));
        };
        const auto old = estados_anteriores_.find(m.id_robo);
        const bool known = old != estados_anteriores_.end() && old->second.geracao == m.geracao;
        const auto posicao = known ? old->second.posicao : m.posicao;
        if (m.tipo == TipoMensagem::SaudacaoRobo) add(TipoEvento::ROBO_CRIADO, "Processo do robô criado", m.posicao);
        else if (m.tipo == TipoMensagem::RoboPronto) add(TipoEvento::ROBO_PRONTO, "Robô pronto para receber tarefas", m.posicao);
        else if (m.tipo == TipoMensagem::RoboDesconectado) add(TipoEvento::ROBO_DESCONECTADO, "Robô desconectado: " + m.texto, posicao);
        else if (m.tipo == TipoMensagem::RoboEncerrado) add(TipoEvento::ROBO_ENCERRADO, "Processo do robô finalizado", posicao);
        if (m.tipo == TipoMensagem::EstadoAtualRobo || m.tipo == TipoMensagem::RoboPronto) {
            const auto s = estadoRoboDaMensagem(m);
            if (known && s.sequencia <= old->second.sequencia) return;
            if (known && s.posicao != old->second.posicao) add(TipoEvento::ROBO_EM_MOVIMENTO, "Posição confirmada atualizada", s.posicao);
            if (s.falha && (!known || !old->second.falha)) add(TipoEvento::FALHA_NO_ROBO, "Falha identificada", s.posicao);
            if (!s.falha && known && old->second.falha) add(TipoEvento::ROBO_RECUPERADO, "Falha removida; missão exige retomada explícita", s.posicao);
            if (s.bateria <= 15 && (!known || old->second.bateria > 15)) add(TipoEvento::BATERIA_BAIXA, "Bateria baixa", s.posicao);
            if (s.estado == EstadoRobo::RECARREGANDO && (!known || old->second.estado != EstadoRobo::RECARREGANDO)) add(TipoEvento::ROBO_RECARREGANDO, "Recarga iniciada", s.posicao);
            if (s.estado == EstadoRobo::DISPONIVEL && s.bateria == 100 && known && old->second.estado == EstadoRobo::RECARREGANDO) add(TipoEvento::RECARGA_CONCLUIDA, "Recarga concluída", s.posicao);
            if ((s.estado == EstadoRobo::AGUARDANDO_EQUIPE || s.estado == EstadoRobo::AGUARDANDO_DESCARGA) && (!known || s.estado != old->second.estado)) add(TipoEvento::ROBO_CHEGOU, "Chegada confirmada", s.posicao);
            estados_anteriores_[m.id_robo] = s;
        }
    }
    for (auto& event : eventos) registrar(std::move(event));
}
void GravadorEventos::aoReceberEventoLogistica(const AvisoLogistica& n) {
    TipoEvento tipo; std::string descricao;
    switch (n.tipo) {
    case AvisoLogistica::TipoAviso::MissaoAtribuida: tipo = TipoEvento::MISSAO_ATRIBUIDA; descricao = "Equipe completa reservada"; break;
    case AvisoLogistica::TipoAviso::EquipeReunida: tipo = TipoEvento::EQUIPE_REUNIDA; descricao = "Todos os integrantes chegaram à baía"; break;
    case AvisoLogistica::TipoAviso::EquipeAcoplada: tipo = TipoEvento::CARGA_COLETADA; descricao = "Acoplamento de todos os integrantes confirmado"; break;
    case AvisoLogistica::TipoAviso::TransporteIniciado: tipo = TipoEvento::TRANSPORTE_INICIADO; descricao = "Transporte colaborativo liberado"; break;
    case AvisoLogistica::TipoAviso::CargaEntregue: tipo = TipoEvento::CARGA_ENTREGUE; descricao = "Entrega e desacoplamento de todos os integrantes confirmados"; break;
    case AvisoLogistica::TipoAviso::MissaoCancelada: tipo = TipoEvento::MISSAO_CANCELADA; descricao = "Reserva cancelada: " + n.carga.motivo; break;
    case AvisoLogistica::TipoAviso::MissaoSuspensa: tipo = TipoEvento::MISSAO_SUSPENSA; descricao = "Missão suspensa: " + n.carga.motivo; break;
    case AvisoLogistica::TipoAviso::MissaoRetomada: tipo = TipoEvento::MISSAO_RETOMADA; descricao = "Retomada autorizada após recuperação"; break;
    }
    Posicao posicao; if (!n.missao.relatos.empty()) posicao = n.missao.relatos.begin()->second.posicao;
    EventoRobo e(-1, static_cast<int>(n.missao.id), n.carga.id_baia, posicao, tipo, descricao, "Zona " + std::to_string(n.carga.id_destino));
    e.rastrear(n.carga.id, n.missao.id, 0, "logistica", ++sequencia_, n.ocorrido_em_ms, n.missao.integrantes); registrar(std::move(e));
}
void GravadorEventos::aoReceberEventoPassagem(const AvisoPassagem& n) {
    const auto& m = n.mensagem;
    EventoRobo e(m.id_robo, static_cast<int>(m.id_equipe), m.id_baia, m.destino, n.entrou ? TipoEvento::ENTRADA_EM_TRECHO : TipoEvento::SAIDA_DE_TRECHO,
        std::string(n.entrou ? "Entrada na passagem " : "Saída da passagem ") + std::to_string(n.id_trecho) + " com " + std::to_string(n.vagas) + " vaga(s)", "");
    e.rastrear(m.id_carga, m.id_missao, m.geracao, "passagens", ++sequencia_, instanteUnixMs(), m.integrantes); registrar(std::move(e));
}
