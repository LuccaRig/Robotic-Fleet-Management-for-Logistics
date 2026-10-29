#ifndef TOPICS_HPP
#define TOPICS_HPP

#include <string>

namespace Topics {

inline const char* root() { return "fleet/telemetry"; }

inline std::string robotTelemetry(int id) {
    return std::string(root()) + "/robots/" + std::to_string(id);
}

inline const char* fleetSnapshot() { return "fleet/telemetry/fleet"; }

inline const char* events() { return "fleet/events"; }

} // namespace Topics

/*
 *
 * O orquestrador publica telemetria de robos via MQTT nos topicos abaixo.
 * Uma interface visual (web, desktop, etc.) deve se conectar ao mesmo broker
 * mosquitto (localhost:1883 por padrao) e inscrever-se nesses topicos para
 * exibir posicoes e status dos robos no armazem.
 *
 * Topicos disponiveis:
 *
 * 1) fleet/telemetry/robots/<id>
 *    - Publicado para cada robô conectado, a cada ciclo de controle.
 *    - Mensagem retida (retain=true): novos assinantes recebem o ultimo estado
 *      imediatamente, sem esperar o proximo ciclo.
 *    - Payload: objeto JSON com campos do EstadoAtualRobo, por exemplo:
 *        {
 *          "id": 3,
 *          "geracao": 1,
 *          "sequencia": 42,
 *          "posicao": {10, 5},
 *          "destino": {10, 5},
 *          "estado": "Disponível",
 *          "fase": 0,
 *          "bateria": 85,
 *          "falha": false,
 *          "acoplado": false,
 *          "conectado": true,
 *          "disponivel": true,
 *          "id_missao": 0,
 *          "id_carga": 0,
 *          "id_equipe": 0,
 *          "id_baia": -1,
 *          "passo": 0,
 *          "sequencia_sensor": 0,
 *          "distancia": 0,
 *          "atualizado_em_ms": 1730000000000
 *        }
 *    - Uso na interface: inscreva-se em fleet/telemetry/robots/# para receber
 *      todos os robos, ou em fleet/telemetry/robots/<id> para um robo especifico.
 *      Atualize a posicao (x,y) e o estado (cor/icone) do robo no mapa do
 *      armazem sempre que uma mensagem chegar.
 *
 * 2) fleet/telemetry/fleet
 *    - Snapshot completo da frota (todos os robos conectados) em uma unica
 *      mensagem JSON: {"robos":[...]}.
 *    - Util para inicializacao da interface: carregue este topico uma
 *      vez ao conectar para desenhar todos os robos de uma so vez, depois
 *      acompanhe atualizacoes individuais via topico por robo.
 *
 * 3) fleet/events/<tipo>
 *    - Eventos pontuais (falha, inicio/fim de missao, recarga, etc.)
 *    - Uso opcional na interface: exibir notificacoes ou historico de eventos.
 *
 * Como consumir (exemplo conceitual em qualquer linguagem com cliente MQTT):
 *   client.connect("localhost", 1883)
 *   client.subscribe("fleet/telemetry/robots/#")   // todos os robos
 *   client.subscribe("fleet/telemetry/fleet")      // snapshot inicial
 *   // ao receber mensagem em fleet/telemetry/robots/<id>:
 *   //   parse JSON -> atualizar posicao e status do robo <id> no mapa
 *
 *
 */

#endif // TOPICS_HPP

