#ifndef IPC_CHANNEL_HPP
#define IPC_CHANNEL_HPP

#include "protocol.hpp"
#include <chrono>
#include <mutex>
#include <optional>
#include <vector>

class IpcChannel {
public:
    explicit IpcChannel(int fd);
    ~IpcChannel();
    IpcChannel(const IpcChannel&) = delete;
    IpcChannel& operator=(const IpcChannel&) = delete;
    void send(const Message& message, std::chrono::milliseconds timeout = std::chrono::milliseconds(1000));
    std::optional<Message> receive(std::chrono::milliseconds timeout);
    void interrupt() noexcept;
    int descriptor() const noexcept { return fd_; }
private:
    int fd_;
    std::mutex send_mutex_;
    std::vector<std::uint8_t> incoming_;
};

#endif
