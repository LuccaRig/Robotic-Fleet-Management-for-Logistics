#ifndef EVENT_RECORDER_HPP
#define EVENT_RECORDER_HPP

#include "buffer.hpp"
#include "logistics.hpp"
#include "passages.hpp"
#include <atomic>
#include <functional>
#include <map>
#include <mutex>

class GravadorEventos {
public:
    using Observador = std::function<void(const EventoRobo&)>;
    explicit GravadorEventos(BufferEventos& buffer, Observador observador = {});
    void aoReceberEventoRobo(const Mensagem& mensagem);
    void aoReceberEventoLogistica(const AvisoLogistica& aviso);
    void aoReceberEventoPassagem(const AvisoPassagem& aviso);
private:
    BufferEventos& buffer_;
    Observador observador_;
    std::mutex mutex_;
    std::map<int, EstadoAtualRobo> estados_anteriores_;
    std::atomic<std::uint64_t> sequencia_{0};
    void registrar(EventoRobo event);
};

#endif
