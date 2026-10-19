#ifndef ROBOT_EVENT_HPP
#define ROBOT_EVENT_HPP

#include "domain.hpp"
#include <atomic>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

enum class TipoEvento {
    ROBO_CRIADO = 0, ROBO_EM_MOVIMENTO = 1, ROBO_CHEGOU = 2, BATERIA_BAIXA = 3,
    ROBO_RECARREGANDO = 4, FALHA_NO_ROBO = 5, CARGA_COLETADA = 6, CARGA_ENTREGUE = 7,
    ROBO_PRONTO, ROBO_DESCONECTADO, ROBO_ENCERRADO, MISSAO_ATRIBUIDA,
    MISSAO_CANCELADA, EQUIPE_REUNIDA, TRANSPORTE_INICIADO, MISSAO_SUSPENSA,
    MISSAO_RETOMADA, ENTRADA_EM_TRECHO, SAIDA_DE_TRECHO, ROBO_RECUPERADO, RECARGA_CONCLUIDA
};

class EventoRobo {
public:
    EventoRobo() : EventoRobo(-1, -1, -1, {0, 0}, TipoEvento::ROBO_CRIADO, "", "") {}
    EventoRobo(int id_robo, int id_equipe, int id_baia, Posicao posicao, TipoEvento tipo, const std::string& descricao, const std::string& destino)
        : id_robo_(id_robo), id_equipe_(id_equipe), id_baia_(id_baia), posicao_(posicao), tipo_(tipo), descricao_(descricao), destino_(destino), ocorrido_em_ms_(instanteUnixMs()), sequencia_origem_(++contador_) {
        const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::tm local{}; localtime_r(&now, &local); std::ostringstream stream; stream << std::put_time(&local, "%d-%m-%Y %H:%M:%S"); instante_texto_ = stream.str();
    }
    int idRobo() const { return id_robo_; }
    int idEquipe() const { return id_equipe_; }
    int idBaia() const { return id_baia_; }
    Posicao posicao() const { return posicao_; }
    TipoEvento tipo() const { return tipo_; }
    const std::string& descricao() const { return descricao_; }
    const std::string& instanteTexto() const { return instante_texto_; }
    const std::string& destino() const { return destino_; }
    IdEntidade idCarga() const { return id_carga_; }
    IdEntidade idMissao() const { return id_missao_; }
    std::int64_t instanteOcorrencia() const { return ocorrido_em_ms_; }
    std::uint64_t geracao() const { return geracao_; }
    std::uint64_t sequenciaOrigem() const { return sequencia_origem_; }
    const std::string& origem() const { return origem_; }
    const std::vector<int>& participantes() const { return participantes_; }
    EventoRobo& rastrear(IdEntidade carga, IdEntidade missao, std::uint64_t geracao, std::string origem, std::uint64_t sequencia, std::int64_t ocorrido, std::vector<int> participantes = {}) {
        id_carga_ = carga; id_missao_ = missao; geracao_ = geracao; origem_ = std::move(origem);
        sequencia_origem_ = sequencia; ocorrido_em_ms_ = ocorrido; participantes_ = std::move(participantes);
        const auto segundos = static_cast<std::time_t>(ocorrido / 1000); std::tm local{};
        localtime_r(&segundos, &local); std::ostringstream stream; stream << std::put_time(&local, "%d-%m-%Y %H:%M:%S"); instante_texto_ = stream.str(); return *this;
    }
private:
    int id_robo_, id_equipe_, id_baia_;
    Posicao posicao_;
    TipoEvento tipo_;
    std::string descricao_, destino_, instante_texto_;
    IdEntidade id_carga_ = 0, id_missao_ = 0;
    std::int64_t ocorrido_em_ms_;
    std::uint64_t geracao_ = 0, sequencia_origem_;
    std::string origem_ = "local";
    std::vector<int> participantes_;
    inline static std::atomic<std::uint64_t> contador_{0};
};

#endif
