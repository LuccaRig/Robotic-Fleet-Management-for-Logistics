#include "warehouse.hpp"
#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>

MapaGalpao::MapaGalpao(int width, int height) : largura_(width), altura_(height) {
    if (width <= 0 || height <= 0 || static_cast<long long>(width) * height > 1000000) throw std::invalid_argument("Dimensões de mapa inválidas");
    grade_.assign(static_cast<std::size_t>(width) * height, EstadoCelula::LIVRE);
}
MapaGalpao::MapaGalpao(const std::vector<std::vector<int>>& layout)
    : MapaGalpao(layout.empty() ? 0 : static_cast<int>(layout.front().size()), static_cast<int>(layout.size())) {
    for (int y = 0; y < altura_; ++y) {
        if (layout[y].size() != static_cast<std::size_t>(largura_)) throw std::invalid_argument("O mapa deve ser retangular");
        for (int x = 0; x < largura_; ++x) {
            const int value = layout[y][x];
            if (value < 0 || value > 6 || value == 1) throw std::invalid_argument("Célula inicial inválida; robôs são gerenciados separadamente");
            grade_[index(x, y)] = static_cast<EstadoCelula>(value);
        }
    }
}
void MapaGalpao::definirCelula(int x, int y, EstadoCelula estado) {
    if (x < 0 || y < 0 || x >= largura_ || y >= altura_) throw std::out_of_range("Posição fora do mapa");
    if (estado == EstadoCelula::ROBO || static_cast<unsigned>(estado) > 6) throw std::invalid_argument("O mapa estático não armazena robôs");
    grade_[index(x, y)] = estado;
}
EstadoCelula MapaGalpao::obterCelula(int x, int y) const {
    if (x < 0 || y < 0 || x >= largura_ || y >= altura_) return EstadoCelula::OBSTACULO;
    return grade_[index(x, y)];
}
bool MapaGalpao::podeTransitar(Posicao p) const { return obterCelula(p.x, p.y) != EstadoCelula::OBSTACULO; }
void MapaGalpao::imprimirMapa() const {
    for (int y = 0; y < altura_; ++y) { for (int x = 0; x < largura_; ++x) std::cout << static_cast<int>(obterCelula(x, y)) << ' '; std::cout << '\n'; }
}
void MapaGalpao::limpar() { std::fill(grade_.begin(), grade_.end(), EstadoCelula::LIVRE); baias_.clear(); destinations_.clear(); passages_.clear(); }
void MapaGalpao::adicionarBaia(BaiaCarregamento bay) {
    if (bay.id < 0 || !podeTransitar(bay.posicao) || baias_.count(bay.id)) throw std::invalid_argument("Baía inválida ou duplicada");
    definirCelula(bay.posicao.x, bay.posicao.y, EstadoCelula::BAIA_COLETA); baias_.emplace(bay.id, std::move(bay));
}
void MapaGalpao::adicionarDestino(DestinoEntrega d) {
    if (d.id < 0 || !podeTransitar(d.posicao) || destinations_.count(d.id)) throw std::invalid_argument("Destino inválido ou duplicado");
    definirCelula(d.posicao.x, d.posicao.y, EstadoCelula::AREA_ENTREGA); destinations_.emplace(d.id, d);
}
void MapaGalpao::adicionarTrecho(TrechoRestrito p) {
    if (p.id < 0 || p.capacidade < 1 || p.capacidade > 4 || p.cells.empty() || passages_.count(p.id)) throw std::invalid_argument("Passagem inválida ou duplicada");
    for (auto cell : p.cells) if (!podeTransitar(cell) || trechoEm(cell)) throw std::invalid_argument("Região de passagem inválida");
    for (auto cell : p.cells) definirCelula(cell.x, cell.y, EstadoCelula::TRECHO_RESTRITO);
    passages_.emplace(p.id, std::move(p));
}
const TrechoRestrito* MapaGalpao::trechoEm(Posicao p) const {
    for (const auto& pair : passages_) for (const auto cell : pair.second.cells) if (cell == p) return &pair.second;
    return nullptr;
}
std::vector<Posicao> MapaGalpao::carregadores() const {
    std::vector<Posicao> result;
    for (int y = 0; y < altura_; ++y) for (int x = 0; x < largura_; ++x) if (obterCelula(x, y) == EstadoCelula::ESTACAO_RECARGA) result.push_back({x, y});
    return result;
}
void MapaGalpao::validar() const {
    for (int y = 0; y < altura_; ++y) for (int x = 0; x < largura_; ++x)
        if (obterCelula(x, y) == EstadoCelula::TRECHO_RESTRITO && !trechoEm({x, y})) throw std::invalid_argument("Túnel sem capacidade cadastrada");
    for (const auto& a : passages_) for (const auto cell : a.second.cells) {
        for (const Posicao delta : std::vector<Posicao>{{1,0},{-1,0},{0,1},{0,-1}}) {
            const auto* b = trechoEm({cell.x + delta.x, cell.y + delta.y});
            if (b && b->id != a.first) throw std::invalid_argument("Passagens distintas devem ter uma área livre entre si");
        }
    }
    for (const auto& b : baias_) if (!podeTransitar(b.second.posicao) || trechoEm(b.second.posicao)) throw std::invalid_argument("Baía em posição inválida");
    for (const auto& d : destinations_) if (!podeTransitar(d.second.posicao) || trechoEm(d.second.posicao)) throw std::invalid_argument("Destino em posição inválida");
}
