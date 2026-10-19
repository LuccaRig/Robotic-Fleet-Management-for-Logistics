#include "orchestrator.hpp"
#include "ipc_channel.hpp"
#include <cerrno>
#include <condition_variable>
#include <csignal>
#include <deque>
#include <exception>
#include <fcntl.h>
#include <iostream>
#include <map>
#include <mutex>
#include <spawn.h>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

extern char** environ;

struct GestorRobos::Impl {
    struct Entry {
        ProcessoRobo process;
        std::shared_ptr<CanalIpc> channel;
        std::deque<Mensagem> outgoing;
        std::map<std::uint64_t, std::chrono::steady_clock::time_point> pendente;
        std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
        std::chrono::steady_clock::time_point seen = started;
        std::chrono::steady_clock::time_point stopping_at;
        int escalation = 0;
        std::uint64_t sent = 0;
        std::uint64_t received = 0;
        bool conectado = true;
    };
    std::string executavel;
    mutable std::mutex mutex;
    std::condition_variable cv;
    std::map<int, std::shared_ptr<Entry>> entries;
    EstadoFrota frota;
    Handler tratador;
    std::function<bool(Posicao)> validador;
    std::thread gateway;
    std::mutex lifecycle;
    std::uint64_t geracao = 0;
    bool encerrando = false;
    std::exception_ptr error;

    explicit Impl(std::string path) : executavel(std::move(path)) { gateway = std::thread(&Impl::loop, this); }
    void notify(const Mensagem& mensagem) {
        if (mensagem.tipo == TipoMensagem::RoboPronto || mensagem.tipo == TipoMensagem::EstadoAtualRobo) { if (!frota.atualizar(estadoRoboDaMensagem(mensagem))) return; }
        if (mensagem.tipo == TipoMensagem::RoboDesconectado) frota.desconectar(mensagem.id_robo, mensagem.geracao);
        Handler callback;
        { std::lock_guard<std::mutex> lock(mutex); callback = tratador; }
        if (callback) callback(mensagem);
    }
    void desconectar(const std::shared_ptr<Entry>& entry, const std::string& motivo) {
        Mensagem mensagem;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!entry->conectado) return;
            entry->conectado = false;
            entry->process.pronto = false;
            entry->process.encerrando = true;
            entry->stopping_at = std::chrono::steady_clock::now();
            entry->channel->interromper();
            entry->outgoing.clear(); entry->pendente.clear();
            mensagem.tipo = TipoMensagem::RoboDesconectado; mensagem.id_robo = entry->process.id;
            mensagem.geracao = entry->process.geracao; mensagem.texto = motivo; mensagem.ocorrido_em_ms = instanteUnixMs();
        }
        notify(mensagem);
    }
    void consume(const std::shared_ptr<Entry>& entry, const Mensagem& mensagem) {
        if (mensagem.id_robo != entry->process.id || mensagem.geracao != entry->process.geracao || mensagem.sequencia <= entry->received)
            throw std::runtime_error("Identidade ou sequência IPC inválida");
        entry->received = mensagem.sequencia;
        {
            std::lock_guard<std::mutex> lock(mutex);
            entry->seen = std::chrono::steady_clock::now();
            if (mensagem.tipo == TipoMensagem::RoboPronto) entry->process.pronto = true;
            if (mensagem.tipo == TipoMensagem::ConfirmacaoComando) entry->pendente.erase(mensagem.id_comando);
        }
        notify(mensagem);
    }
    void loop() noexcept {
        try {
            while (true) {
                std::vector<std::shared_ptr<Entry>> atual;
                {
                    std::unique_lock<std::mutex> lock(mutex);
                    if (encerrando && entries.empty()) break;
                    for (const auto& pair : entries) atual.push_back(pair.second);
                }
                for (const auto& entry : atual) {
                    std::deque<Mensagem> outgoing;
                    { std::lock_guard<std::mutex> lock(mutex); outgoing.swap(entry->outgoing); }
                    std::string send_error;
                    try { for (const auto& mensagem : outgoing) entry->channel->enviar(mensagem); }
                    catch (const std::exception& e) { send_error = e.what(); }
                    try {
                        while (entry->conectado) {
                            const auto received = entry->channel->receber(std::chrono::milliseconds(0));
                            if (!received) break;
                            consume(entry, *received);
                        }
                    } catch (const std::exception& e) { desconectar(entry, e.what()); }
                    if (!send_error.empty()) desconectar(entry, send_error);
                    bool expired = false;
                    {
                        std::lock_guard<std::mutex> lock(mutex);
                        const auto now = std::chrono::steady_clock::now();
                        expired = entry->conectado && !entry->process.encerrando &&
                            ((!entry->process.pronto && now - entry->started > std::chrono::seconds(2)) ||
                             (entry->process.pronto && now - entry->seen > std::chrono::seconds(3)));
                        for (const auto& pendente : entry->pendente) if (now > pendente.second) expired = true;
                        if (entry->process.encerrando) {
                            const auto elapsed = now - entry->stopping_at;
                            if (elapsed > std::chrono::seconds(3) && entry->escalation < 2) { kill(entry->process.pid, SIGKILL); entry->escalation = 2; }
                            else if (elapsed > std::chrono::seconds(2) && entry->escalation < 1) { kill(entry->process.pid, SIGTERM); entry->escalation = 1; }
                        }
                    }
                    if (expired) desconectar(entry, "Prazo de comunicação com o robô excedido");
                    int status = 0;
                    const auto pid = waitpid(entry->process.pid, &status, WNOHANG);
                    if (pid > 0 || (pid < 0 && errno == ECHILD)) {
                        if (entry->conectado) {
                            try {
                                while (const auto mensagem = entry->channel->receber(std::chrono::milliseconds(0))) consume(entry, *mensagem);
                            } catch (const std::exception& e) { desconectar(entry, e.what()); }
                        }
                        desconectar(entry, "Processo do robô encerrado");
                        std::lock_guard<std::mutex> lock(mutex);
                        entries.erase(entry->process.id); cv.notify_all();
                    }
                }
                std::unique_lock<std::mutex> lock(mutex);
                cv.wait_for(lock, std::chrono::milliseconds(10));
            }
        } catch (...) {
            std::lock_guard<std::mutex> lock(mutex); error = std::current_exception();
            for (auto& pair : entries) { pair.second->channel->interromper(); kill(pair.second->process.pid, SIGKILL); }
        }
    }
};

GestorRobos::GestorRobos(std::string executavel) : impl_(std::make_unique<Impl>(std::move(executavel))) {}
GestorRobos::~GestorRobos() { try { parar(); } catch (...) {} }
void GestorRobos::definirTratadorMensagem(Handler tratador) { std::lock_guard<std::mutex> lock(impl_->mutex); impl_->tratador = std::move(tratador); }
void GestorRobos::definirValidadorPosicao(std::function<bool(Posicao)> validador) { std::lock_guard<std::mutex> lock(impl_->mutex); impl_->validador = std::move(validador); }

bool GestorRobos::iniciarProcessoRobo(int id, int x, int y) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (id < 0 || impl_->encerrando || impl_->executavel.empty() || impl_->entries.count(id) || (impl_->validador && !impl_->validador({x, y}))) return false;
    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) throw std::runtime_error("Não foi possível criar o canal do robô");
    if (fcntl(fds[0], F_SETFD, FD_CLOEXEC) < 0 || fcntl(fds[1], F_SETFD, FD_CLOEXEC) < 0) { close(fds[0]); close(fds[1]); throw std::runtime_error("Falha ao proteger descritores IPC"); }
    const auto geracao = ++impl_->geracao;
    std::vector<std::string> args{impl_->executavel, "--robot-worker", "3", std::to_string(id), std::to_string(geracao), std::to_string(x), std::to_string(y)};
    std::vector<char*> argv; for (auto& arg : args) argv.push_back(arg.data()); argv.push_back(nullptr);
    posix_spawn_file_actions_t acoes;
    int action_error = posix_spawn_file_actions_init(&acoes);
    if (action_error != 0) { close(fds[0]); close(fds[1]); return false; }
    action_error = posix_spawn_file_actions_addclose(&acoes, fds[0]);
    if (!action_error) action_error = posix_spawn_file_actions_adddup2(&acoes, fds[1], 3);
    if (!action_error && fds[1] != 3) action_error = posix_spawn_file_actions_addclose(&acoes, fds[1]);
    if (action_error) { posix_spawn_file_actions_destroy(&acoes); close(fds[0]); close(fds[1]); return false; }
    pid_t pid = -1;
    const int result = posix_spawn(&pid, impl_->executavel.c_str(), &acoes, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&acoes); close(fds[1]);
    if (result != 0) { close(fds[0]); return false; }
    try {
        auto channel = std::make_shared<CanalIpc>(fds[0]);
        auto entry = std::make_shared<Impl::Entry>();
        entry->process = {id, static_cast<int>(pid), geracao, false, false}; entry->channel = std::move(channel);
        impl_->frota.registrarRobo(id, geracao, {x, y}); impl_->entries.emplace(id, std::move(entry));
    } catch (...) { kill(pid, SIGKILL); while (waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {} throw; }
    impl_->cv.notify_all(); return true;
}

bool GestorRobos::enviar(int id, Mensagem mensagem) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    const auto it = impl_->entries.find(id);
    if (it == impl_->entries.end() || !it->second->conectado) return false;
    auto& entry = *it->second;
    if (entry.process.encerrando && mensagem.tipo != TipoMensagem::Encerrar) return false;
    if (!entry.process.pronto && mensagem.tipo != TipoMensagem::Encerrar) return false;
    if (entry.outgoing.size() >= 1024) throw std::runtime_error("Fila de envio IPC esgotada");
    mensagem.id_robo = id; mensagem.geracao = entry.process.geracao; mensagem.sequencia = ++entry.sent;
    mensagem.ocorrido_em_ms = instanteUnixMs();
    switch (mensagem.tipo) {
    case TipoMensagem::AtribuirMissao: case TipoMensagem::CancelarMissao: case TipoMensagem::Acoplar:
    case TipoMensagem::Desacoplar: case TipoMensagem::PausarMissao: case TipoMensagem::RetomarMissao: case TipoMensagem::Encerrar:
        if (!mensagem.id_comando) mensagem.id_comando = mensagem.sequencia;
        entry.pendente[mensagem.id_comando] = std::chrono::steady_clock::now() + std::chrono::seconds(3); break;
    default: break;
    }
    entry.outgoing.push_back(std::move(mensagem)); impl_->cv.notify_all(); return true;
}

bool GestorRobos::encerrarProcessoRobo(int id) {
    Mensagem mensagem; mensagem.tipo = TipoMensagem::Encerrar;
    const bool sent = enviar(id, mensagem);
    std::lock_guard<std::mutex> lock(impl_->mutex);
    const auto it = impl_->entries.find(id);
    if (it == impl_->entries.end()) return false;
    if (!it->second->process.encerrando) { it->second->process.encerrando = true; it->second->stopping_at = std::chrono::steady_clock::now(); }
    impl_->cv.notify_all(); return sent;
}
std::vector<ProcessoRobo> GestorRobos::processos() const {
    std::lock_guard<std::mutex> lock(impl_->mutex); std::vector<ProcessoRobo> result;
    for (const auto& pair : impl_->entries) result.push_back(pair.second->process);
    return result;
}
std::size_t GestorRobos::quantidadeRobosAtivos() const { return processos().size(); }
bool GestorRobos::existeProcessoRobo(int id) const { for (const auto& p : processos()) if (p.id == id) return true; return false; }
void GestorRobos::imprimirEstadosRobos() const { for (const auto& p : processos()) std::cout << "Robô " << p.id << " | PID " << p.pid << " | " << (p.pronto ? "Pronto" : "Inicializando") << '\n'; }
const EstadoFrota& GestorRobos::frota() const { return impl_->frota; }
void GestorRobos::verificarFalha() const { std::lock_guard<std::mutex> lock(impl_->mutex); if (impl_->error) std::rethrow_exception(impl_->error); }
void GestorRobos::parar() {
    std::lock_guard<std::mutex> lifecycle(impl_->lifecycle);
    { std::lock_guard<std::mutex> lock(impl_->mutex); impl_->encerrando = true; }
    for (const auto& p : processos()) encerrarProcessoRobo(p.id);
    impl_->cv.notify_all();
    if (impl_->gateway.joinable()) impl_->gateway.join();
    for (const auto& p : processos()) { kill(p.pid, SIGKILL); while (waitpid(p.pid, nullptr, 0) < 0 && errno == EINTR) {} }
    { std::lock_guard<std::mutex> lock(impl_->mutex); impl_->entries.clear(); }
    verificarFalha();
}
