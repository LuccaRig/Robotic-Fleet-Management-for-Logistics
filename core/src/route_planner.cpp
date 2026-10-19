#include "route_planner.hpp"

#include <algorithm>
#include <queue>

std::optional<std::vector<Posicao>> PlanejadorRotas::calcularRota(
    Posicao inicio, Posicao destino, int quantidade_integrantes) const {
    if (quantidade_integrantes < 1 || quantidade_integrantes > 4) {
        return std::nullopt;
    }

    const auto podeUsar = [&](Posicao posicao) {
        const auto* trecho = galpao_.trechoEm(posicao);
        return galpao_.podeTransitar(posicao) &&
               (!trecho || trecho->capacidade >= quantidade_integrantes);
    };
    if (!podeUsar(inicio) || !podeUsar(destino)) {
        return std::nullopt;
    }

    const int largura = galpao_.largura();
    const auto indice = [largura](Posicao posicao) {
        return posicao.y * largura + posicao.x;
    };
    const auto posicaoNoIndice = [largura](int valor) {
        return Posicao{valor % largura, valor / largura};
    };

    std::vector<int> anterior(
        static_cast<std::size_t>(largura) * galpao_.altura(), -1);
    std::queue<Posicao> fila;
    fila.push(inicio);
    anterior[indice(inicio)] = indice(inicio);

    // A busca em largura encontra uma rota curta sem atravessar células incompatíveis.
    const Posicao movimentos[]{{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
    while (!fila.empty() && anterior[indice(destino)] < 0) {
        const Posicao atual = fila.front();
        fila.pop();

        for (const Posicao movimento : movimentos) {
            const Posicao proxima{atual.x + movimento.x, atual.y + movimento.y};
            if (!podeUsar(proxima) || anterior[indice(proxima)] >= 0) {
                continue;
            }

            anterior[indice(proxima)] = indice(atual);
            fila.push(proxima);
        }
    }

    if (anterior[indice(destino)] < 0) {
        return std::nullopt;
    }

    std::vector<Posicao> rota;
    for (int atual = indice(destino); atual != indice(inicio); atual = anterior[atual]) {
        rota.push_back(posicaoNoIndice(atual));
    }
    std::reverse(rota.begin(), rota.end());
    return rota;
}

std::optional<std::pair<Posicao, std::vector<Posicao>>>
PlanejadorRotas::carregadorMaisProximo(Posicao inicio) const {
    std::optional<std::pair<Posicao, std::vector<Posicao>>> resultado;

    for (const Posicao carregador : galpao_.carregadores()) {
        auto rota = calcularRota(inicio, carregador);
        if (rota && (!resultado || rota->size() < resultado->second.size())) {
            resultado = std::make_pair(carregador, std::move(*rota));
        }
    }

    return resultado;
}
