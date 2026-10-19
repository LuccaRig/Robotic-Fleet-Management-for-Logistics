#ifndef RUNTIME_HPP
#define RUNTIME_HPP

#include "event_recorder.hpp"
#include "hardware.hpp"
#include "orchestrator.hpp"
#include <chrono>
#include <map>
#include <set>

struct PosicaoInicialRobo { int id; Posicao posicao; };
struct Cenario {
    std::string nome;
    MapaGalpao galpao;
    std::vector<Carga> cargos;
    std::vector<PosicaoInicialRobo> robos;
};

class AplicacaoCentral {
public:
    AplicacaoCentral(std::string executavel, std::string banco, Cenario cenario);
    ~AplicacaoCentral();
    void iniciar();
    void parar();
    void avancarCiclo();
    bool adicionarRobo(int id, Posicao posicao);
    bool removerRobo(int id);
    bool interromperRobo(int id);
    bool definirBateria(int id, int bateria);
    bool definirFalha(int id, bool falha);
    void retomar(IdEntidade id_missao);
    bool concluido() const;
    std::size_t quantidadeEventos(TipoEvento tipo) const;
    void executarComando(const std::string& linha);
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
    OrganizadorLogistica logistics_;
    std::uint64_t commands_ = 0;
    std::map<std::uint64_t, Mensagem> pending_;
    std::vector<Mensagem> eventos_logistica_;
    std::set<int> removals_;
    std::chrono::steady_clock::time_point ultimo_ciclo_logistica_ = std::chrono::steady_clock::now();
    bool started_ = false;
    bool stopped_ = false;
    bool exit_requested_ = false;
    bool sendCommand(int id, Mensagem mensagem);
    void aoReceberMensagem(const Mensagem& mensagem);
    void aoReceberLeitura(Mensagem leitura_sensores);
    void tratarLeiturasHardware(std::vector<Mensagem> leituras);
    void tratarEfeitosPassagem(ResultadoPassagens resultado);
};

#endif
