#include "runtime.hpp"
#include <csignal>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <unistd.h>

AplicacaoCentral::AplicacaoCentral(std::string executavel, std::string banco, Cenario cenario)
    : galpao_(std::move(cenario.galpao)), initial_(std::move(cenario.robos)), collector_(banco),
      recorder_(collector_, [this](const EventoRobo& event) { ++metrics_[event.tipo()]; imprimir("Evento: " + event.descricao() + " | robô " + std::to_string(event.idRobo()) + " | missão " + std::to_string(event.idMissao())); }),
      manager_(std::move(executavel)),
      passages_(galpao_),
      hardware_(galpao_),
      logistics_(galpao_, std::move(cenario.cargos)) {
    galpao_.validar();
}
AplicacaoCentral::~AplicacaoCentral() { try { parar(); } catch (const std::exception& e) { imprimir("Falha no encerramento: " + std::string(e.what())); } }
void AplicacaoCentral::iniciar() {
    if (started_) return;
    collector_.iniciar(); started_ = true;
    for (const auto& robo : initial_) if (!adicionarRobo(robo.id, robo.posicao)) throw std::runtime_error("Não foi possível disponibilizar o robô " + std::to_string(robo.id));
    imprimir("Sistema iniciado. Execução " + std::to_string(collector_.idExecucao()));
}
void AplicacaoCentral::parar() {
    if (stopped_) return;
    stopped_ = true;
    std::exception_ptr error;
    try {
        for (const auto& mensagem : manager_.parar()) aoReceberMensagem(mensagem);
    } catch (...) { error = std::current_exception(); }
    try { collector_.parar(); } catch (...) { if (!error) error = std::current_exception(); }
    if (error) std::rethrow_exception(error);
}
bool AplicacaoCentral::sendCommand(int id, Mensagem mensagem) {
    const auto robo = manager_.frota().obter(id); if (!robo || !robo->conectado) return false;
    mensagem.id_robo = id; mensagem.geracao = robo->geracao; mensagem.id_comando = ++commands_; mensagem.revisao = mensagem.id_comando;
    pending_[mensagem.id_comando] = mensagem;
    try {
        if (manager_.enviar(id, mensagem)) return true;
    } catch (...) { pending_.erase(mensagem.id_comando); throw; }
    pending_.erase(mensagem.id_comando); return false;
}
void AplicacaoCentral::aoReceberLeitura(Mensagem leitura_sensores) {
    manager_.atualizarLeituraSensores(leitura_sensores);
    tratarEfeitosPassagem(passages_.confirmar(leitura_sensores));
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
        hardware_.registrarRobo(estadoRoboDaMensagem(m));
        tratarEfeitosPassagem(passages_.confirmar(m));
        imprimir("Robô " + std::to_string(m.id_robo) + " pronto, geração " + std::to_string(m.geracao));
    }
    if (m.tipo == TipoMensagem::ConfirmacaoComando) {
        std::optional<Mensagem> original;
        auto it = pending_.find(m.id_comando);
        if (it != pending_.end()) {
            if (it->second.id_robo == m.id_robo && it->second.geracao == m.geracao) original = it->second;
            pending_.erase(it);
        }
        if (original && m.aceito) {
            tratarLeiturasHardware(hardware_.confirmarComandoAceito(*original));
            if (original->tipo == TipoMensagem::CancelarMissao || original->tipo == TipoMensagem::PausarMissao) {
                tratarEfeitosPassagem(passages_.cancelar(m.id_robo, original->id_missao));
            }
        }
    } else if (m.tipo == TipoMensagem::SolicitarAcessoTrecho) {
        const auto lerLeitura = hardware_.lerLeitura(m.id_robo);
        if (lerLeitura && lerLeitura->geracao == m.geracao && lerLeitura->id_missao == m.id_missao && lerLeitura->revisao == m.revisao && !lerLeitura->falha && lerLeitura->posicao == m.posicao) {
            tratarEfeitosPassagem(passages_.solicitarAcesso(m));
        }
    } else if (m.tipo == TipoMensagem::DestinoNavegacao) {
        if (!passages_.permiteMovimento(m)) throw std::runtime_error("Movimento sem permissão de passagem");
        tratarLeiturasHardware(hardware_.setpoint(m));
    }
    else if (m.tipo == TipoMensagem::EstadoAtualRobo) hardware_.registrarObservacao(estadoRoboDaMensagem(m));
    else if (m.tipo == TipoMensagem::RoboDesconectado) {
        hardware_.desconectar(m.id_robo);
        const auto lerLeitura = hardware_.lerLeitura(m.id_robo);
        tratarEfeitosPassagem(passages_.cancelar(m.id_robo, lerLeitura ? lerLeitura->id_missao : 0));
        for (auto it = pending_.begin(); it != pending_.end();) { if (it->second.id_robo == m.id_robo) it = pending_.erase(it); else ++it; }
    }
    recorder_.aoReceberEventoRobo(m);
    if (!stopped_) eventos_logistica_.push_back(m);
}
void AplicacaoCentral::avancarCiclo() {
    for (const auto& mensagem : manager_.processarCiclo()) aoReceberMensagem(mensagem);
    collector_.verificarFalha(); tratarLeiturasHardware(hardware_.avancarCiclo());
    const auto agora = std::chrono::steady_clock::now();
    if (!eventos_logistica_.empty() || agora - ultimo_ciclo_logistica_ >= std::chrono::milliseconds(200)) {
        auto resultado = logistics_.processarCiclo(eventos_logistica_, manager_.frota().capturarEstado());
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
        if (!hardware_.removerRobo(*it)) throw std::runtime_error("Remoção física recusada");
        auto resultado = passages_.removerRobo(*it);
        if (!resultado.aceito) throw std::runtime_error("Remoção física recusada");
        tratarEfeitosPassagem(std::move(resultado));
        it = removals_.erase(it);
    }
}
bool AplicacaoCentral::adicionarRobo(int id, Posicao posicao) {
    if (hardware_.contemRobo(id) || !galpao_.podeTransitar(posicao) || galpao_.trechoEm(posicao)) return false;
    return manager_.iniciarProcessoRobo(id, posicao.x, posicao.y);
}
bool AplicacaoCentral::removerRobo(int id) {
    const auto robo = manager_.frota().obter(id);
    if (!robo || robo->acoplado || robo->id_missao || removals_.count(id)) return false;
    removals_.insert(id); if (manager_.existeProcessoRobo(id)) manager_.encerrarProcessoRobo(id); return true;
}
bool AplicacaoCentral::interromperRobo(int id) { for (const auto& p : manager_.processos()) if (p.id == id) return kill(p.pid, SIGTERM) == 0; return false; }
bool AplicacaoCentral::definirBateria(int id, int bateria) {
    auto leituras = hardware_.injetarLeitura(id, bateria, std::nullopt);
    if (!leituras) return false;
    tratarLeiturasHardware(std::move(*leituras));
    return true;
}
bool AplicacaoCentral::definirFalha(int id, bool falha) {
    auto leituras = hardware_.injetarLeitura(id, std::nullopt, falha);
    if (!leituras) return false;
    tratarLeiturasHardware(std::move(*leituras));
    return true;
}
void AplicacaoCentral::retomar(IdEntidade id_missao) {
    Mensagem mensagem;
    mensagem.tipo = TipoMensagem::RetomarMissao;
    mensagem.id_missao = id_missao;
    eventos_logistica_.push_back(std::move(mensagem));
}
bool AplicacaoCentral::concluido() const {
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
        for (const auto& s : frota().capturarEstado()) imprimir("Robô " + std::to_string(s.id) + " | " + nomeEstadoRobo(s.estado) + " | posição " + std::to_string(s.posicao.x) + "," + std::to_string(s.posicao.y) + " | bateria " + std::to_string(s.bateria) + "% | distância " + std::to_string(s.distancia) + " | " + (s.conectado ? "Conectado" : "Desconectado"));
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
