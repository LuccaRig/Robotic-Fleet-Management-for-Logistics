#include "event_recorder.hpp"

EventRecorder::EventRecorder(Buffer& buffer, Observer observer) : buffer_(buffer), observer_(std::move(observer)) {}
void EventRecorder::record(RobotEvent event) { buffer_.addEvent(event); if (observer_) observer_(event); }
void EventRecorder::onRobot(const Message& m) {
    std::vector<RobotEvent> events;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto add = [&](EventType type, const std::string& description, Pos position) {
            RobotEvent e(m.robot_id, static_cast<int>(m.team_id), m.bay_id, position, type, description, "");
            e.trace(m.cargo_id, m.mission_id, m.generation, "robo:" + std::to_string(m.robot_id), m.sequence ? m.sequence : ++sequence_, m.occurred_at_ms ? m.occurred_at_ms : epochMilliseconds());
            events.push_back(std::move(e));
        };
        const auto old = previous_.find(m.robot_id);
        const bool known = old != previous_.end() && old->second.generation == m.generation;
        const auto position = known ? old->second.position : m.position;
        if (m.type == MessageType::RobotHello) add(EventType::ROBOT_CREATED, "Processo do robô criado", m.position);
        else if (m.type == MessageType::RobotReady) add(EventType::ROBOT_READY, "Robô pronto para receber tarefas", m.position);
        else if (m.type == MessageType::RobotDisconnected) add(EventType::ROBOT_DISCONNECTED, "Robô desconectado: " + m.text, position);
        else if (m.type == MessageType::RobotStopped) add(EventType::ROBOT_STOPPED, "Processo do robô finalizado", position);
        if (m.type == MessageType::RobotSnapshot || m.type == MessageType::RobotReady) {
            const auto s = snapshotFromMessage(m);
            if (known && s.sequence <= old->second.sequence) return;
            if (known && s.position != old->second.position) add(EventType::ROBOT_MOVING, "Posição confirmada atualizada", s.position);
            if (s.failure && (!known || !old->second.failure)) add(EventType::ROBOT_FAILURE, "Falha identificada", s.position);
            if (!s.failure && known && old->second.failure) add(EventType::ROBOT_RECOVERED, "Falha removida; missão exige retomada explícita", s.position);
            if (s.battery <= 15 && (!known || old->second.battery > 15)) add(EventType::LOW_BATTERY, "Bateria baixa", s.position);
            if (s.state == RobotState::CHARGING && (!known || old->second.state != RobotState::CHARGING)) add(EventType::ROBOT_CHARGING, "Recarga iniciada", s.position);
            if (s.state == RobotState::IDLE && s.battery == 100 && known && old->second.state == RobotState::CHARGING) add(EventType::RECHARGE_COMPLETED, "Recarga concluída", s.position);
            if ((s.state == RobotState::WAITING_TO_LOAD || s.state == RobotState::WAITING_TO_UNLOAD) && (!known || s.state != old->second.state)) add(EventType::ROBOT_ARRIVED, "Chegada confirmada", s.position);
            previous_[m.robot_id] = s;
        }
    }
    for (auto& event : events) record(std::move(event));
}
void EventRecorder::onLogistics(const LogisticsNotice& n) {
    EventType type; std::string description;
    switch (n.kind) {
    case LogisticsNotice::Kind::Assigned: type = EventType::MISSION_ASSIGNED; description = "Equipe completa reservada"; break;
    case LogisticsNotice::Kind::Gathered: type = EventType::TEAM_GATHERED; description = "Todos os integrantes chegaram à baía"; break;
    case LogisticsNotice::Kind::Coupled: type = EventType::CARGO_PICKUP; description = "Acoplamento de todos os integrantes confirmado"; break;
    case LogisticsNotice::Kind::TransportStarted: type = EventType::TRANSPORT_STARTED; description = "Transporte colaborativo liberado"; break;
    case LogisticsNotice::Kind::Delivered: type = EventType::CARGO_DELIVERED; description = "Entrega e desacoplamento de todos os integrantes confirmados"; break;
    case LogisticsNotice::Kind::Cancelled: type = EventType::MISSION_CANCELLED; description = "Reserva cancelada: " + n.cargo.reason; break;
    case LogisticsNotice::Kind::Suspended: type = EventType::MISSION_SUSPENDED; description = "Missão suspensa: " + n.cargo.reason; break;
    case LogisticsNotice::Kind::Resumed: type = EventType::MISSION_RESUMED; description = "Retomada autorizada após recuperação"; break;
    }
    Pos position; if (!n.mission.reports.empty()) position = n.mission.reports.begin()->second.position;
    RobotEvent e(-1, static_cast<int>(n.mission.id), n.cargo.bay_id, position, type, description, "Zona " + std::to_string(n.cargo.destination_id));
    e.trace(n.cargo.id, n.mission.id, 0, "logistica", ++sequence_, n.occurred_at_ms, n.mission.members); record(std::move(e));
}
void EventRecorder::onPassage(const PassageNotice& n) {
    const auto& m = n.context;
    RobotEvent e(m.robot_id, static_cast<int>(m.team_id), m.bay_id, m.target, n.entered ? EventType::PASSAGE_ENTERED : EventType::PASSAGE_EXITED,
        std::string(n.entered ? "Entrada na passagem " : "Saída da passagem ") + std::to_string(n.passage_id) + " com " + std::to_string(n.slots) + " vaga(s)", "");
    e.trace(m.cargo_id, m.mission_id, m.generation, "passagens", ++sequence_, epochMilliseconds(), m.members); record(std::move(e));
}
