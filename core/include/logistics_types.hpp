#ifndef LOGISTICS_TYPES_HPP
#define LOGISTICS_TYPES_HPP

#include "domain.hpp"
#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>

enum class EstadoCarga { PENDENTE, RESERVADA, REUNINDO_EQUIPE, ACOPLANDO, EM_TRANSPORTE, DESACOPLANDO, ENTREGUE, SUSPENSO };
inline std::string nomeEstadoCarga(EstadoCarga estado) {
    switch (estado) {
    case EstadoCarga::PENDENTE: return "Pendente";
    case EstadoCarga::RESERVADA: return "Reservada";
    case EstadoCarga::REUNINDO_EQUIPE: return "Reunindo equipe";
    case EstadoCarga::ACOPLANDO: return "Acoplando";
    case EstadoCarga::EM_TRANSPORTE: return "Em transporte";
    case EstadoCarga::DESACOPLANDO: return "Desacoplando";
    case EstadoCarga::ENTREGUE: return "Entregue";
    case EstadoCarga::SUSPENSO: return "Suspensa";
    }
    return "Desconhecido";
}
struct Carga {
    IdEntidade id = 0;
    int tamanho = 1;
    int id_baia = -1;
    int id_destino = -1;
    EstadoCarga estado = EstadoCarga::PENDENTE;
    IdEntidade id_missao = 0;
    std::string motivo;
};
struct BaiaCarregamento { int id; Posicao posicao; std::deque<IdEntidade> fila_cargas; };
struct DestinoEntrega { int id; Posicao posicao; };
struct TrechoRestrito { int id; int capacidade; std::vector<Posicao> cells; };
struct MissaoEquipe {
    IdEntidade id = 0;
    IdEntidade id_carga = 0;
    FaseMissao fase = FaseMissao::INDO_PARA_COLETA;
    FaseMissao fase_suspensa = FaseMissao::NENHUMA;
    std::vector<int> integrantes;
    std::map<int, std::uint64_t> geracoes;
    std::map<int, EstadoAtualRobo> relatos;
    std::vector<Posicao> rota_entrega;
    std::set<int> integrantes_acoplados;
    bool ja_acoplou = false;
};

#endif
