#include "passages.hpp"
#include <algorithm>
#include <iterator>
#include <stdexcept>

ControlePassagens::ControlePassagens(const MapaGalpao& galpao) : galpao_(galpao) {
    for (const auto& par : galpao.passagens()) {
        RegiaoTrecho regiao;
        regiao.trecho = par.second;
        regioes_.emplace(par.first, std::move(regiao));
    }
}

std::string ControlePassagens::chave(const Mensagem& mensagem) {
    return mensagem.acoplado
        ? "e" + std::to_string(mensagem.id_missao)
        : "r" + std::to_string(mensagem.id_robo) + ":" + std::to_string(mensagem.geracao);
}

int ControlePassagens::sentido(const Mensagem& mensagem) {
    const int dx = mensagem.destino.x - mensagem.posicao.x;
    const int dy = mensagem.destino.y - mensagem.posicao.y;
    if (dx) return dx > 0 ? 1 : -1;
    return dy > 0 ? 2 : -2;
}

Mensagem ControlePassagens::concederAcesso(const Mensagem& solicitacao, int trecho) const {
    auto permissao = solicitacao;
    permissao.tipo = TipoMensagem::AcessoTrechoConcedido;
    permissao.id_trecho = trecho;
    permissao.aceito = true;
    return permissao;
}

void ControlePassagens::liberarFila(RegiaoTrecho& regiao, ResultadoPassagens& resultado) {
    while (!regiao.fila.empty()) {
        auto& aguardando = regiao.fila.front();
        const auto& primeira = aguardando.solicitacoes.begin()->second;
        const int vagas = primeira.acoplado ? static_cast<int>(primeira.integrantes.size()) : 1;
        const int fluxo = sentido(primeira);
        if (regiao.ocupado + vagas > regiao.trecho.capacidade || (regiao.ocupado && regiao.sentido != fluxo)) break;

        regiao.reservas.emplace(aguardando.chave, Claim{primeira, vagas, fluxo, false});
        regiao.ocupado += vagas;
        regiao.sentido = fluxo;
        regiao.maximo = std::max(regiao.maximo, regiao.ocupado);
        for (const auto& solicitacao : aguardando.solicitacoes) {
            resultado.permissoes.push_back(concederAcesso(solicitacao.second, regiao.trecho.id));
        }
        regiao.fila.pop_front();
    }
}

ResultadoPassagens ControlePassagens::solicitarAcesso(const Mensagem& mensagem) {
    ResultadoPassagens resultado;
    const auto* trecho = galpao_.trechoEm(mensagem.destino);
    if (!trecho) {
        resultado.permissoes.push_back(concederAcesso(mensagem, -1));
        return resultado;
    }

    auto& regiao = regioes_.at(trecho->id);
    const auto chave_reserva = chave(mensagem);
    if (regiao.reservas.count(chave_reserva)) {
        resultado.permissoes.push_back(concederAcesso(mensagem, trecho->id));
        return resultado;
    }

    const int vagas = mensagem.acoplado ? static_cast<int>(mensagem.integrantes.size()) : 1;
    if (vagas < 1 || vagas > trecho->capacidade || vagas != mensagem.quantidade_integrantes) {
        throw std::runtime_error("Solicitação incompatível com a capacidade da passagem");
    }

    auto aguardando = std::find_if(regiao.fila.begin(), regiao.fila.end(), [&](const SolicitacaoPendente& pedido) {
        return pedido.chave == chave_reserva;
    });
    if (aguardando == regiao.fila.end()) {
        regiao.fila.push_back({chave_reserva, {}});
        aguardando = std::prev(regiao.fila.end());
    }
    aguardando->solicitacoes[mensagem.id_robo] = mensagem;
    liberarFila(regiao, resultado);
    return resultado;
}

void ControlePassagens::liberarTrechosVazios(RegiaoTrecho& regiao, ResultadoPassagens& resultado) {
    for (auto reserva = regiao.reservas.begin(); reserva != regiao.reservas.end();) {
        const auto integrantes = reserva->second.mensagem.acoplado
            ? reserva->second.mensagem.integrantes
            : std::vector<int>{reserva->second.mensagem.id_robo};
        bool ocupado = false;
        bool posicoes_conhecidas = true;

        for (const auto id : integrantes) {
            const auto posicao = posicoes_.find(id);
            if (posicao == posicoes_.end()) {
                posicoes_conhecidas = false;
                break;
            }
            const auto* trecho_atual = galpao_.trechoEm(posicao->second.posicao);
            if (trecho_atual && trecho_atual->id == regiao.trecho.id) ocupado = true;
        }

        if (ocupado && !reserva->second.entrou) {
            reserva->second.entrou = true;
            resultado.eventos.push_back({true, regiao.trecho.id, reserva->second.vagas, reserva->second.mensagem});
        }
        if (posicoes_conhecidas && !ocupado && reserva->second.entrou) {
            resultado.eventos.push_back({false, regiao.trecho.id, reserva->second.vagas, reserva->second.mensagem});
            regiao.ocupado -= reserva->second.vagas;
            reserva = regiao.reservas.erase(reserva);
        } else {
            ++reserva;
        }
    }
    if (!regiao.ocupado) regiao.sentido = 0;
}

ResultadoPassagens ControlePassagens::confirmar(const Mensagem& leitura) {
    ResultadoPassagens resultado;
    const auto anterior = posicoes_.find(leitura.id_robo);
    if (anterior != posicoes_.end() &&
        (leitura.geracao < anterior->second.geracao ||
         (leitura.geracao == anterior->second.geracao && leitura.sequencia_sensor < anterior->second.sequencia_sensor))) {
        return resultado;
    }

    posicoes_[leitura.id_robo] = leitura;
    for (auto& regiao : regioes_) {
        liberarTrechosVazios(regiao.second, resultado);
        liberarFila(regiao.second, resultado);
    }
    return resultado;
}

ResultadoPassagens ControlePassagens::cancelar(int id_robo, IdEntidade id_missao) {
    ResultadoPassagens resultado;
    for (auto& par : regioes_) {
        auto& regiao = par.second;
        for (auto pedido = regiao.fila.begin(); pedido != regiao.fila.end();) {
            for (auto solicitacao = pedido->solicitacoes.begin(); solicitacao != pedido->solicitacoes.end();) {
                if (solicitacao->first == id_robo || (id_missao && solicitacao->second.id_missao == id_missao)) {
                    solicitacao = pedido->solicitacoes.erase(solicitacao);
                } else {
                    ++solicitacao;
                }
            }
            if (pedido->solicitacoes.empty()) pedido = regiao.fila.erase(pedido);
            else ++pedido;
        }

        for (auto reserva = regiao.reservas.begin(); reserva != regiao.reservas.end();) {
            const auto& mensagem = reserva->second.mensagem;
            const bool corresponde = mensagem.id_robo == id_robo || (id_missao && mensagem.id_missao == id_missao);
            bool fora_do_trecho = true;
            const auto integrantes = mensagem.acoplado ? mensagem.integrantes : std::vector<int>{mensagem.id_robo};
            for (const auto id : integrantes) {
                const auto posicao = posicoes_.find(id);
                if (posicao == posicoes_.end()) {
                    fora_do_trecho = false;
                    break;
                }
                const auto* trecho_atual = galpao_.trechoEm(posicao->second.posicao);
                if (trecho_atual && trecho_atual->id == regiao.trecho.id) fora_do_trecho = false;
            }
            if (corresponde && fora_do_trecho && !reserva->second.entrou) {
                regiao.ocupado -= reserva->second.vagas;
                reserva = regiao.reservas.erase(reserva);
            } else {
                ++reserva;
            }
        }
        if (!regiao.ocupado) regiao.sentido = 0;
        liberarFila(regiao, resultado);
    }
    return resultado;
}

bool ControlePassagens::permiteMovimento(const Mensagem& movimento) const {
    const auto* trecho = galpao_.trechoEm(movimento.destino);
    if (!trecho) return true;
    const auto& reservas = regioes_.at(trecho->id).reservas;
    const auto reserva = reservas.find(chave(movimento));
    return reserva != reservas.end() && reserva->second.mensagem.id_missao == movimento.id_missao;
}

std::vector<EstadoTrecho> ControlePassagens::capturarEstado() const {
    std::vector<EstadoTrecho> resultado;
    for (const auto& par : regioes_) {
        const auto& regiao = par.second;
        resultado.push_back({par.first, regiao.trecho.capacidade, regiao.ocupado, regiao.sentido, regiao.fila.size(), regiao.maximo});
    }
    return resultado;
}

ResultadoPassagens ControlePassagens::removerRobo(int id) {
    for (const auto& par : regioes_) {
        for (const auto& reserva : par.second.reservas) {
            const auto& mensagem = reserva.second.mensagem;
            if (mensagem.acoplado && std::find(mensagem.integrantes.begin(), mensagem.integrantes.end(), id) != mensagem.integrantes.end()) {
                ResultadoPassagens recusado;
                recusado.aceito = false;
                return recusado;
            }
        }
    }

    const auto posicao = posicoes_.find(id);
    if (posicao == posicoes_.end()) {
        ResultadoPassagens recusado;
        recusado.aceito = false;
        return recusado;
    }

    posicao->second.posicao = {-1, -1};
    auto resultado = cancelar(id, 0);
    Mensagem removido = posicoes_.at(id);
    auto confirmacao = confirmar(removido);
    resultado.permissoes.insert(resultado.permissoes.end(), confirmacao.permissoes.begin(), confirmacao.permissoes.end());
    resultado.eventos.insert(resultado.eventos.end(), confirmacao.eventos.begin(), confirmacao.eventos.end());
    return resultado;
}
