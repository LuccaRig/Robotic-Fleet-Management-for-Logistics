#ifndef ORCHESTRATOR_HPP
#define ORCHESTRATOR_HPP

#include "protocol.hpp"
#include "fleet_state.hpp"
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct ProcessoRobo {
    int id = -1;
    int pid = -1;
    std::uint64_t geracao = 0;
    bool pronto = false;
    bool encerrando = false;
};

class GestorRobos {
public:
    using Handler = std::function<void(const Mensagem&)>;
    explicit GestorRobos(std::string executavel = {});
    ~GestorRobos();
    GestorRobos(const GestorRobos&) = delete;
    GestorRobos& operator=(const GestorRobos&) = delete;
    bool iniciarProcessoRobo(int id, int start_x, int start_y);
    bool encerrarProcessoRobo(int id);
    bool enviar(int id, Mensagem mensagem);
    void definirTratadorMensagem(Handler tratador);
    void definirValidadorPosicao(std::function<bool(Posicao)> validador);
    std::vector<ProcessoRobo> processos() const;
    std::size_t quantidadeRobosAtivos() const;
    bool existeProcessoRobo(int id) const;
    void imprimirEstadosRobos() const;
    void verificarFalha() const;
    const EstadoFrota& frota() const;
    void parar();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

int executarAgenteRobo(int fd, int id, std::uint64_t geracao, Posicao inicio);

#endif
