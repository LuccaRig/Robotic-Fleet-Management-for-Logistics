# Gerenciamento de frota robótica para logística

Trabalho de Automação em Tempo Real da UFMG. O núcleo da Etapa 1 usa C++17, processos independentes por robô, sockets locais para IPC e SQLite para histórico.

## Preparação e execução

Requisitos: compilador C++17, CMake 3.15+, SQLite 3.24+ com headers e APIs POSIX. No Ubuntu, instale `build-essential`, `cmake` e `libsqlite3-dev`. No macOS, use Command Line Tools, CMake e SQLite.

Execute a partir da raiz do repositório:

```sh
cmake -S . -B build -DBUILD_TESTING=OFF
cmake --build build --parallel 2
./build/app/simulador_exec --cenario base
./build/app/simulador_exec --cenario cooperativo --interativo
```

Cenários: `base`, `cooperativo`, `disponibilidade`, `passagens`, `rota-equipe`, `falhas`, `recarga` e `processos`. O modo automático encerra ao concluir o roteiro, com prazo máximo de 120 segundos. No modo interativo, use `ajuda` para listar comandos e `sair` para encerrar.

O banco padrão é `logs/etapa1.db`. Use `--banco logs/etapa1-demo.db` para outro arquivo. O banco de inspeção versionado é preservado pelas execuções padrão.

## Telemetria MQTT

O MQTT é opcional e vem desativado. Para habilitá-lo, disponibilize os headers e a biblioteca libmosquitto e configure com `-DENABLE_MQTT=ON`. O CMake procura tanto `mosquitto.h` quanto a biblioteca; instalações fora dos diretórios padrão podem ser indicadas por `CMAKE_PREFIX_PATH`.

O broker deve ser executado separadamente. O endereço padrão é `localhost:1883`, configurável pelas variáveis `MQTT_HOST` e `MQTT_PORT` ou pelos argumentos `--mqtt-host` e `--mqtt-port`, que têm precedência. A porta deve ser um inteiro entre 1 e 65535. Quando o MQTT está desativado, essas configurações são ignoradas.

```sh
./build/app/simulador_exec --cenario cooperativo --interativo --mqtt-host localhost --mqtt-port 1883
```

O módulo `IntegracaoMqtt`, em `communication/`, concentra a configuração, a serialização e a publicação da frota. A aplicação central chama `iniciar`, `publicar` e `parar` no seu próprio ciclo. O cliente usa a thread de rede da libmosquitto e callbacks que atualizam somente o estado da conexão, sem modificar os robôs ou a logística. A conexão inicial é assíncrona e as reconexões usam intervalos progressivos de até 10 segundos. Falhas de rede não encerram a simulação. O encerramento solicita a desconexão e aguarda o término da thread antes de destruir o cliente, conforme o [ciclo de vida da libmosquitto](https://mosquitto.org/api/files/mosquitto-h.html#mosquitto_loop_stop).

As publicações mantêm QoS 1 e mensagens retidas:

| Tópico | Conteúdo |
|---|---|
| `fleet/telemetry/fleet` | Snapshot no formato `{"robos":[...]}`, incluindo os estados conhecidos de robôs desconectados. |
| `fleet/telemetry/robots/<id>` | Estado individual com posição, destino, estado operacional, bateria, falha, conexão, missão e demais identificadores. |

As coordenadas usam objetos JSON, como `"posicao":{"x":10,"y":5}` e `"destino":{"x":12,"y":5}`. Os campos booleanos usam `true` e `false`; `revisao` identifica a revisão do comando. O mesmo estado serializado alimenta os tópicos individual e de frota.

Um robô desconectado continua sendo publicado com `conectado=false` e `disponivel=false`, substituindo seu estado retido anterior. A interface deve usar o snapshot de frota como referência dos IDs conhecidos na execução atual, pois tópicos individuais de execuções anteriores podem permanecer retidos no broker. A publicação continua a cada ciclo enquanto há conexão; após reconectar, o próximo ciclo envia o estado atual. No encerramento normal, a aplicação tenta publicar um último snapshot antes de desconectar. Não há garantia de entrega desse último snapshot se a rede estiver indisponível ou se o processo for interrompido abruptamente.

A integração atual publica telemetria. Assinaturas MQTT, comandos remotos, eventos e a ligação entre setpoints e sensores de um simulador externo ainda precisam ser implementados.

## Testes

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

Os testes são opcionais; quando ativados, o CMake procura GoogleTest instalado ou obtém a versão 1.14.0. A suíte contém 48 casos: 39 unitários e 9 de integração. O CI está configurado somente para Ubuntu.

Consulte a [arquitetura da Etapa 1](docs/arquitetura-etapa1.md) para funcionamento, sincronização, evidências e pendências.
