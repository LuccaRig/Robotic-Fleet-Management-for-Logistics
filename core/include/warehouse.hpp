#ifndef WAREHOUSE_HPP
#define WAREHOUSE_HPP

#include "logistics_types.hpp"
#include <cstdint>
#include <map>
#include <vector>

enum class EstadoCelula : std::uint8_t {
    LIVRE = 0, ROBO = 1, ESTACAO_RECARGA = 2, TRECHO_RESTRITO = 3,
    BAIA_COLETA = 4, OBSTACULO = 5, AREA_ENTREGA = 6
};

class MapaGalpao {
public:
    MapaGalpao(int width, int height);
    explicit MapaGalpao(const std::vector<std::vector<int>>& layout);
    void definirCelula(int x, int y, EstadoCelula estado);
    EstadoCelula obterCelula(int x, int y) const;
    bool podeTransitar(Posicao p) const;
    int largura() const { return largura_; }
    int altura() const { return altura_; }
    void imprimirMapa() const;
    void limpar();
    void adicionarBaia(BaiaCarregamento bay);
    void adicionarDestino(DestinoEntrega destino);
    void adicionarTrecho(TrechoRestrito passage);
    const std::map<int, BaiaCarregamento>& baias() const { return baias_; }
    const std::map<int, DestinoEntrega>& destinos() const { return destinations_; }
    const std::map<int, TrechoRestrito>& passagens() const { return passages_; }
    const TrechoRestrito* trechoEm(Posicao p) const;
    std::vector<Posicao> carregadores() const;
    void validar() const;
private:
    int largura_;
    int altura_;
    std::vector<EstadoCelula> grade_;
    std::map<int, BaiaCarregamento> baias_;
    std::map<int, DestinoEntrega> destinations_;
    std::map<int, TrechoRestrito> passages_;
    std::size_t index(int x, int y) const { return static_cast<std::size_t>(y) * largura_ + x; }
};

#endif
