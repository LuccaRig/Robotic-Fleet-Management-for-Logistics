#ifndef EVENT_RECORDER_HPP
#define EVENT_RECORDER_HPP

#include "data_collector.hpp"
#include "logistics.hpp"
#include "passages.hpp"
#include <functional>
#include <map>

class GravadorEventos {
public:
    using Observador = std::function<void(const EventoRobo&)>;
    explicit GravadorEventos(ColetorDados& coletor, Observador observador = {});
    void aoReceberEventoRobo(const Mensagem& mensagem);
    void aoReceberEventoLogistica(const AvisoLogistica& aviso);
    void aoReceberEventoPassagem(const AvisoPassagem& aviso);
private:
    ColetorDados& coletor_;
    Observador observador_;
    std::map<int, EstadoAtualRobo> estados_anteriores_;
    std::uint64_t sequencia_ = 0;
    void registrar(EventoRobo event);
};

#endif
