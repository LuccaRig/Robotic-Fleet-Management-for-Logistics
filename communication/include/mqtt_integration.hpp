#ifndef MQTT_INTEGRATION_HPP
#define MQTT_INTEGRATION_HPP

#include "domain.hpp"
#include "logistics_types.hpp"
#include "mqtt_client.hpp"
#include "passages.hpp"
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

enum class AcaoOperador { DefinirModo, DefinirDestino, DefinirFalha, DefinirBateria, AdicionarCarga, AdicionarLoteCargas, AdicionarRobo, RetomarMissao };

struct ComandoOperador {
    std::string id;
    int id_robo = -1;
    std::uint64_t geracao = 0;
    AcaoOperador acao = AcaoOperador::DefinirModo;
    ModoOperacao modo = ModoOperacao::AUTOMATICO;
    Posicao destino;
    bool falha = false;
    int bateria = 100;
    Carga carga;
    std::vector<Carga> cargas;
    Posicao posicao;
    IdEntidade id_missao = 0;
};

ComandoOperador decodificarComandoOperador(const std::string& payload, std::string* id_requisicao = nullptr);

class IntegracaoMqtt {
public:
    using Executor = std::function<void(const ComandoOperador&)>;

    ~IntegracaoMqtt();
    void iniciar();
    bool conectada() const { return cliente_.isConnected(); }
    void publicar(const std::vector<EstadoAtualRobo>& frota, const std::vector<Carga>& cargas,
                  const std::vector<MissaoEquipe>& missoes, const std::vector<EstadoTrecho>& passagens);
    void enfileirarComando(std::string payload, bool retido = false);
    void processarComandos(const Executor& executar);
    void responder(const std::string& id, bool aceito, const std::string& mensagem);
    void parar();

private:
    struct Entrada { std::string payload; bool retido; };
    struct Registro { ComandoOperador comando; std::string resposta; };
    ClienteMqtt cliente_;
    std::mutex mutex_fila_;
    std::vector<Entrada> fila_;
    std::deque<Registro> registros_;
    Registro* buscarRegistro(const std::string& id);
    std::string publicarResultado(const std::string& id, bool aceito, const std::string& mensagem);
};

#endif
