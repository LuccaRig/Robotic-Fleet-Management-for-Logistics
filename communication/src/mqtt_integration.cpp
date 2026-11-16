#include "mqtt_integration.hpp"
#include "topics.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <memory>
#include <set>
#include <utility>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <system_error>

#if defined(ENABLE_MQTT) && ENABLE_MQTT
#include <cJSON.h>
#endif

namespace {
bool mesmoPedido(const ComandoOperador& primeiro, const ComandoOperador& segundo) {
    if (primeiro.acao != segundo.acao || primeiro.id_robo != segundo.id_robo || primeiro.geracao != segundo.geracao) return false;
    switch (primeiro.acao) {
        case AcaoOperador::AdicionarCarga:
            return primeiro.carga.id == segundo.carga.id && primeiro.carga.tamanho == segundo.carga.tamanho &&
                primeiro.carga.id_baia == segundo.carga.id_baia && primeiro.carga.id_destino == segundo.carga.id_destino;
        case AcaoOperador::RetomarMissao: return primeiro.id_missao == segundo.id_missao;
        case AcaoOperador::AdicionarRobo: return primeiro.posicao == segundo.posicao;
        case AcaoOperador::DefinirModo: return primeiro.modo == segundo.modo;
        case AcaoOperador::DefinirDestino: return primeiro.destino == segundo.destino;
        case AcaoOperador::DefinirFalha: return primeiro.falha == segundo.falha;
        case AcaoOperador::DefinirBateria: return primeiro.bateria == segundo.bateria;
    }
    return false;
}

std::string escaparJson(const std::string& texto) {
    std::string resultado;
    const char hex[] = "0123456789abcdef";
    for (unsigned char caractere : texto) {
        if (caractere == '"' || caractere == '\\') {
            resultado += '\\';
            resultado += static_cast<char>(caractere);
        } else if (caractere < 0x20) {
            resultado += "\\u00";
            resultado += hex[caractere >> 4];
            resultado += hex[caractere & 0x0f];
        } else {
            resultado += static_cast<char>(caractere);
        }
    }
    return resultado;
}

std::string roboParaJson(const EstadoAtualRobo& robo) {
    std::ostringstream json;
    json.imbue(std::locale::classic());
    json << std::boolalpha
         << "{\"id\":" << robo.id
         << ",\"geracao\":" << robo.geracao
         << ",\"sequencia\":" << robo.sequencia
         << ",\"posicao\":{\"x\":" << robo.posicao.x << ",\"y\":" << robo.posicao.y << "}"
         << ",\"destino\":{\"x\":" << robo.destino.x << ",\"y\":" << robo.destino.y << "}"
         << ",\"estado\":\"" << escaparJson(nomeEstadoRobo(robo.estado)) << "\""
         << ",\"modo\":\"" << nomeModoOperacao(robo.modo) << "\""
         << ",\"fase\":" << static_cast<int>(robo.fase)
         << ",\"bateria\":" << robo.bateria
         << ",\"falha\":" << robo.falha
         << ",\"acoplado\":" << robo.acoplado
         << ",\"conectado\":" << robo.conectado
         << ",\"disponivel\":" << robo.disponivel
         << ",\"id_missao\":" << robo.id_missao
         << ",\"id_carga\":" << robo.id_carga
         << ",\"id_equipe\":" << robo.id_equipe
         << ",\"id_baia\":" << robo.id_baia
         << ",\"passo\":" << robo.passo
         << ",\"sequencia_sensor\":" << robo.sequencia_sensor
         << ",\"revisao\":" << robo.revisao
         << ",\"distancia\":" << robo.distancia
         << ",\"atualizado_em_ms\":" << robo.atualizado_em_ms << "}";
    return json.str();
}
}

IntegracaoMqtt::~IntegracaoMqtt() {
    cliente_.disconnect();
}

void IntegracaoMqtt::iniciar() {
#if defined(ENABLE_MQTT) && ENABLE_MQTT
    const char* host = std::getenv("MQTT_HOST");
    if (host && !*host) throw std::invalid_argument("MQTT_HOST não pode ser vazio");

    int porta = 1883;
    if (const char* valor = std::getenv("MQTT_PORT")) {
        const auto fim = valor + std::strlen(valor);
        const auto resultado = std::from_chars(valor, fim, porta);
        if (resultado.ec != std::errc{} || resultado.ptr != fim || porta < 1 || porta > 65535) {
            throw std::invalid_argument("MQTT_PORT deve ser um número inteiro entre 1 e 65535");
        }
    }
    cliente_.connect(host ? host : "localhost", porta, Topics::commands(),
        [this](std::string payload, bool retido) { enfileirarComando(std::move(payload), retido); });
#endif
}

void IntegracaoMqtt::publicar(const std::vector<EstadoAtualRobo>& frota) {
    if (!cliente_.isConnected()) return;
    try {
        std::ostringstream snapshot;
        snapshot << "{\"robos\":[";
        bool primeiro = true;
        for (const auto& robo : frota) {
            const auto json = roboParaJson(robo);
            if (!primeiro) snapshot << ',';
            primeiro = false;
            snapshot << json;
            cliente_.publish(Topics::robotTelemetry(robo.id), json);
        }
        snapshot << "]}";
        cliente_.publish(Topics::fleetSnapshot(), snapshot.str());
    } catch (const std::exception& erro) {
        std::cerr << "MQTT: falha ao publicar telemetria: " << erro.what() << '\n';
    }
}

void IntegracaoMqtt::parar() {
    for (auto& registro : registros_) {
        if (registro.resposta.empty()) {
            registro.resposta = publicarResultado(registro.comando.id, false, "Aplicação encerrada antes da confirmação");
        }
    }
    cliente_.disconnect();
    std::lock_guard<std::mutex> lock(mutex_fila_);
    fila_.clear();
}


#if defined(ENABLE_MQTT) && ENABLE_MQTT
namespace {
void validarObjeto(const cJSON* objeto) {
    if (!cJSON_IsObject(objeto)) throw std::invalid_argument("Informe um objeto JSON");
    std::set<std::string> encontrados;
    for (const auto* campo = objeto->child; campo; campo = campo->next) {
        if (!campo->string || !encontrados.insert(campo->string).second) {
            throw std::invalid_argument("Campo repetido no comando JSON");
        }
    }
}

std::string lerTexto(const cJSON* objeto, const char* nome) {
    const auto* campo = cJSON_GetObjectItemCaseSensitive(objeto, nome);
    if (!cJSON_IsString(campo) || !campo->valuestring) {
        throw std::invalid_argument(std::string("Informe o campo textual: ") + nome);
    }
    return campo->valuestring;
}

double lerInteiro(const cJSON* objeto, const char* nome, double minimo, double maximo) {
    const auto* campo = cJSON_GetObjectItemCaseSensitive(objeto, nome);
    if (!cJSON_IsNumber(campo) || !std::isfinite(campo->valuedouble) ||
        std::floor(campo->valuedouble) != campo->valuedouble || campo->valuedouble < minimo || campo->valuedouble > maximo) {
        throw std::invalid_argument(std::string("Informe um inteiro válido: ") + nome);
    }
    return campo->valuedouble;
}

Posicao lerPosicao(const cJSON* objeto, const char* nome) {
    const auto* campo = cJSON_GetObjectItemCaseSensitive(objeto, nome);
    validarObjeto(campo);
    return {static_cast<int>(lerInteiro(campo, "x", 0, std::numeric_limits<int>::max())),
            static_cast<int>(lerInteiro(campo, "y", 0, std::numeric_limits<int>::max()))};
}
}
#endif

ComandoOperador decodificarComandoOperador(const std::string& payload, std::string* id_requisicao) {
    if (id_requisicao) id_requisicao->clear();
#if defined(ENABLE_MQTT) && ENABLE_MQTT
    if (payload.empty() || payload.size() > 4096 || payload.find('\0') != std::string::npos || payload.find("\\u0000") != std::string::npos) {
        throw std::invalid_argument("Comando JSON vazio, muito longo ou com caractere nulo");
    }
    std::unique_ptr<cJSON, decltype(&cJSON_Delete)> objeto(
        cJSON_ParseWithLengthOpts(payload.c_str(), payload.size() + 1, nullptr, true), cJSON_Delete);
    if (!objeto) throw std::invalid_argument("Comando JSON inválido");
    validarObjeto(objeto.get());

    ComandoOperador comando;
    comando.id = lerTexto(objeto.get(), "id");
    if (comando.id.empty() || comando.id.size() > 64) throw std::invalid_argument("ID da requisição deve ter de 1 a 64 bytes");
    if (id_requisicao) *id_requisicao = comando.id;
    const auto acao = lerTexto(objeto.get(), "acao");
    if (acao == "adicionar_carga") {
        const auto* carga = cJSON_GetObjectItemCaseSensitive(objeto.get(), "carga");
        validarObjeto(carga);
        comando.acao = AcaoOperador::AdicionarCarga;
        comando.carga.id = static_cast<IdEntidade>(lerInteiro(carga, "id", 1, 9007199254740991.0));
        comando.carga.tamanho = static_cast<int>(lerInteiro(carga, "tamanho", 1, 4));
        comando.carga.id_baia = static_cast<int>(lerInteiro(carga, "id_baia", 0, std::numeric_limits<int>::max()));
        comando.carga.id_destino = static_cast<int>(lerInteiro(carga, "id_destino", 0, std::numeric_limits<int>::max()));
        return comando;
    }
    if (acao == "retomar_missao") {
        comando.acao = AcaoOperador::RetomarMissao;
        comando.id_missao = static_cast<IdEntidade>(lerInteiro(objeto.get(), "id_missao", 1, 9007199254740991.0));
        return comando;
    }
    comando.id_robo = static_cast<int>(lerInteiro(objeto.get(), "id_robo", 0, std::numeric_limits<int>::max()));
    if (acao == "adicionar_robo") {
        comando.acao = AcaoOperador::AdicionarRobo;
        comando.posicao = lerPosicao(objeto.get(), "posicao");
        return comando;
    }
    comando.geracao = static_cast<std::uint64_t>(lerInteiro(objeto.get(), "geracao", 1, 9007199254740991.0));
    if (acao == "definir_modo") {
        const auto modo = lerTexto(objeto.get(), "modo");
        if (modo != "manual" && modo != "automatico") throw std::invalid_argument("Modo deve ser manual ou automatico");
        comando.acao = AcaoOperador::DefinirModo;
        comando.modo = modo == "manual" ? ModoOperacao::MANUAL : ModoOperacao::AUTOMATICO;
    } else if (acao == "definir_destino") {
        comando.acao = AcaoOperador::DefinirDestino;
        comando.destino = lerPosicao(objeto.get(), "destino");
    } else if (acao == "definir_falha") {
        const auto* falha = cJSON_GetObjectItemCaseSensitive(objeto.get(), "falha");
        if (!cJSON_IsBool(falha)) throw std::invalid_argument("Informe o campo booleano: falha");
        comando.acao = AcaoOperador::DefinirFalha;
        comando.falha = cJSON_IsTrue(falha);
    } else if (acao == "definir_bateria") {
        comando.acao = AcaoOperador::DefinirBateria;
        comando.bateria = static_cast<int>(lerInteiro(objeto.get(), "bateria", 0, 100));
    } else {
        throw std::invalid_argument("Ação de operação desconhecida");
    }
    return comando;
#else
    throw std::logic_error("Recebimento de comandos MQTT está desativado");
#endif
}

void IntegracaoMqtt::enfileirarComando(std::string payload, bool retido) {
    if (payload.size() > 4096) throw std::invalid_argument("Comando MQTT excede 4096 bytes");
    std::lock_guard<std::mutex> lock(mutex_fila_);
    if (fila_.size() >= 256) throw std::runtime_error("Fila de comandos MQTT esgotada");
    fila_.push_back({std::move(payload), retido});
}

std::string IntegracaoMqtt::publicarResultado(const std::string& id, bool aceito, const std::string& mensagem) {
    const auto resposta = std::string("{\"id\":") + (id.empty() ? "null" : "\"" + escaparJson(id) + "\"") +
        ",\"aceito\":" + (aceito ? "true" : "false") + ",\"mensagem\":\"" + escaparJson(mensagem) + "\"}";
    cliente_.publish(Topics::commandResults(), resposta, false);
    return resposta;
}

IntegracaoMqtt::Registro* IntegracaoMqtt::buscarRegistro(const std::string& id) {
    const auto registro = std::find_if(registros_.begin(), registros_.end(), [&](const Registro& item) { return item.comando.id == id; });
    return registro == registros_.end() ? nullptr : &*registro;
}

void IntegracaoMqtt::responder(const std::string& id, bool aceito, const std::string& mensagem) {
    const auto resposta = publicarResultado(id, aceito, mensagem);
    if (auto* registro = buscarRegistro(id)) registro->resposta = resposta;
}

void IntegracaoMqtt::processarComandos(const Executor& executar) {
    std::vector<Entrada> entradas;
    {
        std::lock_guard<std::mutex> lock(mutex_fila_);
        entradas.swap(fila_);
    }
    for (const auto& entrada : entradas) {
        ComandoOperador comando;
        std::string id_requisicao;
        try {
            comando = decodificarComandoOperador(entrada.payload, &id_requisicao);
            if (entrada.retido) {
                publicarResultado(comando.id, false, "Comandos retidos não são executados; publique com retain=false");
                continue;
            }
            if (const auto* anterior = buscarRegistro(comando.id)) {
                if (!mesmoPedido(anterior->comando, comando)) {
                    publicarResultado(comando.id, false, "ID da requisição já utilizado por outro conteúdo");
                } else if (!anterior->resposta.empty()) {
                    cliente_.publish(Topics::commandResults(), anterior->resposta, false);
                }
                continue;
            }
            if (registros_.size() >= 1024) {
                const auto antigo = std::find_if(registros_.begin(), registros_.end(), [](const Registro& registro) { return !registro.resposta.empty(); });
                if (antigo == registros_.end()) throw std::runtime_error("Limite de requisições pendentes atingido");
                registros_.erase(antigo);
            }
            registros_.push_back({comando, {}});
            try {
                executar(comando);
            } catch (const std::exception& erro) {
                responder(comando.id, false, erro.what());
            }
        } catch (const std::exception& erro) {
            publicarResultado(id_requisicao, false, erro.what());
        }
    }
}
