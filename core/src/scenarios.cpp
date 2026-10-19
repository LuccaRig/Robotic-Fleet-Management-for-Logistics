#include "scenarios.hpp"
#include <algorithm>
#include <stdexcept>
#include <unistd.h>

namespace {
MapaGalpao layout() {
    MapaGalpao map(21, 15);
    for (int x = 0; x < 21; ++x) { map.definirCelula(x, 0, EstadoCelula::OBSTACULO); map.definirCelula(x, 14, EstadoCelula::OBSTACULO); }
    for (int y = 1; y < 14; ++y) {
        map.definirCelula(0, y, EstadoCelula::OBSTACULO); map.definirCelula(20, y, EstadoCelula::OBSTACULO);
        for (const int x : {6, 12, 17}) map.definirCelula(x, y, EstadoCelula::OBSTACULO);
    }
    int id = 0;
    for (const auto item : std::vector<std::pair<int,int>>{{6,3},{12,4},{17,5}}) {
        ++id; map.definirCelula(item.first, item.second, EstadoCelula::LIVRE); map.adicionarTrecho({id, id, {{item.first, item.second}}});
        map.definirCelula(item.first, 10, EstadoCelula::LIVRE); map.adicionarTrecho({10 + id, 4, {{item.first, 10}}});
    }
    map.adicionarBaia({1, {2,3}, {}}); map.adicionarBaia({2, {2,10}, {}});
    map.adicionarDestino({1, {19,3}}); map.adicionarDestino({2, {19,10}});
    map.definirCelula(2, 1, EstadoCelula::ESTACAO_RECARGA); map.definirCelula(19, 12, EstadoCelula::ESTACAO_RECARGA);
    return map;
}
Carga carga(IdEntidade id, int tamanho, int bay = 1, int destino = 1) {
    Carga c; c.id = id; c.tamanho = tamanho; c.id_baia = bay; c.id_destino = destino; return c;
}
bool allSuspended(const AplicacaoCentral& runtime, const MissaoEquipe& missao, bool healthy) {
    for (const auto id : missao.integrantes) {
        const auto s = runtime.frota().obter(id);
        if (!s || (healthy ? s->estado != EstadoRobo::SUSPENSO || s->falha : (s->estado != EstadoRobo::SUSPENSO && s->estado != EstadoRobo::COM_FALHA))) return false;
    }
    return true;
}
}
std::string nomesCenarios() { return "base, cooperativo, disponibilidade, passagens, rota-equipe, falhas, recarga, processos"; }
Cenario criarCenario(const std::string& nome) {
    Cenario s{nome, layout(), {carga(1,1)}, {{1,{2,5}}}};
    if (nome == "base") return s;
    if (nome == "cooperativo") { s.cargos = {carga(1,2), carga(2,3), carga(3,4)}; s.robos = {{1,{2,5}},{2,{3,5}},{3,{4,5}},{4,{5,5}}}; }
    else if (nome == "disponibilidade") s.cargos = {carga(1,2)};
    else if (nome == "rota-equipe") { s.cargos = {carga(1,4)}; s.robos = {{1,{2,5}},{2,{3,5}},{3,{4,5}},{4,{5,5}}}; }
    else if (nome == "falhas") { s.cargos = {carga(1,2)}; s.robos = {{1,{2,12}},{2,{3,12}}}; }
    else if (nome == "recarga") s.robos = {{1,{2,2}}};
    else if (nome == "processos") s.cargos.clear();
    else if (nome == "passagens") {
        s.galpao.adicionarBaia({3,{19,3},{}}); s.galpao.adicionarBaia({4,{3,3},{}}); s.galpao.adicionarBaia({5,{4,3},{}});
        s.galpao.adicionarBaia({6,{18,3},{}}); s.galpao.adicionarBaia({7,{19,4},{}}); s.galpao.adicionarDestino({3,{2,3}});
        s.cargos = {carga(1,1,1,1), carga(2,1,4,1), carga(3,1,5,1), carga(4,1,3,3), carga(5,1,6,3), carga(6,1,7,3)};
        s.robos = {{1,{2,3}},{2,{3,3}},{3,{4,3}},{4,{19,3}},{5,{18,3}},{6,{19,4}}};
    } else throw std::invalid_argument("Cenário desconhecido. Opções: " + nomesCenarios());
    s.galpao.validar(); return s;
}
void RoteiroCenario::avancarCiclo(AplicacaoCentral& runtime) {
    for (const auto& p : runtime.passagens()) {
        if (p.ocupado < 0 || p.ocupado > p.capacidade || p.maximo > p.capacidade) throw std::runtime_error("Invariante de capacidade violada");
        if (p.maximo > 0) passages_seen_.insert(p.id);
        if (p.aguardando > 0) queue_observed_ = true;
    }
    const auto first = runtime.frota().obter(1);
    if (name_ == "disponibilidade" && stage_ == 0 && first && first->conectado) {
        const auto cargos = runtime.cargos();
        if (!cargos.empty() && cargos.front().estado == EstadoCarga::PENDENTE && !cargos.front().motivo.empty()) {
            if (!runtime.adicionarRobo(2, {3,5})) throw std::runtime_error("Falha ao completar a equipe");
            runtime.imprimir("Evidência: carga aguardou equipe completa antes do despacho"); stage_ = 1;
        }
    } else if (name_ == "recarga") {
        if (stage_ == 0 && first && first->conectado && !first->acoplado) {
            if (!runtime.definirBateria(1, 10)) throw std::runtime_error("Falha ao injetar bateria baixa"); stage_ = 1;
        } else if (stage_ == 1 && runtime.quantidadeEventos(TipoEvento::ROBO_RECARREGANDO)) stage_ = 2;
        else if (stage_ == 2 && runtime.quantidadeEventos(TipoEvento::RECARGA_CONCLUIDA)) { runtime.imprimir("Evidência: recarga completa confirmada antes do retorno"); stage_ = 3; }
    } else if (name_ == "processos") {
        const auto second = runtime.frota().obter(2);
        if (stage_ == 0 && first && first->conectado) {
            if (runtime.adicionarRobo(1,{2,5}) || !runtime.adicionarRobo(2,{3,5})) throw std::runtime_error("Regra de ID duplicado incorreta"); stage_ = 1;
        } else if (stage_ == 1 && second && second->conectado) {
            for (const auto& p : runtime.processos()) if (p.pid == getpid()) throw std::runtime_error("Robô sem processo independente");
            generation_ = second->geracao;
            if (!runtime.interromperRobo(2)) throw std::runtime_error("Falha ao interromper processo"); stage_ = 2;
        } else if (stage_ == 2 && second && !second->conectado) {
            bool exists = false; for (const auto& p : runtime.processos()) if (p.id == 2) exists = true;
            if (!exists && runtime.removerRobo(2)) stage_ = 3;
        } else if (stage_ == 3 && runtime.adicionarRobo(2,{3,5})) stage_ = 4;
        else if (stage_ == 4 && second && second->conectado && second->geracao > generation_) { runtime.imprimir("Evidência: processo recriado com nova geração após remoção explícita"); stage_ = 5; }
    } else if (name_ == "falhas") {
        const auto missoes = runtime.missoes();
        const auto active = std::find_if(missoes.rbegin(), missoes.rend(), [](const MissaoEquipe& m) { return m.fase != FaseMissao::NENHUMA; });
        if (stage_ == 0 && active != missoes.rend() && active->fase == FaseMissao::INDO_PARA_COLETA && first && first->estado == EstadoRobo::INDO_PARA_COLETA) {
            if (!runtime.definirFalha(1,true)) throw std::runtime_error("Falha ao injetar defeito"); stage_ = 1;
        } else if (stage_ == 1 && runtime.quantidadeEventos(TipoEvento::MISSAO_CANCELADA) && first && first->falha) {
            runtime.definirFalha(1,false); stage_ = 2;
        } else if (stage_ == 2 && active != missoes.rend() && active->fase == FaseMissao::INDO_PARA_ENTREGA) {
            runtime.definirFalha(2,true); stage_ = 3;
        } else if (stage_ == 3 && active != missoes.rend() && active->fase == FaseMissao::SUSPENSO && allSuspended(runtime,*active,false)) {
            runtime.definirFalha(2,false); for (const auto id : active->integrantes) runtime.definirBateria(id,100); stage_ = 4;
        } else if (stage_ == 4) {
            if (runtime.quantidadeEventos(TipoEvento::MISSAO_RETOMADA)) stage_ = 6;
            else if (active != missoes.rend() && active->fase == FaseMissao::SUSPENSO && allSuspended(runtime,*active,true)) runtime.retomar(active->id);
        }
    }
}
bool RoteiroCenario::concluido(const AplicacaoCentral& runtime) const {
    if (!runtime.concluido()) return false;
    if (name_ == "disponibilidade") return stage_ == 1;
    if (name_ == "recarga") return stage_ == 3;
    if (name_ == "processos") return stage_ == 5;
    if (name_ == "falhas") return stage_ == 6 && runtime.quantidadeEventos(TipoEvento::MISSAO_CANCELADA) && runtime.quantidadeEventos(TipoEvento::MISSAO_SUSPENSA);
    if (name_ == "passagens") return queue_observed_ && passages_seen_.count(1) && passages_seen_.count(2) && passages_seen_.count(3);
    if (name_ == "rota-equipe") return passages_seen_.count(11) && passages_seen_.count(12) && passages_seen_.count(13) && !passages_seen_.count(1) && !passages_seen_.count(2) && !passages_seen_.count(3);
    return runtime.quantidadeEventos(TipoEvento::CARGA_ENTREGUE) > 0;
}
