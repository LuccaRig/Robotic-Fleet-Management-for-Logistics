#ifndef ROUTE_PLANNER_HPP
#define ROUTE_PLANNER_HPP

#include "warehouse.hpp"
#include <optional>
#include <utility>

class PlanejadorRotas {
public:
    explicit PlanejadorRotas(const MapaGalpao& galpao) : galpao_(galpao) {}
    std::optional<std::vector<Posicao>> calcularRota(Posicao inicio, Posicao destino, int quantidade_integrantes = 1) const;
    std::optional<std::pair<Posicao, std::vector<Posicao>>> carregadorMaisProximo(Posicao inicio) const;
private:
    const MapaGalpao& galpao_;
};

#endif
