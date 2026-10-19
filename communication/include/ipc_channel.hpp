#ifndef IPC_CHANNEL_HPP
#define IPC_CHANNEL_HPP

#include "protocol.hpp"
#include <chrono>
#include <mutex>
#include <optional>
#include <vector>

class CanalIpc {
public:
    explicit CanalIpc(int fd);
    ~CanalIpc();
    CanalIpc(const CanalIpc&) = delete;
    CanalIpc& operator=(const CanalIpc&) = delete;
    void enviar(const Mensagem& mensagem, std::chrono::milliseconds timeout = std::chrono::milliseconds(1000));
    std::optional<Mensagem> receber(std::chrono::milliseconds timeout);
    void interromper() noexcept;
    int descriptor() const noexcept { return fd_; }
private:
    int fd_;
    std::timed_mutex send_mutex_;
    std::vector<std::uint8_t> incoming_;
};

#endif
