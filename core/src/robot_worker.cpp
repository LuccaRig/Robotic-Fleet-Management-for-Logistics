#include "orchestrator.hpp"
#include "ipc_channel.hpp"
#include "robot_agent.hpp"
#include <iostream>
#include <mutex>

int executarAgenteRobo(int fd, int id, std::uint64_t geracao, Posicao inicio) {
    try {
        CanalIpc channel(fd); std::mutex send_mutex; std::uint64_t sequencia = 0; AgenteRobo* active = nullptr;
        auto enviar = [&](Mensagem m) {
            std::lock_guard<std::mutex> lock(send_mutex);
            if (active && m.tipo == TipoMensagem::EstadoAtualRobo) m = mensagemDoEstadoRobo(active->capturarEstado());
            m.id_robo = id; m.geracao = geracao; m.sequencia = ++sequencia; m.ocorrido_em_ms = instanteUnixMs(); channel.enviar(m);
        };
        Mensagem hello; hello.tipo = TipoMensagem::SaudacaoRobo; hello.posicao = inicio; enviar(hello);
        AgenteRobo robo(id, inicio.x, inicio.y, geracao, enviar); active = &robo;
        Mensagem pronto = mensagemDoEstadoRobo(robo.capturarEstado()); pronto.tipo = TipoMensagem::RoboPronto; enviar(pronto);
        robo.iniciar();
        while (true) {
            robo.verificarFalha();
            const auto m = channel.receber(std::chrono::milliseconds(100));
            if (!m) continue;
            if (m->id_robo != id || m->geracao != geracao) throw std::runtime_error("Identidade do comando inválida");
            robo.processarMensagem(*m);
            if (m->tipo == TipoMensagem::Encerrar) break;
        }
        robo.parar(); robo.atualizarStatus();
        Mensagem stopped = mensagemDoEstadoRobo(robo.capturarEstado()); stopped.tipo = TipoMensagem::RoboEncerrado; stopped.estado = EstadoRobo::ENCERRADO; enviar(stopped); return 0;
    } catch (const std::exception& e) { std::cerr << "Processo do robô: " << e.what() << '\n'; return 1; }
}
