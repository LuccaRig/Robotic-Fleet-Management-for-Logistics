#ifndef LOGISTICS_HPP
#define LOGISTICS_HPP

#include "fleet_state.hpp"
#include "protocol.hpp"
#include "route_planner.hpp"
#include <condition_variable>
#include <deque>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>

struct AvisoLogistica {
    enum class TipoAviso { MissaoAtribuida, EquipeReunida, EquipeAcoplada, TransporteIniciado, CargaEntregue, MissaoCancelada, MissaoSuspensa, MissaoRetomada };
    TipoAviso tipo;
    Carga carga;
    MissaoEquipe missao;
    std::int64_t ocorrido_em_ms = instanteUnixMs();
};

// Avança cargas somente após receber as confirmações de toda a equipe.
class OrganizadorLogistica {
public:
    using Emissor = std::function<bool(int, Mensagem)>;
    using Observador = std::function<void(const AvisoLogistica&)>;
    OrganizadorLogistica(const MapaGalpao& galpao, const EstadoFrota& frota, std::vector<Carga> carga, Emissor emissor, Observador observador = {});
    ~OrganizadorLogistica();
    void iniciar();
    void parar();
    void aoReceberMensagem(const Mensagem& mensagem);
    void solicitarRetomada(IdEntidade id_missao);
    void processarUmaVez();
    std::vector<Carga> cargos() const;
    std::vector<MissaoEquipe> missoes() const;
    bool todasCargasEntregues() const;
    void verificarFalha() const;
private:
    const MapaGalpao& galpao_;
    const EstadoFrota& frota_;
    PlanejadorRotas planejador_;
    Emissor emissor_;
    Observador observador_;
    mutable std::mutex mutex_estado_;
    std::map<IdEntidade, Carga> cargas_;
    std::map<int, BaiaCarregamento> baias_;
    std::map<IdEntidade, MissaoEquipe> missoes_;
    IdEntidade proxima_missao_ = 0;
    std::set<int> em_recarga_;
    std::map<int, EstadoAtualRobo> saude_;
    std::vector<std::pair<int, Mensagem>> acoes_;
    std::vector<AvisoLogistica> avisos_;
    mutable std::mutex mutex_fila_;
    std::condition_variable cv_;
    std::deque<Mensagem> eventos_;
    bool executando_ = false;
    std::thread thread_;
    std::mutex ciclo_vida_;
    std::exception_ptr erro_;
    void executarRotina() noexcept;
    void agendarCargas();
    void registrarMensagem(const Mensagem& mensagem);
    void avancarMissao(MissaoEquipe& missao);
    void interromper(MissaoEquipe& missao, const std::string& motivo);
    void retomar(MissaoEquipe& missao);
    void registrarAviso(AvisoLogistica::TipoAviso tipo, const MissaoEquipe& missao);
    void executarComando(MissaoEquipe& missao, TipoMensagem tipo, FaseMissao fase);
};

#endif
