#ifndef BUFFER_HPP
#define BUFFER_HPP

#include "data_collector.hpp"

#include <cstddef>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <vector>

// Buffer local de eventos: mantem um historico limitado e encaminha
// cada evento aceito para a fila assincrona do DataCollector.
// Nao e um mecanismo de IPC entre processos.
class Buffer {
private:
    DataCollector& collector_;
    const std::size_t capacity_;
    mutable std::mutex mutex_;
    std::deque<RobotEvent> events_;

public:
    explicit Buffer(DataCollector& collector, std::size_t capacity = 100)
        : collector_(collector), capacity_(capacity) {
        if (capacity_ == 0) {
            throw std::invalid_argument("A capacidade do Buffer deve ser maior que zero");
        }
    }

    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

    // Thread-safe: ao retornar sem excecao, o evento ja foi aceito pela
    // fila do coletor; nao significa que o SQLite ja concluiu a escrita.
    // O mutex tambem preserva a ordem de encaminhamento entre produtores.
    void addEvent(const RobotEvent& event) {
        std::lock_guard<std::mutex> lock(mutex_);

        events_.push_back(event);
        try {
            collector_.enqueueEvent(event);
        } catch (...) {
            events_.pop_back();
            throw;
        }

        // O limite se aplica apenas ao historico local. Os eventos mais
        // antigos ja foram encaminhados ao DataCollector para persistencia.
        if (events_.size() > capacity_) {
            events_.pop_front();
        }
    }

    std::vector<RobotEvent> snapshot() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return {events_.begin(), events_.end()};
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        events_.clear();
    }

    std::size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return events_.size();
    }

    std::size_t capacity() const noexcept {
        return capacity_;
    }
};

#endif // BUFFER_HPP
