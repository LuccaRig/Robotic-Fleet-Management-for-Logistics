#include "passages.hpp"
#include <algorithm>
#include <iterator>
#include <stdexcept>

ControlePassagens::ControlePassagens(const MapaGalpao& galpao, Emissor emissor, Observador observador)
    : galpao_(galpao), emissor_(std::move(emissor)), observador_(std::move(observador)) {
    for (const auto& pair : galpao.passagens()) { RegiaoTrecho region; region.trecho = pair.second; regioes_.emplace(pair.first, std::move(region)); }
}
std::string ControlePassagens::chave(const Mensagem& m) { return m.acoplado ? "e" + std::to_string(m.id_missao) : "r" + std::to_string(m.id_robo) + ":" + std::to_string(m.geracao); }
int ControlePassagens::sentido(const Mensagem& m) { const int dx = m.destino.x - m.posicao.x, dy = m.destino.y - m.posicao.y; return dx ? (dx > 0 ? 1 : -1) : (dy > 0 ? 2 : -2); }
Mensagem ControlePassagens::concederAcesso(const Mensagem& solicitarAcesso, int passage) const { auto m = solicitarAcesso; m.tipo = TipoMensagem::AcessoTrechoConcedido; m.id_trecho = passage; m.aceito = true; return m; }
void ControlePassagens::liberarFila(RegiaoTrecho& region, std::vector<Mensagem>& liberacoes) {
    while (!region.fila.empty()) {
        auto& aguardando = region.fila.front(); const auto& first = aguardando.solicitacoes.begin()->second;
        const int vagas = first.acoplado ? static_cast<int>(first.integrantes.size()) : 1;
        const int flow = sentido(first);
        if (region.ocupado + vagas > region.trecho.capacidade || (region.ocupado && region.sentido != flow)) break;
        region.reservas.emplace(aguardando.chave, Claim{first, vagas, flow, false}); region.ocupado += vagas; region.sentido = flow;
        region.maximo = std::max(region.maximo, region.ocupado);
        for (const auto& pair : aguardando.solicitacoes) liberacoes.push_back(concederAcesso(pair.second, region.trecho.id));
        region.fila.pop_front();
    }
}
void ControlePassagens::solicitarAcesso(const Mensagem& m) {
    std::vector<Mensagem> liberacoes;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto* passage = galpao_.trechoEm(m.destino);
        if (!passage) liberacoes.push_back(concederAcesso(m, -1));
        else {
            auto& region = regioes_.at(passage->id); const auto claim_key = chave(m);
            if (region.reservas.count(claim_key)) liberacoes.push_back(concederAcesso(m, passage->id));
            else {
                const int vagas = m.acoplado ? static_cast<int>(m.integrantes.size()) : 1;
                if (vagas < 1 || vagas > passage->capacidade || vagas != m.quantidade_integrantes) throw std::runtime_error("Solicitação incompatível com a capacidade da passagem");
                auto aguardando = std::find_if(region.fila.begin(), region.fila.end(), [&](const SolicitacaoPendente& item) { return item.chave == claim_key; });
                if (aguardando == region.fila.end()) { region.fila.push_back({claim_key, {}}); aguardando = std::prev(region.fila.end()); }
                aguardando->solicitacoes[m.id_robo] = m; liberarFila(region, liberacoes);
            }
        }
    }
    enviarRespostas(std::move(liberacoes), {});
}
void ControlePassagens::liberarTrechosVazios(RegiaoTrecho& region, std::vector<AvisoPassagem>& avisos) {
    for (auto it = region.reservas.begin(); it != region.reservas.end();) {
        auto integrantes = it->second.mensagem.acoplado ? it->second.mensagem.integrantes : std::vector<int>{it->second.mensagem.id_robo};
        bool ocupado = false, known = true;
        for (const auto id : integrantes) {
            const auto p = posicoes_.find(id); if (p == posicoes_.end()) { known = false; break; }
            const auto* passage = galpao_.trechoEm(p->second.posicao); if (passage && passage->id == region.trecho.id) ocupado = true;
        }
        if (ocupado && !it->second.entrou) { it->second.entrou = true; avisos.push_back({true, region.trecho.id, it->second.vagas, it->second.mensagem}); }
        if (known && !ocupado && it->second.entrou) {
            avisos.push_back({false, region.trecho.id, it->second.vagas, it->second.mensagem}); region.ocupado -= it->second.vagas; it = region.reservas.erase(it);
        } else ++it;
    }
    if (!region.ocupado) region.sentido = 0;
}
void ControlePassagens::confirmar(const Mensagem& leitura_sensores) {
    std::vector<Mensagem> liberacoes; std::vector<AvisoPassagem> avisos;
    {
        std::lock_guard<std::mutex> lock(mutex_); auto old = posicoes_.find(leitura_sensores.id_robo);
        if (old != posicoes_.end() && (leitura_sensores.geracao < old->second.geracao || (leitura_sensores.geracao == old->second.geracao && leitura_sensores.sequencia_sensor < old->second.sequencia_sensor))) return;
        posicoes_[leitura_sensores.id_robo] = leitura_sensores;
        for (auto& pair : regioes_) { liberarTrechosVazios(pair.second, avisos); liberarFila(pair.second, liberacoes); }
    }
    enviarRespostas(std::move(liberacoes), std::move(avisos));
}
void ControlePassagens::cancelar(int id_robo, IdEntidade id_missao) {
    std::vector<Mensagem> liberacoes;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& pair : regioes_) {
            auto& region = pair.second;
            for (auto it = region.fila.begin(); it != region.fila.end();) {
                for (auto r = it->solicitacoes.begin(); r != it->solicitacoes.end();) {
                    if (r->first == id_robo || (id_missao && r->second.id_missao == id_missao)) r = it->solicitacoes.erase(r); else ++r;
                }
                if (it->solicitacoes.empty()) it = region.fila.erase(it); else ++it;
            }
            for (auto it = region.reservas.begin(); it != region.reservas.end();) {
                const auto& mensagem = it->second.mensagem;
                const bool matches = mensagem.id_robo == id_robo || (id_missao && mensagem.id_missao == id_missao);
                bool outside = true;
                for (const auto id : (mensagem.acoplado ? mensagem.integrantes : std::vector<int>{mensagem.id_robo})) {
                    const auto p = posicoes_.find(id);
                    if (p == posicoes_.end()) { outside = false; break; }
                    const auto* passage = galpao_.trechoEm(p->second.posicao);
                    if (passage && passage->id == region.trecho.id) outside = false;
                }
                if (matches && outside && !it->second.entrou) { region.ocupado -= it->second.vagas; it = region.reservas.erase(it); } else ++it;
            }
            if (!region.ocupado) region.sentido = 0; liberarFila(region, liberacoes);
        }
    }
    enviarRespostas(std::move(liberacoes), {});
}
bool ControlePassagens::permiteMovimento(const Mensagem& m) const {
    std::lock_guard<std::mutex> lock(mutex_); const auto* passage = galpao_.trechoEm(m.destino);
    if (!passage) return true;
    const auto& reservas = regioes_.at(passage->id).reservas;
    const auto it = reservas.find(chave(m)); return it != reservas.end() && it->second.mensagem.id_missao == m.id_missao;
}
std::vector<EstadoTrecho> ControlePassagens::capturarEstado() const {
    std::lock_guard<std::mutex> lock(mutex_); std::vector<EstadoTrecho> result;
    for (const auto& pair : regioes_) { const auto& r = pair.second; result.push_back({pair.first, r.trecho.capacidade, r.ocupado, r.sentido, r.fila.size(), r.maximo}); } return result;
}
bool ControlePassagens::removerRobo(int id) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& pair : regioes_) for (const auto& claim : pair.second.reservas)
            if (claim.second.mensagem.acoplado && std::find(claim.second.mensagem.integrantes.begin(), claim.second.mensagem.integrantes.end(), id) != claim.second.mensagem.integrantes.end()) return false;
        const auto it = posicoes_.find(id); if (it == posicoes_.end()) return false;
        it->second.posicao = {-1, -1};
    }
    cancelar(id, 0);
    Mensagem removed; { std::lock_guard<std::mutex> lock(mutex_); removed = posicoes_.at(id); }
    confirmar(removed); return true;
}
void ControlePassagens::enviarRespostas(std::vector<Mensagem> liberacoes, std::vector<AvisoPassagem> avisos) {
    for (auto& m : liberacoes) emissor_(std::move(m));
    for (const auto& n : avisos) if (observador_) observador_(n);
}
