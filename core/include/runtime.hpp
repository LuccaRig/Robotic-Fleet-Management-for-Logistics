#ifndef RUNTIME_HPP
#define RUNTIME_HPP

#include "event_recorder.hpp"
#include "hardware.hpp"
#include "hardware_mqtt.hpp"
#include "mqtt_integration.hpp"
#include "orchestrator.hpp"
#include <chrono>
#include <map>
#include <memory>
#include <set>
#include <string>

struct PosicaoInicialRobo { int id; Posicao posicao; };
struct Cenario {
    std::string nome;
    MapaGalpao galpao;
    std::vector<Carga> cargos;
    std::vector<PosicaoInicialRobo> robos;
};

class AplicacaoCentral {
public:
    AplicacaoCentral(std::string executavel, std::string banco, Cenario cenario, bool simulador_mqtt = false);
    ~AplicacaoCentral();
    void iniciar();
    void parar();
    void avancarCiclo();
    bool adicionarRobo(int id, Posicao posicao);
    bool removerRobo(int id);
    bool interromperRobo(int id);
    bool definirBateria(int id, int bateria);
    bool definirFalha(int id, bool falha);
    void adicionarCarga(Carga carga);
    void retomar(IdEntidade id_missao);
    bool concluido() const;
    void exportarMapaTxt(const std::string& caminho_arquivo) const;
    std::size_t quantidadeEventos(TipoEvento tipo) const;
    void executarComando(const std::string& linha);
    void executarComandoOperador(const ComandoOperador& comando);
    void imprimir(const std::string& texto) const;
    const EstadoFrota& frota() const { return manager_.frota(); }
    std::vector<Carga> cargos() const { return logistics_.cargos(); }
    std::vector<MissaoEquipe> missoes() const { return logistics_.missoes(); }
    std::vector<ProcessoRobo> processos() const { return manager_.processos(); }
    std::vector<EstadoTrecho> passagens() const { return passages_.capturarEstado(); }
    bool encerramentoSolicitado() const { return exit_requested_; }
private:
    MapaGalpao galpao_;
    std::vector<PosicaoInicialRobo> initial_;
    ColetorDados collector_;
    std::map<TipoEvento, std::size_t> metrics_;
    GravadorEventos recorder_;
    GestorRobos manager_;
    ControlePassagens passages_;
    AdaptadorHardwareLocal hardware_;
    std::unique_ptr<CanalSimulacaoMqtt> simulador_;
    OrganizadorLogistica logistics_;
    IntegracaoMqtt mqtt_;
    std::uint64_t commands_ = 0;
    struct ComandoPendente { Mensagem mensagem; std::string id_operador; };
    std::map<std::uint64_t, ComandoPendente> pending_;
    std::vector<Mensagem> eventos_logistica_;
    std::set<int> removals_;
    std::map<int, Posicao> robos_solicitados_;
    std::map<int, std::uint64_t> robos_simulados_;
    struct ComandoFisico { Mensagem mensagem; std::chrono::steady_clock::time_point enviado; };
    std::map<int, ComandoFisico> comandos_fisicos_;
    std::chrono::steady_clock::time_point ultima_leitura_simulador_ = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point ultima_publicacao_simulador_ = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point ultimo_ciclo_logistica_ = std::chrono::steady_clock::now();
    bool started_ = false;
    bool stopped_ = false;
    bool exit_requested_ = false;
    bool sendCommand(int id, Mensagem mensagem, const std::string& id_operador = {});
    void aoReceberMensagem(const Mensagem& mensagem);
    void aoReceberLeitura(Mensagem leitura_sensores);
    void tratarLeiturasHardware(std::vector<Mensagem> leituras);
    void tratarEfeitosPassagem(ResultadoPassagens resultado);
    void processarSimulador();
    void enviarComandoFisico(Mensagem mensagem);
    std::optional<Mensagem> lerLeituraHardware(int id) const;
};

#endif
