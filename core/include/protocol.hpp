#ifndef PROTOCOL_HPP
#define PROTOCOL_HPP

#include "domain.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

constexpr std::uint32_t VERSAO_PROTOCOLO = 1;
constexpr std::size_t TAMANHO_MAXIMO_MENSAGEM = 64 * 1024;

enum class TipoMensagem : std::uint32_t {
    SaudacaoRobo = 1, RoboPronto, AtribuirMissao, CancelarMissao, ConfirmacaoComando,
    AtualizarSensores, EstadoAtualRobo, DestinoNavegacao, SolicitarAcessoTrecho,
    AcessoTrechoConcedido, AcessoTrechoLiberado, Acoplar, Desacoplar, PausarMissao,
    RetomarMissao, Encerrar, RoboEncerrado, RoboDesconectado
};

struct Mensagem {
    TipoMensagem tipo = TipoMensagem::SaudacaoRobo;
    int id_robo = -1;
    std::uint64_t geracao = 0;
    std::uint64_t sequencia = 0;
    std::uint64_t id_comando = 0;
    std::uint64_t revisao = 0;
    IdEntidade id_missao = 0;
    IdEntidade id_carga = 0;
    IdEntidade id_equipe = 0;
    int id_baia = -1;
    FaseMissao fase = FaseMissao::NENHUMA;
    EstadoRobo estado = EstadoRobo::DISPONIVEL;
    Posicao posicao;
    Posicao destino;
    int bateria = 100;
    bool falha = false;
    bool acoplado = false;
    bool aceito = true;
    std::uint64_t distancia = 0;
    std::uint64_t passo = 0;
    std::uint64_t sequencia_sensor = 0;
    std::int64_t ocorrido_em_ms = 0;
    int id_trecho = -1;
    int sentido = 0;
    int quantidade_integrantes = 1;
    std::vector<Posicao> rota;
    std::vector<int> integrantes;
    std::string texto;
};

std::vector<std::uint8_t> codificarMensagem(const Mensagem& mensagem);
Mensagem decodificarMensagem(const std::vector<std::uint8_t>& bytes);
EstadoAtualRobo estadoRoboDaMensagem(const Mensagem& mensagem);
Mensagem mensagemDoEstadoRobo(const EstadoAtualRobo& capturarEstado);

#endif
