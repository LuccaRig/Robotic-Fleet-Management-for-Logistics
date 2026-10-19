#ifndef BUFFER_HPP
#define BUFFER_HPP

#include "data_collector.hpp"

#include <cstddef>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <vector>

// Mantém um histórico recente e encaminha os eventos ao coletor assíncrono.
class BufferEventos {
private:
    ColetorDados& coletor_;
    const std::size_t capacidade_;
    mutable std::mutex mutex_;
    std::deque<EventoRobo> eventos_;

public:
    explicit BufferEventos(ColetorDados& coletor, std::size_t capacidade = 100)
        : coletor_(coletor), capacidade_(capacidade) {
        if (capacidade_ == 0) {
            throw std::invalid_argument("A capacidade do Buffer deve ser maior que zero");
        }
    }

    BufferEventos(const BufferEventos&) = delete;
    BufferEventos& operator=(const BufferEventos&) = delete;

    // Só registra no histórico após o coletor aceitar o evento.
    void adicionarEvento(const EventoRobo& evento) {
        std::lock_guard<std::mutex> lock(mutex_);

        eventos_.push_back(evento);
        try {
            coletor_.enfileirarEvento(evento);
        } catch (...) {
            eventos_.pop_back();
            throw;
        }

        if (eventos_.size() > capacidade_) {
            eventos_.pop_front();
        }
    }

    std::vector<EventoRobo> capturarEventos() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return {eventos_.begin(), eventos_.end()};
    }

    void limpar() {
        std::lock_guard<std::mutex> lock(mutex_);
        eventos_.clear();
    }

    std::size_t quantidade() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return eventos_.size();
    }

    std::size_t capacidade() const noexcept {
        return capacidade_;
    }
};

#endif // BUFFER_HPP
