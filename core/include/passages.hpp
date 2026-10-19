#ifndef PASSAGES_HPP
#define PASSAGES_HPP

#include "protocol.hpp"
#include "warehouse.hpp"
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>

struct EstadoTrecho { int id; int capacidade; int ocupado; int sentido; std::size_t aguardando; int maximo; };
struct AvisoPassagem { bool entrou; int id_trecho; int vagas; Mensagem mensagem; };
// Reserva a capacidade inteira de uma passagem para cada robô ou equipe.
class ControlePassagens {
public:
    using Emissor = std::function<void(Mensagem)>;
    using Observador = std::function<void(const AvisoPassagem&)>;
    ControlePassagens(const MapaGalpao& galpao, Emissor emissor, Observador observador = {});
    void solicitarAcesso(const Mensagem& mensagem);
    void confirmar(const Mensagem& leitura_sensores);
    void cancelar(int id_robo, IdEntidade id_missao);
    bool permiteMovimento(const Mensagem& movement) const;
    std::vector<EstadoTrecho> capturarEstado() const;
    bool removerRobo(int id);
private:
    struct Claim { Mensagem mensagem; int vagas; int sentido; bool entrou = false; };
    struct SolicitacaoPendente { std::string chave; std::map<int, Mensagem> solicitacoes; };
    struct RegiaoTrecho { TrechoRestrito trecho; int ocupado = 0; int sentido = 0; int maximo = 0; std::map<std::string, Claim> reservas; std::deque<SolicitacaoPendente> fila; };
    const MapaGalpao& galpao_;
    Emissor emissor_;
    Observador observador_;
    mutable std::mutex mutex_;
    std::map<int, RegiaoTrecho> regioes_;
    std::map<int, Mensagem> posicoes_;
    static std::string chave(const Mensagem& mensagem);
    static int sentido(const Mensagem& mensagem);
    Mensagem concederAcesso(const Mensagem& solicitarAcesso, int passage) const;
    void liberarFila(RegiaoTrecho& region, std::vector<Mensagem>& liberacoes);
    void liberarTrechosVazios(RegiaoTrecho& region, std::vector<AvisoPassagem>& avisos);
    void enviarRespostas(std::vector<Mensagem> liberacoes, std::vector<AvisoPassagem> avisos);
};

#endif
