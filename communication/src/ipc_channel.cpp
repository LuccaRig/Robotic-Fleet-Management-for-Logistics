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
int remaining(std::chrono::steady_clock::time_point deadline) {
    return static_cast<int>(std::max<std::int64_t>(0, std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count()));
}
void fail(const char* operation) { throw std::runtime_error(std::string(operation) + ": " + std::strerror(errno)); }
}

IpcChannel::IpcChannel(int fd) : fd_(fd) {
    const int flags = fcntl(fd_, F_GETFL, 0);
    if (flags < 0 || fcntl(fd_, F_SETFL, flags | O_NONBLOCK) < 0) { close(fd_); fd_ = -1; fail("Falha ao configurar IPC"); }
#ifdef SO_NOSIGPIPE
    int enabled = 1;
    setsockopt(fd_, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled));
#endif
}
IpcChannel::~IpcChannel() { if (fd_ >= 0) close(fd_); }
void IpcChannel::interrupt() noexcept { if (fd_ >= 0) ::shutdown(fd_, SHUT_RDWR); }

void IpcChannel::send(const Message& message, std::chrono::milliseconds timeout) {
    auto payload = encodeMessage(message);
    const auto size = static_cast<std::uint32_t>(payload.size());
    std::vector<std::uint8_t> frame{static_cast<std::uint8_t>(size >> 24), static_cast<std::uint8_t>(size >> 16), static_cast<std::uint8_t>(size >> 8), static_cast<std::uint8_t>(size)};
    frame.insert(frame.end(), payload.begin(), payload.end());
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::unique_lock<std::timed_mutex> lock(send_mutex_, std::defer_lock);
    if (!lock.try_lock_until(deadline)) throw std::runtime_error("Prazo de serialização do envio IPC excedido");
    std::size_t offset = 0;
    while (offset < frame.size()) {
        if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error("Prazo de envio IPC excedido");
        pollfd p{fd_, POLLOUT, 0};
        const int ready = poll(&p, 1, remaining(deadline));
        if (ready < 0 && errno == EINTR) continue;
        if (ready < 0) fail("Falha ao aguardar envio IPC");
        if (ready == 0) continue;
        int flags = 0;
#ifdef MSG_NOSIGNAL
        flags = MSG_NOSIGNAL;
#endif
        const auto n = ::send(fd_, frame.data() + offset, frame.size() - offset, flags);
        if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        if (n <= 0) fail("Canal IPC indisponível para envio");
        offset += static_cast<std::size_t>(n);
    }
}

std::optional<Message> IpcChannel::receive(std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    do {
        if (incoming_.size() >= 4) {
            const std::uint32_t size = (std::uint32_t(incoming_[0]) << 24) | (std::uint32_t(incoming_[1]) << 16) | (std::uint32_t(incoming_[2]) << 8) | incoming_[3];
            if (size == 0 || size > MAX_MESSAGE_BYTES) throw std::runtime_error("Enquadramento IPC inválido");
            if (incoming_.size() >= size + 4) {
                std::vector<std::uint8_t> payload(incoming_.begin() + 4, incoming_.begin() + 4 + size);
                incoming_.erase(incoming_.begin(), incoming_.begin() + 4 + size);
                return decodeMessage(payload);
            }
        }
        pollfd p{fd_, POLLIN, 0};
        const int ready = poll(&p, 1, remaining(deadline));
        if (ready < 0 && errno == EINTR) continue;
        if (ready < 0) fail("Falha ao aguardar recepção IPC");
        if (ready == 0) return std::nullopt;
        std::uint8_t bytes[4096];
        const auto n = recv(fd_, bytes, sizeof(bytes), 0);
        if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        if (n < 0) fail("Falha ao receber IPC");
        if (n == 0) throw std::runtime_error("Canal IPC encerrado");
        incoming_.insert(incoming_.end(), bytes, bytes + n);
    } while (std::chrono::steady_clock::now() < deadline);
    return std::nullopt;
}
