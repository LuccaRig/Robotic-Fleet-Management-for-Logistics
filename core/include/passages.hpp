#ifndef PASSAGES_HPP
#define PASSAGES_HPP

#include "protocol.hpp"
#include "warehouse.hpp"
#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>

struct EstadoTrecho { int id; int capacidade; int ocupado; int sentido; std::size_t aguardando; int maximo; };
struct AvisoPassagem { bool entrou; int id_trecho; int vagas; Mensagem mensagem; };
struct ResultadoPassagens { bool aceito = true; std::vector<Mensagem> permissoes; std::vector<AvisoPassagem> eventos; };
// Reserva a capacidade inteira de uma passagem para cada robô ou equipe.
class ControlePassagens {
public:
    explicit ControlePassagens(const MapaGalpao& galpao);
    ResultadoPassagens solicitarAcesso(const Mensagem& mensagem);
    ResultadoPassagens confirmar(const Mensagem& leitura_sensores);
    ResultadoPassagens cancelar(int id_robo, IdEntidade id_missao);
    bool permiteMovimento(const Mensagem& movement) const;
    std::vector<EstadoTrecho> capturarEstado() const;
    ResultadoPassagens removerRobo(int id);
private:
    struct Claim { Mensagem mensagem; int vagas; int sentido; bool entrou = false; };
    struct SolicitacaoPendente { std::string chave; std::map<int, Mensagem> solicitacoes; };
    struct RegiaoTrecho { TrechoRestrito trecho; int ocupado = 0; int sentido = 0; int maximo = 0; std::map<std::string, Claim> reservas; std::deque<SolicitacaoPendente> fila; };
    const MapaGalpao& galpao_;
    std::map<int, RegiaoTrecho> regioes_;
    std::map<int, Mensagem> posicoes_;
    static std::string chave(const Mensagem& mensagem);
    static int sentido(const Mensagem& mensagem);
    Mensagem concederAcesso(const Mensagem& solicitarAcesso, int passage) const;
    void liberarFila(RegiaoTrecho& region, ResultadoPassagens& resultado);
    void liberarTrechosVazios(RegiaoTrecho& region, ResultadoPassagens& resultado);
};

#endif
