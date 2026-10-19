#include "protocol.hpp"
#include <stdexcept>
#include <limits>

namespace {
struct Writer {
    std::vector<std::uint8_t> bytes;
    void number(std::uint64_t value) {
        for (int i = 7; i >= 0; --i) bytes.push_back(static_cast<std::uint8_t>(value >> (i * 8)));
    }
    void point(Posicao p) { number(static_cast<std::uint32_t>(p.x)); number(static_cast<std::uint32_t>(p.y)); }
};
struct Reader {
    const std::vector<std::uint8_t>& bytes;
    std::size_t deslocamento = 0;
    std::uint64_t number() {
        if (bytes.size() - deslocamento < 8) throw std::runtime_error("Mensagem IPC incompleta");
        std::uint64_t value = 0;
        for (int i = 0; i < 8; ++i) value = (value << 8) | bytes[deslocamento++];
        return value;
    }
    int integer() {
        const auto value = number();
        if (value > std::numeric_limits<std::uint32_t>::max()) throw std::runtime_error("Inteiro IPC fora dos limites");
        if (value <= static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max())) return static_cast<int>(value);
        return -1 - static_cast<int>(std::numeric_limits<std::uint32_t>::max() - value);
    }
    bool boolean() {
        const auto v = number();
        if (v > 1) throw std::runtime_error("Valor booleano IPC inválido");
        return v != 0;
    }
    Posicao point() { const int x = integer(); const int y = integer(); return {x, y}; }
    std::size_t count(std::size_t unit) {
        const auto n = number();
        if (n > (bytes.size() - deslocamento) / unit) throw std::runtime_error("Comprimento IPC inválido");
        return static_cast<std::size_t>(n);
    }
};
}

std::vector<std::uint8_t> codificarMensagem(const Mensagem& m) {
    Writer w;
    w.number(VERSAO_PROTOCOLO); w.number(static_cast<std::uint32_t>(m.tipo));
    w.number(static_cast<std::uint32_t>(m.id_robo)); w.number(m.geracao); w.number(m.sequencia);
    w.number(m.id_comando); w.number(m.revisao); w.number(m.id_missao); w.number(m.id_carga); w.number(m.id_equipe);
    w.number(static_cast<std::uint32_t>(m.id_baia)); w.number(static_cast<unsigned>(m.fase));
    w.number(static_cast<unsigned>(m.estado)); w.point(m.posicao); w.point(m.destino);
    w.number(static_cast<std::uint32_t>(m.bateria)); w.number(m.falha); w.number(m.acoplado); w.number(m.aceito);
    w.number(m.distancia); w.number(m.passo); w.number(m.sequencia_sensor); w.number(m.ocorrido_em_ms);
    w.number(static_cast<std::uint32_t>(m.id_trecho)); w.number(static_cast<std::uint32_t>(m.sentido)); w.number(m.quantidade_integrantes);
    w.number(m.rota.size()); for (const auto p : m.rota) w.point(p);
    w.number(m.integrantes.size()); for (const int id : m.integrantes) w.number(static_cast<std::uint32_t>(id));
    w.number(m.texto.size()); w.bytes.insert(w.bytes.end(), m.texto.begin(), m.texto.end());
    if (w.bytes.size() > TAMANHO_MAXIMO_MENSAGEM) throw std::runtime_error("Mensagem IPC excede 64 KiB");
    return w.bytes;
}

Mensagem decodificarMensagem(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() > TAMANHO_MAXIMO_MENSAGEM) throw std::runtime_error("Mensagem IPC excede 64 KiB");
    Reader r{bytes};
    if (r.number() != VERSAO_PROTOCOLO) throw std::runtime_error("Versão IPC incompatível");
    const auto tipo = r.number();
    if (tipo < 1 || tipo > static_cast<unsigned>(TipoMensagem::RoboDesconectado)) throw std::runtime_error("Tipo IPC inválido");
    Mensagem m; m.tipo = static_cast<TipoMensagem>(tipo); m.id_robo = r.integer();
    m.geracao = r.number(); m.sequencia = r.number(); m.id_comando = r.number(); m.revisao = r.number();
    m.id_missao = static_cast<IdEntidade>(r.number()); m.id_carga = static_cast<IdEntidade>(r.number()); m.id_equipe = static_cast<IdEntidade>(r.number());
    m.id_baia = r.integer();
    const auto fase = r.number(), estado = r.number();
    if (fase > static_cast<unsigned>(FaseMissao::SUSPENSO) || estado > static_cast<unsigned>(EstadoRobo::ENCERRADO)) throw std::runtime_error("Estado IPC inválido");
    m.fase = static_cast<FaseMissao>(fase); m.estado = static_cast<EstadoRobo>(estado);
    m.posicao = r.point(); m.destino = r.point(); m.bateria = r.integer();
    m.falha = r.boolean(); m.acoplado = r.boolean(); m.aceito = r.boolean();
    m.distancia = r.number(); m.passo = r.number(); m.sequencia_sensor = r.number(); m.ocorrido_em_ms = static_cast<std::int64_t>(r.number());
    m.id_trecho = r.integer(); m.sentido = r.integer(); m.quantidade_integrantes = r.integer();
    if (m.bateria < 0 || m.bateria > 100 || m.quantidade_integrantes < 1 || m.quantidade_integrantes > 4) throw std::runtime_error("Dados IPC fora dos limites");
    const auto route_count = r.count(16); for (std::size_t i = 0; i < route_count; ++i) m.rota.push_back(r.point());
    const auto member_count = r.count(8); for (std::size_t i = 0; i < member_count; ++i) m.integrantes.push_back(r.integer());
    const auto length = r.count(1); m.texto.assign(bytes.begin() + r.deslocamento, bytes.begin() + r.deslocamento + length); r.deslocamento += length;
    if (r.deslocamento != bytes.size()) throw std::runtime_error("Dados extras na mensagem IPC");
    return m;
}

EstadoAtualRobo estadoRoboDaMensagem(const Mensagem& m) {
    EstadoAtualRobo s;
    s.id = m.id_robo; s.geracao = m.geracao; s.sequencia = m.sequencia;
    s.posicao = m.posicao; s.destino = m.destino; s.estado = m.estado; s.fase = m.fase;
    s.bateria = m.bateria; s.falha = m.falha; s.acoplado = m.acoplado;
    s.conectado = true; s.disponivel = m.estado == EstadoRobo::DISPONIVEL && !m.falha && !m.acoplado && m.bateria > 15 && m.id_missao == 0;
    s.id_missao = m.id_missao; s.id_carga = m.id_carga; s.id_equipe = m.id_equipe; s.id_baia = m.id_baia;
    s.revisao = m.revisao; s.passo = m.passo; s.sequencia_sensor = m.sequencia_sensor; s.distancia = m.distancia; s.atualizado_em_ms = m.ocorrido_em_ms;
    return s;
}

Mensagem mensagemDoEstadoRobo(const EstadoAtualRobo& s) {
    Mensagem m; m.tipo = TipoMensagem::EstadoAtualRobo; m.id_robo = s.id; m.geracao = s.geracao;
    m.sequencia = s.sequencia; m.posicao = s.posicao; m.destino = s.destino; m.estado = s.estado; m.fase = s.fase;
    m.bateria = s.bateria; m.falha = s.falha; m.acoplado = s.acoplado; m.distancia = s.distancia;
    m.id_missao = s.id_missao; m.id_carga = s.id_carga; m.id_equipe = s.id_equipe; m.id_baia = s.id_baia;
    m.revisao = s.revisao; m.passo = s.passo; m.sequencia_sensor = s.sequencia_sensor; m.ocorrido_em_ms = s.atualizado_em_ms;
    return m;
}
