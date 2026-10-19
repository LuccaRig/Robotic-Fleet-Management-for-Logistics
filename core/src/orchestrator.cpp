#include "orchestrator.hpp"
#include "ipc_channel.hpp"
#include <cerrno>
#include <chrono>
#include <csignal>
#include <deque>
#include <exception>
#include <fcntl.h>
#include <iterator>
#include <map>
#include <spawn.h>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

extern char** environ;

struct GestorRobos::Impl {
    struct Entrada {
        ProcessoRobo processo;
        std::unique_ptr<CanalIpc> canal;
        std::deque<Mensagem> saida;
        std::map<std::uint64_t, std::chrono::steady_clock::time_point> comandos_pendentes;
        std::chrono::steady_clock::time_point iniciado = std::chrono::steady_clock::now();
        std::chrono::steady_clock::time_point ultima_mensagem = iniciado;
        std::chrono::steady_clock::time_point encerramento_solicitado;
        int escalonamento = 0;
        std::uint64_t sequencia_enviada = 0;
        std::uint64_t sequencia_recebida = 0;
        bool conectado = true;
    };

    std::string executavel;
    std::map<int, std::unique_ptr<Entrada>> entradas;
    EstadoFrota frota;
    std::uint64_t proxima_geracao = 0;
    bool encerrando = false;
    std::exception_ptr erro;

    explicit Impl(std::string caminho_executavel) : executavel(std::move(caminho_executavel)) {}

    void registrarMensagem(Entrada& entrada, const Mensagem& mensagem, std::vector<Mensagem>& recebidas) {
        if (mensagem.id_robo != entrada.processo.id || mensagem.geracao != entrada.processo.geracao || mensagem.sequencia <= entrada.sequencia_recebida) {
            throw std::runtime_error("Identidade ou sequência IPC inválida");
        }
        entrada.sequencia_recebida = mensagem.sequencia;
        entrada.ultima_mensagem = std::chrono::steady_clock::now();
        if (mensagem.tipo == TipoMensagem::RoboPronto) entrada.processo.pronto = true;
        if (mensagem.tipo == TipoMensagem::ConfirmacaoComando) entrada.comandos_pendentes.erase(mensagem.id_comando);

        if (mensagem.tipo == TipoMensagem::RoboPronto || mensagem.tipo == TipoMensagem::EstadoAtualRobo) {
            if (!frota.atualizar(estadoRoboDaMensagem(mensagem))) return;
        }
        recebidas.push_back(mensagem);
    }

    void desconectar(Entrada& entrada, const std::string& motivo, std::vector<Mensagem>& recebidas) {
        if (!entrada.conectado) return;
        entrada.conectado = false;
        entrada.processo.pronto = false;
        entrada.processo.encerrando = true;
        entrada.encerramento_solicitado = std::chrono::steady_clock::now();
        entrada.canal->interromper();
        entrada.saida.clear();
        entrada.comandos_pendentes.clear();
        frota.desconectar(entrada.processo.id, entrada.processo.geracao);

        Mensagem mensagem;
        mensagem.tipo = TipoMensagem::RoboDesconectado;
        mensagem.id_robo = entrada.processo.id;
        mensagem.geracao = entrada.processo.geracao;
        mensagem.texto = motivo;
        mensagem.ocorrido_em_ms = instanteUnixMs();
        recebidas.push_back(std::move(mensagem));
    }

    void enviarPendentes(Entrada& entrada, std::vector<Mensagem>& recebidas) {
        while (entrada.conectado && !entrada.saida.empty()) {
            auto mensagem = std::move(entrada.saida.front());
            entrada.saida.pop_front();
            try {
                entrada.canal->enviar(mensagem);
            } catch (const std::exception& erro_envio) {
                desconectar(entrada, erro_envio.what(), recebidas);
            }
        }
    }

    void receberDisponiveis(Entrada& entrada, std::vector<Mensagem>& recebidas) {
        try {
            while (entrada.conectado) {
                const auto mensagem = entrada.canal->receber(std::chrono::milliseconds(0));
                if (!mensagem) break;
                registrarMensagem(entrada, *mensagem, recebidas);
            }
        } catch (const std::exception& erro_recepcao) {
            desconectar(entrada, erro_recepcao.what(), recebidas);
        }
    }

    void verificarPrazos(Entrada& entrada, std::vector<Mensagem>& recebidas) {
        const auto agora = std::chrono::steady_clock::now();
        bool expirado = entrada.conectado && !entrada.processo.encerrando &&
            ((!entrada.processo.pronto && agora - entrada.iniciado > std::chrono::seconds(2)) ||
             (entrada.processo.pronto && agora - entrada.ultima_mensagem > std::chrono::seconds(3)));
        for (const auto& comando : entrada.comandos_pendentes) {
            if (agora > comando.second) expirado = true;
        }
        if (expirado) desconectar(entrada, "Prazo de comunicação com o robô excedido", recebidas);

        if (!entrada.processo.encerrando) return;
        const auto decorrido = agora - entrada.encerramento_solicitado;
        if (decorrido > std::chrono::seconds(3) && entrada.escalonamento < 2) {
            kill(entrada.processo.pid, SIGKILL);
            entrada.escalonamento = 2;
        } else if (decorrido > std::chrono::seconds(2) && entrada.escalonamento < 1) {
            kill(entrada.processo.pid, SIGTERM);
            entrada.escalonamento = 1;
        }
    }

    bool recolher(Entrada& entrada, std::vector<Mensagem>& recebidas) {
        int status = 0;
        const auto resultado = waitpid(entrada.processo.pid, &status, WNOHANG);
        if (resultado == 0 || (resultado < 0 && errno != ECHILD)) return false;
        receberDisponiveis(entrada, recebidas);
        desconectar(entrada, "Processo do robô encerrado", recebidas);
        return true;
    }
};

GestorRobos::GestorRobos(std::string executavel) : impl_(std::make_unique<Impl>(std::move(executavel))) {}
GestorRobos::~GestorRobos() { try { parar(); } catch (...) {} }

bool GestorRobos::iniciarProcessoRobo(int id, int x, int y) {
    if (id < 0 || impl_->encerrando || impl_->executavel.empty() || impl_->entradas.count(id)) return false;
    int descritores[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, descritores) != 0) throw std::runtime_error("Não foi possível criar o canal do robô");
    if (fcntl(descritores[0], F_SETFD, FD_CLOEXEC) < 0 || fcntl(descritores[1], F_SETFD, FD_CLOEXEC) < 0) {
        close(descritores[0]);
        close(descritores[1]);
        throw std::runtime_error("Falha ao proteger descritores IPC");
    }

    const auto geracao = ++impl_->proxima_geracao;
    std::vector<std::string> argumentos{impl_->executavel, "--robot-worker", "3", std::to_string(id), std::to_string(geracao), std::to_string(x), std::to_string(y)};
    std::vector<char*> argv;
    for (auto& argumento : argumentos) argv.push_back(argumento.data());
    argv.push_back(nullptr);

    posix_spawn_file_actions_t acoes;
    int erro_acoes = posix_spawn_file_actions_init(&acoes);
    if (erro_acoes != 0) {
        close(descritores[0]);
        close(descritores[1]);
        return false;
    }
    erro_acoes = posix_spawn_file_actions_addclose(&acoes, descritores[0]);
    if (!erro_acoes) erro_acoes = posix_spawn_file_actions_adddup2(&acoes, descritores[1], 3);
    if (!erro_acoes && descritores[1] != 3) erro_acoes = posix_spawn_file_actions_addclose(&acoes, descritores[1]);
    if (erro_acoes) {
        posix_spawn_file_actions_destroy(&acoes);
        close(descritores[0]);
        close(descritores[1]);
        return false;
    }

    pid_t pid = -1;
    const int resultado = posix_spawn(&pid, impl_->executavel.c_str(), &acoes, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&acoes);
    close(descritores[1]);
    if (resultado != 0) {
        close(descritores[0]);
        return false;
    }

    try {
        auto entrada = std::make_unique<Impl::Entrada>();
        entrada->processo = {id, static_cast<int>(pid), geracao, false, false};
        entrada->canal = std::make_unique<CanalIpc>(descritores[0]);
        impl_->frota.registrarRobo(id, geracao, {x, y});
        impl_->entradas.emplace(id, std::move(entrada));
    } catch (...) {
        kill(pid, SIGKILL);
        while (waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {}
        throw;
    }
    return true;
}

bool GestorRobos::encerrarProcessoRobo(int id) {
    const auto it = impl_->entradas.find(id);
    if (it == impl_->entradas.end()) return false;
    auto& entrada = *it->second;
    if (!entrada.processo.encerrando) {
        Mensagem mensagem;
        mensagem.tipo = TipoMensagem::Encerrar;
        enviar(id, std::move(mensagem));
        entrada.processo.encerrando = true;
        entrada.encerramento_solicitado = std::chrono::steady_clock::now();
    }
    return true;
}

bool GestorRobos::enviar(int id, Mensagem mensagem) {
    const auto it = impl_->entradas.find(id);
    if (it == impl_->entradas.end() || !it->second->conectado) return false;
    auto& entrada = *it->second;
    if (entrada.processo.encerrando && mensagem.tipo != TipoMensagem::Encerrar) return false;
    if (!entrada.processo.pronto && mensagem.tipo != TipoMensagem::Encerrar) return false;
    if (entrada.saida.size() >= 1024) throw std::runtime_error("Fila de envio IPC esgotada");

    mensagem.id_robo = id;
    mensagem.geracao = entrada.processo.geracao;
    mensagem.sequencia = ++entrada.sequencia_enviada;
    mensagem.ocorrido_em_ms = instanteUnixMs();
    switch (mensagem.tipo) {
    case TipoMensagem::AtribuirMissao:
    case TipoMensagem::CancelarMissao:
    case TipoMensagem::Acoplar:
    case TipoMensagem::Desacoplar:
    case TipoMensagem::PausarMissao:
    case TipoMensagem::RetomarMissao:
    case TipoMensagem::Encerrar:
        if (!mensagem.id_comando) mensagem.id_comando = mensagem.sequencia;
        entrada.comandos_pendentes[mensagem.id_comando] = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        break;
    default:
        break;
    }
    entrada.saida.push_back(std::move(mensagem));
    return true;
}

std::vector<Mensagem> GestorRobos::processarCiclo() {
    verificarFalha();
    std::vector<Mensagem> recebidas;
    try {
        for (auto it = impl_->entradas.begin(); it != impl_->entradas.end();) {
            auto& entrada = *it->second;
            impl_->enviarPendentes(entrada, recebidas);
            impl_->receberDisponiveis(entrada, recebidas);
            impl_->verificarPrazos(entrada, recebidas);
            if (impl_->recolher(entrada, recebidas)) it = impl_->entradas.erase(it);
            else ++it;
        }
    } catch (...) {
        impl_->erro = std::current_exception();
        throw;
    }
    return recebidas;
}

std::vector<ProcessoRobo> GestorRobos::processos() const {
    std::vector<ProcessoRobo> resultado;
    for (const auto& par : impl_->entradas) resultado.push_back(par.second->processo);
    return resultado;
}

std::size_t GestorRobos::quantidadeRobosAtivos() const { return impl_->entradas.size(); }
bool GestorRobos::existeProcessoRobo(int id) const { return impl_->entradas.count(id) != 0; }
const EstadoFrota& GestorRobos::frota() const { return impl_->frota; }
void GestorRobos::verificarFalha() const { if (impl_->erro) std::rethrow_exception(impl_->erro); }

std::vector<Mensagem> GestorRobos::parar() {
    if (impl_->encerrando && impl_->entradas.empty()) return {};
    impl_->encerrando = true;
    for (const auto& par : impl_->entradas) encerrarProcessoRobo(par.first);
    std::vector<Mensagem> recebidas;
    while (!impl_->entradas.empty()) {
        auto ciclo = processarCiclo();
        recebidas.insert(recebidas.end(), std::make_move_iterator(ciclo.begin()), std::make_move_iterator(ciclo.end()));
        if (!impl_->entradas.empty()) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    verificarFalha();
    return recebidas;
}
