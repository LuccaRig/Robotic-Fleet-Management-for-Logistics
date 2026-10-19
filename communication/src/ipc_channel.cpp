#include "ipc_channel.hpp"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>

namespace {
int remaining(std::chrono::steady_clock::time_point prazo) {
    return static_cast<int>(std::max<std::int64_t>(0, std::chrono::duration_cast<std::chrono::milliseconds>(prazo - std::chrono::steady_clock::now()).count()));
}
void fail(const char* operation) { throw std::runtime_error(std::string(operation) + ": " + std::strerror(errno)); }
}

CanalIpc::CanalIpc(int fd) : fd_(fd) {
    const int flags = fcntl(fd_, F_GETFL, 0);
    if (flags < 0 || fcntl(fd_, F_SETFL, flags | O_NONBLOCK) < 0) { close(fd_); fd_ = -1; fail("Falha ao configurar IPC"); }
#ifdef SO_NOSIGPIPE
    int enabled = 1;
    setsockopt(fd_, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled));
#endif
}
CanalIpc::~CanalIpc() { if (fd_ >= 0) close(fd_); }
void CanalIpc::interromper() noexcept { if (fd_ >= 0) ::shutdown(fd_, SHUT_RDWR); }

void CanalIpc::enviar(const Mensagem& mensagem, std::chrono::milliseconds timeout) {
    auto payload = codificarMensagem(mensagem);
    const auto tamanho = static_cast<std::uint32_t>(payload.size());
    // O prefixo permite reconstruir mensagens mesmo quando o socket entrega blocos parciais.
    std::vector<std::uint8_t> frame{static_cast<std::uint8_t>(tamanho >> 24), static_cast<std::uint8_t>(tamanho >> 16), static_cast<std::uint8_t>(tamanho >> 8), static_cast<std::uint8_t>(tamanho)};
    frame.insert(frame.end(), payload.begin(), payload.end());
    const auto prazo = std::chrono::steady_clock::now() + timeout;
    std::unique_lock<std::timed_mutex> lock(send_mutex_, std::defer_lock);
    if (!lock.try_lock_until(prazo)) throw std::runtime_error("Prazo de serialização do envio IPC excedido");
    std::size_t deslocamento = 0;
    while (deslocamento < frame.size()) {
        if (std::chrono::steady_clock::now() >= prazo) throw std::runtime_error("Prazo de envio IPC excedido");
        pollfd p{fd_, POLLOUT, 0};
        const int pronto = poll(&p, 1, remaining(prazo));
        if (pronto < 0 && errno == EINTR) continue;
        if (pronto < 0) fail("Falha ao aguardar envio IPC");
        if (pronto == 0) continue;
        int flags = 0;
#ifdef MSG_NOSIGNAL
        flags = MSG_NOSIGNAL;
#endif
        const auto n = ::send(fd_, frame.data() + deslocamento, frame.size() - deslocamento, flags);
        if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        if (n <= 0) fail("Canal IPC indisponível para envio");
        deslocamento += static_cast<std::size_t>(n);
    }
}

std::optional<Mensagem> CanalIpc::receber(std::chrono::milliseconds timeout) {
    const auto prazo = std::chrono::steady_clock::now() + timeout;
    while (true) {
        if (incoming_.size() >= 4) {
            const std::uint32_t tamanho = (std::uint32_t(incoming_[0]) << 24) | (std::uint32_t(incoming_[1]) << 16) | (std::uint32_t(incoming_[2]) << 8) | incoming_[3];
            if (tamanho == 0 || tamanho > TAMANHO_MAXIMO_MENSAGEM) throw std::runtime_error("Enquadramento IPC inválido");
            if (incoming_.size() >= tamanho + 4) {
                std::vector<std::uint8_t> payload(incoming_.begin() + 4, incoming_.begin() + 4 + tamanho);
                incoming_.erase(incoming_.begin(), incoming_.begin() + 4 + tamanho);
                return decodificarMensagem(payload);
            }
        }
        pollfd p{fd_, POLLIN, 0};
        const int pronto = poll(&p, 1, remaining(prazo));
        if (pronto < 0 && errno == EINTR) continue;
        if (pronto < 0) fail("Falha ao aguardar recepção IPC");
        if (pronto == 0) return std::nullopt;
        std::uint8_t bytes[4096];
        const auto n = recv(fd_, bytes, sizeof(bytes), 0);
        if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        if (n < 0) fail("Falha ao receber IPC");
        if (n == 0) throw std::runtime_error("Canal IPC encerrado");
        incoming_.insert(incoming_.end(), bytes, bytes + n);
    }
}
