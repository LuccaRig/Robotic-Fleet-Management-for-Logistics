#include "runtime.hpp"
#include "hardware_mqtt.hpp"
#include <algorithm>
#include <csignal>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <unistd.h>
#include <fstream>
#include <map>
#include <utility>

AplicacaoCentral::AplicacaoCentral(std::string executavel, std::string banco, Cenario cenario, bool simulador_mqtt)
    : galpao_(std::move(cenario.galpao)), initial_(std::move(cenario.robos)), collector_(banco),
      recorder_(collector_, [this](const EventoRobo& event) { ++metrics_[event.tipo()]; imprimir("Evento: " + event.descricao() + " | robô " + std::to_string(event.idRobo()) + " | missão " + std::to_string(event.idMissao())); }),
      manager_(std::move(executavel)),
      passages_(galpao_),
      hardware_(galpao_),
      logistics_(galpao_, std::move(cenario.cargos)) {
    galpao_.validar();
    if (simulador_mqtt) simulador_ = std::make_unique<CanalSimulacaoMqtt>();
}
AplicacaoCentral::~AplicacaoCentral() { try { parar(); } catch (const std::exception& e) { imprimir("Falha no encerramento: " + std::string(e.what())); } }
void AplicacaoCentral::iniciar() {
    if (started_) return;
    collector_.iniciar(); started_ = true;
    mqtt_.iniciar();
    if (simulador_) simulador_->iniciar();
    for (const auto& robo : initial_) if (!adicionarRobo(robo.id, robo.posicao)) throw std::runtime_error("Não foi possível disponibilizar o robô " + std::to_string(robo.id));
    imprimir("Sistema iniciado. Execução " + std::to_string(collector_.idExecucao()));
}
void AplicacaoCentral::parar() {
    if (stopped_) return;
    stopped_ = true;
    std::exception_ptr error;
    try {
        for (const auto& mensagem : manager_.parar()) aoReceberMensagem(mensagem);
        if (mqtt_.conectada()) mqtt_.publicar(manager_.frota().capturarEstado(), logistics_.cargos(), logistics_.missoes(), passages_.capturarEstado());
    } catch (...) { error = std::current_exception(); }
    if (simulador_) simulador_->parar();
    mqtt_.parar();
    try { collector_.parar(); } catch (...) { if (!error) error = std::current_exception(); }
    if (error) std::rethrow_exception(error);
}
bool AplicacaoCentral::sendCommand(int id, Mensagem mensagem, const std::string& id_operador) {
    const auto robo = manager_.frota().obter(id); if (!robo || !robo->conectado) return false;
    mensagem.id_robo = id; mensagem.geracao = robo->geracao; mensagem.id_comando = ++commands_; mensagem.revisao = mensagem.id_comando;
    pending_[mensagem.id_comando] = {mensagem, id_operador};
    try {
        if (manager_.enviar(id, mensagem)) return true;
    } catch (...) { pending_.erase(mensagem.id_comando); throw; }
    pending_.erase(mensagem.id_comando); return false;
}
void AplicacaoCentral::aoReceberLeitura(Mensagem leitura_sensores) {
    if (simulador_) {
        const auto estado = manager_.frota().obter(leitura_sensores.id_robo);
        if (!estado || !estado->conectado || estado->geracao != leitura_sensores.geracao ||
            estado->revisao != leitura_sensores.revisao || estado->id_missao != leitura_sensores.id_missao ||
            leitura_sensores.sequencia_sensor <= estado->sequencia_sensor) return;
        leitura_sensores.estado = estado->estado;
        leitura_sensores.fase = estado->fase;
        leitura_sensores.modo = estado->modo;
        leitura_sensores.destino = estado->destino;
    }
    manager_.atualizarLeituraSensores(leitura_sensores);
    tratarEfeitosPassagem(passages_.confirmar(leitura_sensores));
    if (leitura_sensores.id_missao == 0 && leitura_sensores.fase == FaseMissao::INDO_PARA_DESTINO_MANUAL && (leitura_sensores.falha || leitura_sensores.bateria <= 15)) {
        tratarEfeitosPassagem(passages_.cancelar(leitura_sensores.id_robo, 0));
    }
    eventos_logistica_.push_back(leitura_sensores);
    manager_.enviar(leitura_sensores.id_robo, std::move(leitura_sensores));
}
void AplicacaoCentral::tratarLeiturasHardware(std::vector<Mensagem> leituras) {
    for (auto& leitura : leituras) aoReceberLeitura(std::move(leitura));
}
void AplicacaoCentral::tratarEfeitosPassagem(ResultadoPassagens resultado) {
    for (const auto& evento : resultado.eventos) recorder_.aoReceberEventoPassagem(evento);
    for (auto& permissao : resultado.permissoes) {
        const auto id_robo = permissao.id_robo;
        if (manager_.enviar(id_robo, std::move(permissao))) continue;
        Mensagem desconectado;
        desconectado.tipo = TipoMensagem::RoboDesconectado;
        desconectado.id_robo = id_robo;
        const auto robo = manager_.frota().obter(id_robo);
        if (robo) desconectado.geracao = robo->geracao;
        desconectado.texto = "Não foi possível enviar permissão de passagem";
        eventos_logistica_.push_back(std::move(desconectado));
    }
}
void AplicacaoCentral::aoReceberMensagem(const Mensagem& m) {
    if (m.tipo == TipoMensagem::RoboPronto) {
        if (!simulador_) hardware_.registrarRobo(estadoRoboDaMensagem(m));
        robos_solicitados_.erase(m.id_robo);
        tratarEfeitosPassagem(passages_.confirmar(m));
        imprimir("Robô " + std::to_string(m.id_robo) + " pronto, geração " + std::to_string(m.geracao));
    }
    if (m.tipo == TipoMensagem::ConfirmacaoComando) {
        const auto it = pending_.find(m.id_comando);
        if (it != pending_.end() && it->second.mensagem.id_robo == m.id_robo && it->second.mensagem.geracao == m.geracao) {
            auto pendente = std::move(it->second);
            pending_.erase(it);
            auto& original = pendente.mensagem;
            if (m.aceito) {
                if (simulador_) enviarComandoFisico(original);
                else tratarLeiturasHardware(hardware_.confirmarComandoAceito(original));
                if (original.tipo == TipoMensagem::CancelarMissao || original.tipo == TipoMensagem::PausarMissao) {
                    tratarEfeitosPassagem(passages_.cancelar(m.id_robo, original.id_missao));
                }
                if (!pendente.id_operador.empty()) {
                    original.ocorrido_em_ms = m.ocorrido_em_ms;
                    recorder_.aoReceberComandoOperador(original);
                }
            }
            if (!pendente.id_operador.empty()) {
                mqtt_.responder(pendente.id_operador, m.aceito, m.aceito ? "Comando confirmado pelo robô" : "Comando recusado pelo robô; consulte o estado atual");
            }
        }
    } else if (m.tipo == TipoMensagem::SolicitarAcessoTrecho) {
        const auto lerLeitura = lerLeituraHardware(m.id_robo);
        if (lerLeitura && lerLeitura->geracao == m.geracao &&
            ((simulador_ && m.revisao > lerLeitura->revisao) ||
             (lerLeitura->id_missao == m.id_missao && lerLeitura->revisao == m.revisao && lerLeitura->fase == m.fase)) &&
            !lerLeitura->falha && lerLeitura->bateria > 0 &&
            (lerLeitura->bateria > 15 || m.fase == FaseMissao::INDO_PARA_RECARGA) && lerLeitura->posicao == m.posicao) {
            tratarEfeitosPassagem(passages_.solicitarAcesso(m));
        }
    } else if (m.tipo == TipoMensagem::DestinoNavegacao) {
        const auto leitura = lerLeituraHardware(m.id_robo);
        if (!leitura || leitura->geracao != m.geracao ||
            !((simulador_ && m.revisao > leitura->revisao) ||
              (leitura->id_missao == m.id_missao && leitura->revisao == m.revisao && leitura->fase == m.fase)) ||
            leitura->falha || leitura->bateria == 0 ||
            (leitura->bateria <= 15 && m.fase != FaseMissao::INDO_PARA_RECARGA)) return;
        if (!passages_.permiteMovimento(m)) throw std::runtime_error("Movimento sem permissão de passagem");
        if (simulador_) enviarComandoFisico(m);
        else tratarLeiturasHardware(hardware_.setpoint(m));
    }
    else if (m.tipo == TipoMensagem::EstadoAtualRobo && !simulador_) hardware_.registrarObservacao(estadoRoboDaMensagem(m));
    else if (m.tipo == TipoMensagem::RoboDesconectado) {
        robos_solicitados_.erase(m.id_robo);
        if (simulador_) {
            comandos_fisicos_.erase(m.id_robo);
            simulador_->enviar(m);
        } else hardware_.desconectar(m.id_robo);
        const auto lerLeitura = lerLeituraHardware(m.id_robo);
        tratarEfeitosPassagem(passages_.cancelar(m.id_robo, lerLeitura ? lerLeitura->id_missao : 0));
        for (auto it = pending_.begin(); it != pending_.end();) {
            if (it->second.mensagem.id_robo == m.id_robo && it->second.mensagem.geracao == m.geracao) {
                if (!it->second.id_operador.empty()) mqtt_.responder(it->second.id_operador, false, "Robô desconectado antes da confirmação: " + m.texto);
                it = pending_.erase(it);
            } else ++it;
        }
    }
    recorder_.aoReceberEventoRobo(m);
    if (!stopped_) eventos_logistica_.push_back(m);
}
std::optional<Mensagem> AplicacaoCentral::lerLeituraHardware(int id) const {
    if (!simulador_) return hardware_.lerLeitura(id);
    const auto estado = manager_.frota().obter(id);
    return estado ? std::optional<Mensagem>(mensagemDoEstadoRobo(*estado)) : std::nullopt;
}
void AplicacaoCentral::enviarComandoFisico(Mensagem mensagem) {
    mensagem.rota.clear();
    if (!simulador_->enviar(mensagem)) throw std::runtime_error("Simulador MQTT desconectado");
    if (mensagem.tipo != TipoMensagem::AtualizarSensores) {
        comandos_fisicos_[mensagem.id_robo] = {std::move(mensagem), std::chrono::steady_clock::now()};
    }
}
void AplicacaoCentral::processarSimulador() {
    const auto agora = std::chrono::steady_clock::now();
    for (const auto& mensagem : simulador_->receber()) {
        if (mensagem.id_robo < 0 || !mensagem.geracao) continue;
        if (mensagem.tipo == TipoMensagem::SaudacaoRobo) {
            const auto conhecido = robos_simulados_.find(mensagem.id_robo);
            if (conhecido != robos_simulados_.end()) {
                if (conhecido->second != mensagem.geracao) throw std::runtime_error("O simulador reiniciou; reinicie a operação");
            } else {
                if (!galpao_.podeTransitar(mensagem.posicao) || galpao_.trechoEm(mensagem.posicao) ||
                    !manager_.iniciarProcessoRobo(mensagem.id_robo, mensagem.posicao.x, mensagem.posicao.y, mensagem.geracao)) {
                    throw std::runtime_error("Não foi possível iniciar o robô anunciado pelo simulador");
                }
                robos_simulados_[mensagem.id_robo] = mensagem.geracao;
                robos_solicitados_.emplace(mensagem.id_robo, mensagem.posicao);
            }
            ultima_leitura_simulador_ = agora;
        } else if (robos_simulados_.count(mensagem.id_robo) && robos_simulados_.at(mensagem.id_robo) == mensagem.geracao) {
            ultima_leitura_simulador_ = agora;
            if (mensagem.tipo == TipoMensagem::AtualizarSensores) {
                const auto pendente = comandos_fisicos_.find(mensagem.id_robo);
                if (pendente != comandos_fisicos_.end()) {
                    const auto& comando = pendente->second.mensagem;
                    const bool movimento = comando.tipo == TipoMensagem::DestinoNavegacao;
                    if (mensagem.revisao == (movimento ? comando.revisao : comando.id_comando) &&
                        (!movimento || (mensagem.passo >= comando.passo && mensagem.posicao == comando.destino))) {
                        comandos_fisicos_.erase(pendente);
                    }
                }
                aoReceberLeitura(mensagem);
            } else if (mensagem.tipo == TipoMensagem::RoboEncerrado && removals_.count(mensagem.id_robo)) {
                if (!mensagem.aceito) throw std::runtime_error("Remoção física recusada pelo simulador");
                auto resultado = passages_.removerRobo(mensagem.id_robo);
                if (!resultado.aceito) throw std::runtime_error("Remoção física recusada");
                tratarEfeitosPassagem(std::move(resultado));
                robos_simulados_.erase(mensagem.id_robo);
                removals_.erase(mensagem.id_robo);
                comandos_fisicos_.erase(mensagem.id_robo);
            }
        }
    }
    for (const auto& pendente : comandos_fisicos_) {
        if (agora - pendente.second.enviado >= std::chrono::seconds(3)) throw std::runtime_error("Movimento ou comando físico sem confirmação dos sensores");
    }
    if ((!robos_simulados_.empty() || !robos_solicitados_.empty()) && agora - ultima_leitura_simulador_ >= std::chrono::seconds(3)) {
        throw std::runtime_error("Simulador MQTT sem atualização de sensores ou anúncios");
    }
    if (agora - ultima_publicacao_simulador_ < std::chrono::milliseconds(500)) return;
    for (const auto& robo : robos_solicitados_) {
        if (manager_.existeProcessoRobo(robo.first)) continue;
        Mensagem anuncio;
        anuncio.tipo = TipoMensagem::SaudacaoRobo;
        anuncio.id_robo = robo.first;
        anuncio.posicao = robo.second;
        simulador_->enviar(anuncio);
    }
    for (const auto& pendente : comandos_fisicos_) simulador_->enviar(pendente.second.mensagem);
    ultima_publicacao_simulador_ = agora;
}
void AplicacaoCentral::avancarCiclo() {
    if (simulador_) processarSimulador();
    for (const auto& mensagem : manager_.processarCiclo()) aoReceberMensagem(mensagem);
    collector_.verificarFalha();
    if (!simulador_) tratarLeiturasHardware(hardware_.avancarCiclo());
    mqtt_.processarComandos([this](const ComandoOperador& comando) { executarComandoOperador(comando); });
    const auto agora = std::chrono::steady_clock::now();
    if (!eventos_logistica_.empty() || agora - ultimo_ciclo_logistica_ >= std::chrono::milliseconds(200)) {
        std::set<int> bloqueados;
        for (const auto& pedido : pending_) {
            if (!pedido.second.id_operador.empty()) bloqueados.insert(pedido.second.mensagem.id_robo);
        }
        auto resultado = logistics_.processarCiclo(eventos_logistica_, manager_.frota().capturarEstado(), bloqueados);
        eventos_logistica_.clear();
        ultimo_ciclo_logistica_ = agora;
        for (auto& evento : resultado.eventos) recorder_.aoReceberEventoLogistica(evento);
        for (auto& comando : resultado.comandos) {
            if (sendCommand(comando.first, std::move(comando.second))) continue;
            Mensagem desconectado;
            desconectado.tipo = TipoMensagem::RoboDesconectado;
            desconectado.id_robo = comando.first;
            const auto robo = manager_.frota().obter(comando.first);
            if (robo) desconectado.geracao = robo->geracao;
            desconectado.texto = "Não foi possível enviar comando";
            eventos_logistica_.push_back(std::move(desconectado));
        }
    }
    for (auto it = removals_.begin(); it != removals_.end();) {
        if (manager_.existeProcessoRobo(*it)) { ++it; continue; }
        if (simulador_) {
            auto mensagem = *lerLeituraHardware(*it);
            mensagem.tipo = TipoMensagem::RoboEncerrado;
            simulador_->enviar(mensagem);
            ++it;
            continue;
        }
        if (!hardware_.removerRobo(*it)) throw std::runtime_error("Remoção física recusada");
        auto resultado = passages_.removerRobo(*it);
        if (!resultado.aceito) throw std::runtime_error("Remoção física recusada");
        tratarEfeitosPassagem(std::move(resultado));
        it = removals_.erase(it);
    }


    exportarMapaTxt("mapa_execucao.txt");
    if (mqtt_.conectada()) mqtt_.publicar(manager_.frota().capturarEstado(), logistics_.cargos(), logistics_.missoes(), passages_.capturarEstado());
}
bool AplicacaoCentral::adicionarRobo(int id, Posicao posicao) {
    if (id < 0 || !galpao_.podeTransitar(posicao) || galpao_.trechoEm(posicao)) return false;
    if (simulador_) {
        if (robos_solicitados_.count(id) || robos_simulados_.count(id) || manager_.existeProcessoRobo(id)) return false;
        robos_solicitados_.emplace(id, posicao);
        return true;
    }
    if (hardware_.contemRobo(id)) return false;
    return manager_.iniciarProcessoRobo(id, posicao.x, posicao.y);
}
bool AplicacaoCentral::removerRobo(int id) {
    const auto robo = manager_.frota().obter(id);
    if (!robo || robo->acoplado || robo->id_missao || removals_.count(id)) return false;
    removals_.insert(id); if (manager_.existeProcessoRobo(id)) manager_.encerrarProcessoRobo(id); return true;
}
bool AplicacaoCentral::interromperRobo(int id) { for (const auto& p : manager_.processos()) if (p.id == id) return kill(p.pid, SIGTERM) == 0; return false; }
bool AplicacaoCentral::definirBateria(int id, int bateria) {
    if (simulador_) {
        auto mensagem = lerLeituraHardware(id);
        if (!mensagem || bateria < 0 || bateria > 100) return false;
        mensagem->tipo = TipoMensagem::AtualizarSensores;
        mensagem->id_comando = ++commands_;
        mensagem->texto = "bateria";
        mensagem->bateria = bateria;
        enviarComandoFisico(*mensagem);
        return true;
    }
    auto leituras = hardware_.injetarLeitura(id, bateria, std::nullopt);
    if (!leituras) return false;
    tratarLeiturasHardware(std::move(*leituras));
    return true;
}
bool AplicacaoCentral::definirFalha(int id, bool falha) {
    if (simulador_) {
        auto mensagem = lerLeituraHardware(id);
        if (!mensagem) return false;
        mensagem->tipo = TipoMensagem::AtualizarSensores;
        mensagem->id_comando = ++commands_;
        mensagem->texto = "falha";
        mensagem->falha = falha;
        enviarComandoFisico(*mensagem);
        return true;
    }
    auto leituras = hardware_.injetarLeitura(id, std::nullopt, falha);
    if (!leituras) return false;
    tratarLeiturasHardware(std::move(*leituras));
    return true;
}
void AplicacaoCentral::retomar(IdEntidade id_missao) {
    ultimo_ciclo_logistica_ = {};
    logistics_.retomarMissao(id_missao, std::exchange(eventos_logistica_, {}), manager_.frota().capturarEstado());
}
void AplicacaoCentral::adicionarCarga(Carga carga) {
    logistics_.adicionarCarga(std::move(carga));
    ultimo_ciclo_logistica_ = {};
}
void AplicacaoCentral::adicionarCargas(std::vector<Carga> cargas) {
    logistics_.adicionarCargas(std::move(cargas));
    ultimo_ciclo_logistica_ = {};
}

void AplicacaoCentral::executarComandoOperador(const ComandoOperador& comando) {
    if (!started_ || stopped_) throw std::runtime_error("Aplicação não está em execução");
    if (comando.id.empty()) throw std::invalid_argument("Informe o ID da requisição de operação");
    if (comando.acao == AcaoOperador::AdicionarCarga) {
        adicionarCarga(comando.carga);
        mqtt_.responder(comando.id, true, "Carga adicionada ao fim da fila da baia");
        return;
    }
    if (comando.acao == AcaoOperador::AdicionarLoteCargas) {
        adicionarCargas(comando.cargas);
        mqtt_.responder(comando.id, true, "Lote de cargas adicionado atomicamente às filas das baias");
        return;
    }
    if (comando.acao == AcaoOperador::RetomarMissao) {
        retomar(comando.id_missao);
        mqtt_.responder(comando.id, true, "Retomada autorizada; acompanhe a equipe na telemetria");
        return;
    }
    if (comando.acao == AcaoOperador::AdicionarRobo) {
        if (!adicionarRobo(comando.id_robo, comando.posicao)) throw std::runtime_error("Não foi possível adicionar o robô; confira ID e posição inicial");
        mqtt_.responder(comando.id, true, "Disponibilização solicitada; aguarde a prontidão do robô na telemetria");
        return;
    }
    const auto robo = manager_.frota().obter(comando.id_robo);
    if (!robo || !robo->conectado) throw std::runtime_error("Robô inexistente ou desconectado");
    if (robo->geracao != comando.geracao) throw std::runtime_error("Geração do robô desatualizada; consulte a telemetria");
    if (comando.acao == AcaoOperador::DefinirFalha || comando.acao == AcaoOperador::DefinirBateria) {
        const bool aplicado = comando.acao == AcaoOperador::DefinirFalha ? definirFalha(robo->id, comando.falha) : definirBateria(robo->id, comando.bateria);
        if (!aplicado) throw std::runtime_error("Não foi possível atualizar a leitura do robô");
        mqtt_.responder(comando.id, true, simulador_ ? "Intervenção encaminhada; acompanhe os sensores na telemetria" : "Leitura atualizada no simulador");
        return;
    }
    for (const auto& pendente : pending_) {
        if (pendente.second.mensagem.id_robo == robo->id) throw std::runtime_error("Aguarde a confirmação do comando anterior deste robô");
    }
    if (comando.acao == AcaoOperador::DefinirModo && comando.modo == robo->modo) {
        mqtt_.responder(comando.id, true, "Robô já está no modo solicitado");
        return;
    }
    if (!roboLivre(*robo)) {
        throw std::runtime_error("Operação exige robô parado, sem falha, desacoplado e fora de missão");
    }
    for (const auto& missao : logistics_.missoes()) {
        if (missao.fase != FaseMissao::NENHUMA && std::find(missao.integrantes.begin(), missao.integrantes.end(), robo->id) != missao.integrantes.end()) {
            throw std::runtime_error("Robô reservado por uma missão de carga");
        }
    }

    Mensagem mensagem = mensagemDoEstadoRobo(*robo);
    mensagem.passo = 0;
    mensagem.integrantes = {robo->id};
    if (comando.acao == AcaoOperador::DefinirModo) {
        if (comando.modo != ModoOperacao::MANUAL && comando.modo != ModoOperacao::AUTOMATICO) throw std::invalid_argument("Modo de operação inválido");
        mensagem.tipo = TipoMensagem::DefinirModo;
        mensagem.modo = comando.modo;
        mensagem.destino = robo->posicao;
    } else if (comando.acao == AcaoOperador::DefinirDestino) {
        if (robo->modo != ModoOperacao::MANUAL) throw std::runtime_error("Destino manual exige modo manual confirmado");
        PlanejadorRotas planejador(galpao_);
        const auto rota = planejador.calcularRota(robo->posicao, comando.destino);
        if (!rota) throw std::runtime_error("Destino fora do mapa, bloqueado ou sem rota acessível");
        const bool recarga = galpao_.obterCelula(comando.destino.x, comando.destino.y) == EstadoCelula::ESTACAO_RECARGA;
        if (rota->size() > static_cast<std::size_t>(robo->bateria)) {
            throw std::runtime_error("Bateria insuficiente para alcançar o destino");
        }
        if (!recarga) {
            const auto carregador = planejador.carregadorMaisProximo(comando.destino);
            if (!carregador || robo->bateria <= 15 || rota->size() + carregador->second.size() + 15 >= static_cast<std::size_t>(robo->bateria)) {
                throw std::runtime_error("Destino exige bateria suficiente para o trajeto e o retorno a um carregador");
            }
        }
        mensagem.tipo = TipoMensagem::DefinirDestinoManual;
        mensagem.fase = recarga ? FaseMissao::INDO_PARA_RECARGA : FaseMissao::INDO_PARA_DESTINO_MANUAL;
        mensagem.destino = comando.destino;
        mensagem.rota = *rota;
    } else {
        throw std::invalid_argument("Ação de operação desconhecida");
    }
    if (!sendCommand(robo->id, std::move(mensagem), comando.id)) throw std::runtime_error("Não foi possível enviar o comando ao robô");
}

bool AplicacaoCentral::concluido() const {
    if (simulador_ && (!robos_solicitados_.empty() || !comandos_fisicos_.empty())) return false;
    if (!logistics_.todasCargasEntregues()) return false;
    for (const auto& robo : manager_.frota().capturarEstado()) if (robo.conectado && (robo.id_missao || robo.acoplado || robo.estado != EstadoRobo::DISPONIVEL)) return false;
    return true;
}
std::size_t AplicacaoCentral::quantidadeEventos(TipoEvento tipo) const { const auto it = metrics_.find(tipo); return it == metrics_.end() ? 0 : it->second; }
void AplicacaoCentral::imprimir(const std::string& mensagem) const { std::cout << mensagem << std::endl; }
void AplicacaoCentral::executarComando(const std::string& linha) {
    std::istringstream input(linha); std::string action; input >> action;
    if (action.empty()) return;
    auto exhausted = [&] { std::string extra; if (input >> extra) throw std::invalid_argument("Argumentos adicionais inesperados"); };
    auto integer = [&] { int value; if (!(input >> value)) throw std::invalid_argument("Informe um número inteiro válido"); return value; };
    if (action == "sair") { exhausted(); exit_requested_ = true; }
    else if (action == "status") {
        exhausted();
        for (const auto& s : frota().capturarEstado()) imprimir("Robô " + std::to_string(s.id) + " | " + nomeEstadoRobo(s.estado) + " | modo " + (s.modo == ModoOperacao::MANUAL ? "Manual" : "Automático") + " | posição " + std::to_string(s.posicao.x) + "," + std::to_string(s.posicao.y) + " | bateria " + std::to_string(s.bateria) + "% | distância " + std::to_string(s.distancia) + " | " + (s.conectado ? "Conectado" : "Desconectado"));
        for (const auto& p : processos()) imprimir("Processo do robô " + std::to_string(p.id) + " | PID " + std::to_string(p.pid) + " | geração " + std::to_string(p.geracao));
    } else if (action == "cargas") {
        exhausted(); for (const auto& c : cargos()) imprimir("Carga " + std::to_string(c.id) + " | " + nomeEstadoCarga(c.estado) + " | missão " + std::to_string(c.id_missao) + " | " + c.motivo);
    } else if (action == "passagens") {
        exhausted(); for (const auto& p : passagens()) imprimir("Passagem " + std::to_string(p.id) + " | ocupação " + std::to_string(p.ocupado) + "/" + std::to_string(p.capacidade) + " | esperando " + std::to_string(p.aguardando));
    } else if (action == "robo") {
        std::string operation; input >> operation; const auto id = integer(); bool ok = false;
        if (operation == "adicionar") { const auto x = integer(); const auto y = integer(); exhausted(); ok = adicionarRobo(id, {x, y}); }
        else if (operation == "remover") { exhausted(); ok = removerRobo(id); }
        else if (operation == "interromper") { exhausted(); ok = interromperRobo(id); }
        else throw std::invalid_argument("Operação de robô desconhecida");
        if (!ok) throw std::runtime_error("Operação recusada: verifique ID, posição e missão do robô");
    } else if (action == "bateria" || action == "falha") {
        const auto id = integer(); const auto value = integer(); exhausted();
        if (action == "falha" && value != 0 && value != 1) throw std::invalid_argument("Falha deve ser 0 ou 1");
        const bool ok = action == "bateria" ? definirBateria(id, value) : definirFalha(id, value != 0);
        if (!ok) throw std::runtime_error("Atualização recusada");
    } else if (action == "retomar") { const auto id = integer(); exhausted(); retomar(id); }
    else if (action == "ajuda") { exhausted(); imprimir("Comandos: status; cargas; passagens; robo adicionar ID X Y; robo remover ID; robo interromper ID; bateria ID 0..100; falha ID 0|1; retomar MISSÃO; sair"); }
    else throw std::invalid_argument("Comando desconhecido. Digite ajuda");
}

void AplicacaoCentral::exportarMapaTxt(const std::string& caminho_arquivo) const {
    std::ofstream arquivo(caminho_arquivo);
    if (!arquivo.is_open()) return;

    auto frota = manager_.frota().capturarEstado();

    std::map<std::pair<int, int>, std::vector<int>> robos_por_posicao;
    for (const auto& robo : frota) {
        if (robo.conectado) {
            robos_por_posicao[{robo.posicao.x, robo.posicao.y}].push_back(robo.id);
        }
    }

    arquivo << "=== MAPA DO GALPÃO (Tempo de Execução) ===\n";
    
    int cell_width = 5; 

    arquivo << "+";
    for (int x = 0; x < galpao_.largura_; ++x) {
        arquivo << std::string(cell_width, '-');
    }
    arquivo << "+\n";

    for (int y = 0; y < galpao_.altura_; ++y) {
        arquivo << "|";
        for (int x = 0; x < galpao_.largura_; ++x) {
            auto it = robos_por_posicao.find({x, y});
            
            if (it != robos_por_posicao.end() && !it->second.empty()) {
                const auto& ids = it->second;
                if (ids.size() == 1) {
                    std::string texto = "R" + std::to_string(ids[0]);
                    int pad = cell_width - texto.length();
                    int pad_l = pad / 2;
                    int pad_r = pad - pad_l;
                    arquivo << std::string(pad_l, ' ') << texto << std::string(pad_r, ' ');
                } else {
                    std::string texto = "[";
                    for (size_t i = 0; i < ids.size(); ++i) {
                        texto += std::to_string(ids[i]);
                        if (i + 1 < ids.size()) texto += ",";
                    }
                    texto += "]";
                    
                    if (texto.length() > static_cast<size_t>(cell_width)) {
                        texto = "!COL!";
                    }
                    
                    int pad = cell_width - texto.length();
                    int pad_l = pad / 2;
                    int pad_r = pad - pad_l;
                    arquivo << std::string(pad_l, ' ') << texto << std::string(pad_r, ' ');
                }
            } else {
                int valor_celula = static_cast<int>(galpao_.obterCelula(x, y));
                std::string simbolo = (valor_celula == 0) ? "." : std::to_string(valor_celula);
                
                int pad = cell_width - simbolo.length();
                int pad_l = pad / 2;
                int pad_r = pad - pad_l;
                arquivo << std::string(pad_l, ' ') << simbolo << std::string(pad_r, ' ');
            }
        }
        arquivo << "|\n";
    }

    arquivo << "+";
    for (int x = 0; x < galpao_.largura_; ++x) {
        arquivo << std::string(cell_width, '-');
    }
    arquivo << "+\n\n";

    arquivo << "Legenda:\n";
    arquivo << "  '.' = Livre | '1', '2'... = Tipos de célula do mapa | 'R0' = Robô único\n";
    arquivo << "  '[0,1]' = Múltiplos robôs na mesma célula!\n\n";
    
    arquivo << "Posições ativas da Frota:\n";
    for (const auto& robo : frota) {
        if (robo.conectado) {
            arquivo << "  - Robô " << robo.id 
                    << " -> X: " << robo.posicao.x 
                    << ", Y: " << robo.posicao.y 
                    << " | Estado: " << nomeEstadoRobo(robo.estado) << "\n";
        }
    }

    arquivo.close();
}
