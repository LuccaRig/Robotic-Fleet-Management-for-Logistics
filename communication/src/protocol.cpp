#include "protocol.hpp"
#include <stdexcept>
#include <limits>

namespace {
struct Writer {
    std::vector<std::uint8_t> bytes;
    void number(std::uint64_t value) {
        for (int i = 7; i >= 0; --i) bytes.push_back(static_cast<std::uint8_t>(value >> (i * 8)));
    }
    void point(Pos p) { number(static_cast<std::uint32_t>(p.x)); number(static_cast<std::uint32_t>(p.y)); }
};
struct Reader {
    const std::vector<std::uint8_t>& bytes;
    std::size_t offset = 0;
    std::uint64_t number() {
        if (bytes.size() - offset < 8) throw std::runtime_error("Mensagem IPC incompleta");
        std::uint64_t value = 0;
        for (int i = 0; i < 8; ++i) value = (value << 8) | bytes[offset++];
        return value;
    }
    int integer() { return static_cast<std::int32_t>(number()); }
    bool boolean() {
        const auto v = number();
        if (v > 1) throw std::runtime_error("Valor booleano IPC inválido");
        return v != 0;
    }
    Pos point() { const int x = integer(); const int y = integer(); return {x, y}; }
    std::size_t count(std::size_t unit) {
        const auto n = number();
        if (n > (bytes.size() - offset) / unit) throw std::runtime_error("Comprimento IPC inválido");
        return static_cast<std::size_t>(n);
    }
};
}

std::vector<std::uint8_t> encodeMessage(const Message& m) {
    Writer w;
    w.number(PROTOCOL_VERSION); w.number(static_cast<std::uint32_t>(m.type));
    w.number(static_cast<std::uint32_t>(m.robot_id)); w.number(m.generation); w.number(m.sequence);
    w.number(m.command_id); w.number(m.mission_id); w.number(m.cargo_id); w.number(m.team_id);
    w.number(static_cast<std::uint32_t>(m.bay_id)); w.number(static_cast<unsigned>(m.phase));
    w.number(static_cast<unsigned>(m.state)); w.point(m.position); w.point(m.target);
    w.number(static_cast<std::uint32_t>(m.battery)); w.number(m.failure); w.number(m.coupled); w.number(m.accepted);
    w.number(m.distance); w.number(m.step); w.number(m.sensor_sequence); w.number(m.occurred_at_ms);
    w.number(static_cast<std::uint32_t>(m.passage_id)); w.number(static_cast<std::uint32_t>(m.direction)); w.number(m.team_size);
    w.number(m.route.size()); for (const auto p : m.route) w.point(p);
    w.number(m.members.size()); for (const int id : m.members) w.number(static_cast<std::uint32_t>(id));
    w.number(m.text.size()); w.bytes.insert(w.bytes.end(), m.text.begin(), m.text.end());
    if (w.bytes.size() > MAX_MESSAGE_BYTES) throw std::runtime_error("Mensagem IPC excede 64 KiB");
    return w.bytes;
}

Message decodeMessage(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() > MAX_MESSAGE_BYTES) throw std::runtime_error("Mensagem IPC excede 64 KiB");
    Reader r{bytes};
    if (r.number() != PROTOCOL_VERSION) throw std::runtime_error("Versão IPC incompatível");
    const auto type = r.number();
    if (type < 1 || type > static_cast<unsigned>(MessageType::RobotDisconnected)) throw std::runtime_error("Tipo IPC inválido");
    Message m; m.type = static_cast<MessageType>(type); m.robot_id = r.integer();
    m.generation = r.number(); m.sequence = r.number(); m.command_id = r.number();
    m.mission_id = static_cast<EntityId>(r.number()); m.cargo_id = static_cast<EntityId>(r.number()); m.team_id = static_cast<EntityId>(r.number());
    m.bay_id = r.integer();
    const auto phase = r.number(), state = r.number();
    if (phase > static_cast<unsigned>(MissionPhase::SUSPENDED) || state > static_cast<unsigned>(RobotState::STOPPED)) throw std::runtime_error("Estado IPC inválido");
    m.phase = static_cast<MissionPhase>(phase); m.state = static_cast<RobotState>(state);
    m.position = r.point(); m.target = r.point(); m.battery = r.integer();
    m.failure = r.boolean(); m.coupled = r.boolean(); m.accepted = r.boolean();
    m.distance = r.number(); m.step = r.number(); m.sensor_sequence = r.number(); m.occurred_at_ms = static_cast<std::int64_t>(r.number());
    m.passage_id = r.integer(); m.direction = r.integer(); m.team_size = r.integer();
    if (m.battery < 0 || m.battery > 100 || m.team_size < 1 || m.team_size > 4) throw std::runtime_error("Dados IPC fora dos limites");
    const auto route_count = r.count(16); for (std::size_t i = 0; i < route_count; ++i) m.route.push_back(r.point());
    const auto member_count = r.count(8); for (std::size_t i = 0; i < member_count; ++i) m.members.push_back(r.integer());
    const auto length = r.count(1); m.text.assign(bytes.begin() + r.offset, bytes.begin() + r.offset + length); r.offset += length;
    if (r.offset != bytes.size()) throw std::runtime_error("Dados extras na mensagem IPC");
    return m;
}

RobotSnapshot snapshotFromMessage(const Message& m) {
    RobotSnapshot s;
    s.id = m.robot_id; s.generation = m.generation; s.sequence = m.sequence;
    s.position = m.position; s.target = m.target; s.state = m.state; s.phase = m.phase;
    s.battery = m.battery; s.failure = m.failure; s.coupled = m.coupled;
    s.connected = true; s.available = m.state == RobotState::IDLE && !m.failure && !m.coupled && m.battery > 15;
    s.mission_id = m.mission_id; s.cargo_id = m.cargo_id; s.team_id = m.team_id; s.bay_id = m.bay_id;
    s.step = m.step; s.sensor_sequence = m.sensor_sequence; s.distance = m.distance; s.updated_at_ms = m.occurred_at_ms;
    return s;
}

Message messageFromSnapshot(const RobotSnapshot& s) {
    Message m; m.type = MessageType::RobotSnapshot; m.robot_id = s.id; m.generation = s.generation;
    m.sequence = s.sequence; m.position = s.position; m.target = s.target; m.state = s.state; m.phase = s.phase;
    m.battery = s.battery; m.failure = s.failure; m.coupled = s.coupled; m.distance = s.distance;
    m.mission_id = s.mission_id; m.cargo_id = s.cargo_id; m.team_id = s.team_id; m.bay_id = s.bay_id;
    m.step = s.step; m.sensor_sequence = s.sensor_sequence; m.occurred_at_ms = s.updated_at_ms;
    return m;
}
