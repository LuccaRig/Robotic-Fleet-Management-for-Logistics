#ifndef DOMAIN_HPP
#define DOMAIN_HPP

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

using IdEntidade = std::int64_t;

struct Posicao {
    int x = 0;
    int y = 0;
};

inline bool operator==(Posicao a, Posicao b) { return a.x == b.x && a.y == b.y; }
inline bool operator!=(Posicao a, Posicao b) { return !(a == b); }
inline int distanciaManhattan(Posicao a, Posicao b) {
    return (a.x > b.x ? a.x - b.x : b.x - a.x) + (a.y > b.y ? a.y - b.y : b.y - a.y);
}
inline std::int64_t instanteUnixMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

enum class EstadoRobo {
    DISPONIVEL, 
    INDO_PARA_COLETA, 
    AGUARDANDO_EQUIPE, 
    ACOPLANDO,
    INDO_PARA_ENTREGA, 
    AGUARDANDO_DESCARGA, 
    DESACOPLANDO,
    INDO_PARA_RECARGA, 
    RECARREGANDO, 
    SUSPENSO, 
    COM_FALHA, 
    ENCERRADO
};

enum class FaseMissao {
    NENHUMA, 
    INDO_PARA_COLETA, 
    COLETA_PRONTA, 
    ACOPLANDO, 
    INDO_PARA_ENTREGA,
    ENTREGA_PRONTA, 
    DESACOPLANDO, 
    INDO_PARA_RECARGA, 
    RECARREGANDO, 
    SUSPENSO
};

inline std::string nomeEstadoRobo(EstadoRobo estado) {
    switch (estado) {
    case EstadoRobo::DISPONIVEL: return "Disponível";
    case EstadoRobo::INDO_PARA_COLETA: return "Indo à coleta";
    case EstadoRobo::AGUARDANDO_EQUIPE: return "Aguardando equipe";
    case EstadoRobo::ACOPLANDO: return "Acoplando";
    case EstadoRobo::INDO_PARA_ENTREGA: return "Transportando";
    case EstadoRobo::AGUARDANDO_DESCARGA: return "Aguardando descarga";
    case EstadoRobo::DESACOPLANDO: return "Desacoplando";
    case EstadoRobo::INDO_PARA_RECARGA: return "Indo à recarga";
    case EstadoRobo::RECARREGANDO: return "Recarregando";
    case EstadoRobo::SUSPENSO: return "Missão suspensa";
    case EstadoRobo::COM_FALHA: return "Falha";
    case EstadoRobo::ENCERRADO: return "Encerrado";
    }
    return "Desconhecido";
}

struct EstadoAtualRobo {
    int id = -1;
    std::uint64_t geracao = 0;
    std::uint64_t sequencia = 0;
    Posicao posicao;
    Posicao destino;
    EstadoRobo estado = EstadoRobo::DISPONIVEL;
    FaseMissao fase = FaseMissao::NENHUMA;
    int bateria = 100;
    bool falha = false;
    bool acoplado = false;
    bool conectado = false;
    bool disponivel = false;
    IdEntidade id_missao = 0;
    IdEntidade id_carga = 0;
    IdEntidade id_equipe = 0;
    int id_baia = -1;
    std::uint64_t passo = 0;
    std::uint64_t sequencia_sensor = 0;
    std::uint64_t revisao = 0;
    std::uint64_t distancia = 0;
    std::int64_t atualizado_em_ms = 0;
};

#endif
