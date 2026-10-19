#ifndef LOGISTICS_HPP
#define LOGISTICS_HPP

#include "fleet_state.hpp"
#include "protocol.hpp"
#include "route_planner.hpp"
#include <utility>
#include <vector>

struct AvisoLogistica {
    enum class TipoAviso { MissaoAtribuida, EquipeReunida, EquipeAcoplada, TransporteIniciado, CargaEntregue, MissaoCancelada, MissaoSuspensa, MissaoRetomada };
    TipoAviso tipo;
    Carga carga;
    MissaoEquipe missao;
    std::int64_t ocorrido_em_ms = instanteUnixMs();
};

struct ResultadoCicloLogistica {
    std::vector<std::pair<int, Mensagem>> comandos;
    std::vector<AvisoLogistica> eventos;
};

// Avança cargas somente após receber as confirmações de toda a equipe.
class OrganizadorLogistica {
public:
    OrganizadorLogistica(const MapaGalpao& galpao, std::vector<Carga> carga);
    ResultadoCicloLogistica processarCiclo(const std::vector<Mensagem>& mensagens, const std::vector<EstadoAtualRobo>& frota);
    std::vector<Carga> cargos() const;
    std::vector<MissaoEquipe> missoes() const;
    bool todasCargasEntregues() const;
private:
    const MapaGalpao& galpao_;
    PlanejadorRotas planejador_;
    std::map<IdEntidade, Carga> cargas_;
    std::map<int, BaiaCarregamento> baias_;
    std::map<IdEntidade, MissaoEquipe> missoes_;
    IdEntidade proxima_missao_ = 0;
    std::set<int> em_recarga_;
    std::vector<std::pair<int, Mensagem>> acoes_;
    std::vector<AvisoLogistica> avisos_;
    void agendarCargas(const std::vector<EstadoAtualRobo>& frota);
    void registrarMensagem(const Mensagem& mensagem, const std::vector<EstadoAtualRobo>& frota);
    void avancarMissao(MissaoEquipe& missao);
    void interromper(MissaoEquipe& missao, const std::string& motivo);
    void retomar(MissaoEquipe& missao, const std::vector<EstadoAtualRobo>& frota);
    void registrarAviso(AvisoLogistica::TipoAviso tipo, const MissaoEquipe& missao);
    void executarComando(MissaoEquipe& missao, TipoMensagem tipo, FaseMissao fase);
};

#endif
