#ifndef ROBOT_AGENT_HPP
#define ROBOT_AGENT_HPP

#include "protocol.hpp"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>

// Mantém o estado local e separa a navegação do processamento de sensores.
class AgenteRobo {
public:
    using Emissor = std::function<void(Mensagem)>;
    AgenteRobo(int id, int x, int y, std::uint64_t geracao = 1, Emissor emissor = {});
    ~AgenteRobo();
    AgenteRobo(const AgenteRobo&) = delete;
    AgenteRobo& operator=(const AgenteRobo&) = delete;
    void iniciar();
    void parar();
    void processarMensagem(const Mensagem& mensagem);
    void avancarNavegacao();
    void atualizarStatus();
    EstadoAtualRobo capturarEstado() const;
    void verificarFalha() const;
    void atribuirMissao(const std::string& descricao, int x, int y);
    void comandarNavegacao() { avancarNavegacao(); }
    void processarStatus() { atualizarStatus(); }
    void atualizarEstadoRobo(int bateria, bool falha);
    int obterId() const { return capturarEstado().id; }
    int obterX() const { return capturarEstado().posicao.x; }
    int obterY() const { return capturarEstado().posicao.y; }
    int obterBateria() const { return capturarEstado().bateria; }
    EstadoRobo obterEstado() const { return capturarEstado().estado; }
    bool temFalha() const { return capturarEstado().falha; }
private:
    mutable std::mutex mutex_;
    std::mutex ciclo_vida_;
    std::condition_variable cv_;
    EstadoAtualRobo estado_;
    EstadoRobo estado_antes_falha_ = EstadoRobo::DISPONIVEL;
    Emissor emissor_;
    std::vector<Posicao> rota_;
    std::vector<int> integrantes_;
    std::deque<Mensagem> leituras_sensor_;
    Posicao ultima_posicao_distancia_;
    std::size_t indice_rota_ = 0;
    bool aguardando_posicao_ = false;
    bool aguardando_passagem_ = false;
    bool passagem_liberada_ = false;
    bool estado_alterado_ = true;
    bool comando_pendente_ = false;
    bool executando_ = false;
    bool encerrando_ = false;
    std::uint64_t ultimo_id_comando_ = 0;
    bool ultimo_comando_aceito_ = true;
    std::uint64_t sequencia_sensor_local_ = 0;
    std::thread thread_navegacao_;
    std::thread thread_status_;
    std::exception_ptr erro_;
    void emitirMensagem(Mensagem mensagem);
    void publicarEstado();
    void executarRotina(bool navegacao) noexcept;
};

#endif
