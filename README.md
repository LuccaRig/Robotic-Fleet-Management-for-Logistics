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

## Tela de operação e simulação

A aplicação web local, isolada em `visualization/`, reúne mapa interativo, frota, controle manual, intervenções na simulação, logística e consulta de histórico. Usa o mapa compartilhado, os comandos MQTT existentes e leitura do SQLite. O navegador não implementa regras de navegação ou alocação.

Com Python, Mosquitto e o executável compilado com MQTT ativado, a demo pode ser iniciada por um único comando:

```sh
visualization/.venv/bin/python visualization/server.py
```

Abra `http://127.0.0.1:8080`. Preparação, opções e funcionamento estão no [guia da tela de operação](visualization/README.md). A extensão de telemetria usada pela tela precisa ser recompilada antes da primeira execução. A interface foi validada no navegador em desktop e celular, com builds e suítes completas nas configurações MQTT ativado e desativado.

## Mapa estático compartilhado

O cenário usa `config/galpao.txt`; `passagens` usa `config/galpao-passagens.txt`. Esses arquivos substituem o layout fixo no código e são compartilhados com a tela de operação. Dimensões, obstáculos, baias, destinos, carregadores e capacidades não são publicados por MQTT.

Use `--mapa caminho/galpao.txt` para escolher outro arquivo compatível com as baias, destinos e posições iniciais do cenário. Sem essa opção, a aplicação procura a configuração no diretório atual e depois junto ao diretório de build, onde o CMake copia `config/` durante a configuração.

A primeira linha contém `largura altura`. As seguintes contêm um elemento por linha, com coordenadas inteiras a partir de zero:

| Elemento | Formato |
|---|---|
| Obstáculo retangular, limites inclusivos | `parede x0 y0 x1 y1` |
| Baia de coleta | `baia id x y` |
| Destino de entrega | `destino id x y` |
| Estação de recarga | `carregador x y` |
| Passagem restrita | `passagem id capacidade quantidade_de_celulas x1 y1 x2 y2 ...` |

Declare as paredes antes das passagens, que abrem suas células nos obstáculos. As capacidades vão de um a quatro robôs. Erros de elementos informam o arquivo e a linha. `mapa_execucao.txt` continua sendo uma exportação da execução, incluindo robôs, e não serve como configuração estática.

## Telemetria MQTT

O MQTT é opcional e vem desativado. Para habilitá-lo, disponibilize os headers e as bibliotecas libmosquitto e cJSON e configure com `-DENABLE_MQTT=ON`. O CMake procura as duas dependências; instalações fora dos diretórios padrão podem ser indicadas por `CMAKE_PREFIX_PATH`. A cJSON é usada para validar os comandos recebidos e não é necessária com MQTT desativado.

O broker deve ser executado separadamente. O endereço padrão é `localhost:1883`, configurável pelas variáveis `MQTT_HOST` e `MQTT_PORT` ou pelos argumentos `--mqtt-host` e `--mqtt-port`, que têm precedência. A porta deve ser um inteiro entre 1 e 65535. Quando o MQTT está desativado, essas configurações são ignoradas.

```sh
./build/app/simulador_exec --cenario cooperativo --interativo --mqtt-host localhost --mqtt-port 1883
```

O módulo `IntegracaoMqtt`, em `communication/`, concentra a configuração, a serialização, a publicação da frota e a recepção de comandos. O cliente usa a thread de rede da libmosquitto: seus callbacks acompanham a conexão e enfileiram mensagens, sem modificar os robôs ou a logística. O ciclo central valida e executa os pedidos. A conexão inicial é assíncrona e as reconexões usam intervalos progressivos de até 10 segundos, com renovação da assinatura de comandos. A publicação da telemetria depende da conexão com o broker; recusas da assinatura de comandos são registradas sem interromper a telemetria. Falhas de rede não encerram a simulação. O encerramento solicita a desconexão e aguarda o término da thread antes de destruir o cliente, conforme o [ciclo de vida da libmosquitto](https://mosquitto.org/api/files/mosquitto-h.html#mosquitto_loop_stop).

As publicações mantêm QoS 1 e mensagens retidas:

| Tópico | Conteúdo |
|---|---|
| `fleet/telemetry/fleet` | Snapshot com `versao:2`, `robos`, `cargas`, `missoes` e ocupação/fila de `passagens`, incluindo robôs desconectados. |
| `fleet/telemetry/robots/<id>` | Estado individual com posição, destino, estado operacional, bateria, falha, conexão, missão e demais identificadores. |

As coordenadas usam objetos JSON, como `"posicao":{"x":10,"y":5}` e `"destino":{"x":12,"y":5}`. Os campos booleanos usam `true` e `false`; `revisao` identifica a revisão do comando. O campo `modo` contém `manual` ou `automatico`. O mesmo estado serializado alimenta os tópicos individual e de frota.

Um robô desconectado continua sendo publicado com `conectado=false` e `disponivel=false`, substituindo seu estado retido anterior. A interface deve usar o snapshot de frota como referência dos IDs conhecidos na execução atual, pois tópicos individuais de execuções anteriores podem permanecer retidos no broker. A publicação continua a cada ciclo enquanto há conexão; após reconectar, o próximo ciclo envia o estado atual. No encerramento normal, a aplicação tenta publicar um último snapshot antes de desconectar. Não há garantia de entrega desse último snapshot se a rede estiver indisponível ou se o processo for interrompido abruptamente.

A integração publica telemetria e recebe comandos de modo, destino manual, falha, bateria, inclusão de cargas e robôs e retomada de missões. Eventos remotos e a ligação entre setpoints e sensores de um simulador externo ainda precisam ser implementados.

## Comandos de operação MQTT

Execute com `--interativo` para manter a aplicação recebendo comandos. O cenário `processos` inicia um robô sem cargas, permitindo experimentar o modo manual antes de uma alocação automática. Os pedidos usam `fleet/commands`, QoS 1 e `retain=false`. As respostas usam `fleet/commands/results`, também com QoS 1 e sem retenção.

Cada pedido exige `id` e `acao`. O ID da requisição deve ser um texto não vazio com até 64 bytes. Operações sobre robôs existentes também exigem `id_robo` e `geracao`, que deve corresponder à telemetria atual. Inclusão de robô, inclusão de carga e retomada usam os identificadores próprios descritos abaixo. Os valores dos exemplos são ilustrativos. Campos adicionais são ignorados. Campos repetidos, tipos incorretos e ausência de campos obrigatórios são recusados.

Assine as respostas antes de enviar pedidos:

```sh
mosquitto_sub -h localhost -p 1883 -q 1 -t fleet/commands/results
```

Coloque o robô em modo manual:

```sh
mosquitto_pub -h localhost -p 1883 -q 1 -t fleet/commands -m '{"id":"modo-1","acao":"definir_modo","id_robo":1,"geracao":1,"modo":"manual"}'
```

Aguarde a confirmação e envie um destino por coordenadas:

```sh
mosquitto_pub -h localhost -p 1883 -q 1 -t fleet/commands -m '{"id":"destino-1","acao":"definir_destino","id_robo":1,"geracao":1,"destino":{"x":8,"y":5}}'
```

Após a chegada, devolva o robô ao modo automático:

```sh
mosquitto_pub -h localhost -p 1883 -q 1 -t fleet/commands -m '{"id":"modo-2","acao":"definir_modo","id_robo":1,"geracao":1,"modo":"automatico"}'
```

Uma resposta positiva tem o formato `{"id":"modo-1","aceito":true,"mensagem":"Comando confirmado pelo robô"}`. Para modo e destino, ela confirma a aceitação pelo processo do robô; a chegada ao destino é acompanhada pela telemetria. Recusas usam `aceito=false` e explicam o motivo. Pedidos inválidos preservam o ID quando ele pode ser lido e validado; caso contrário, a resposta usa `id=null`.

As demais operações usam o mesmo tópico e formato de resposta:

```json
{"id":"falha-1","acao":"definir_falha","id_robo":1,"geracao":1,"falha":true}
{"id":"recuperar-1","acao":"definir_falha","id_robo":1,"geracao":1,"falha":false}
{"id":"bateria-1","acao":"definir_bateria","id_robo":1,"geracao":1,"bateria":10}
{"id":"carga-1","acao":"adicionar_carga","carga":{"id":2,"tamanho":3,"id_baia":1,"id_destino":2}}
{"id":"robo-2","acao":"adicionar_robo","id_robo":2,"posicao":{"x":3,"y":5}}
{"id":"retomada-1","acao":"retomar_missao","id_missao":1}
```

- `definir_falha` injeta ou remove defeito, inclusive durante uma missão. `definir_bateria` aceita inteiros de 0 a 100. A resposta confirma a atualização dos sensores no simulador local; o estado do processo do robô é acompanhado pela telemetria.
- `adicionar_carga` exige ID positivo e único na execução, tamanho de um a quatro e baia e destino existentes. A carga entra no fim da fila da baia, sem ultrapassar a carga que já aguardava uma equipe. A resposta confirma sua inclusão na logística.
- `adicionar_robo` exige ID não negativo e livre e posição transitável fora de uma passagem. A resposta confirma a criação do processo; aguarde `conectado=true` no snapshot da frota para confirmar prontidão e obter sua geração.
- `retomar_missao` exige missão suspensa e todos os integrantes conectados, da geração original, sem falha, com bateria acima de 15 e estado de suspensão confirmado. A equipe precisa estar na mesma posição lógica e, para transportar, estar completamente acoplada e ter rota compatível. A resposta confirma a autorização pela logística; as confirmações e o avanço da equipe seguem por IPC e telemetria.

IDs de cargas, missões e gerações recebidos em JSON ficam limitados a `9007199254740991`, para preservar a representação inteira do parser. A recuperação de falha e a reposição de bateria não retomam uma missão suspensa automaticamente.

As regras de operação são:

- Uma troca efetiva de modo ou um novo destino exige robô parado, sem falha, desacoplado e fora de missão de carga ou recarga. Durante um comando pendente, outro pedido para o mesmo robô é recusado. Solicitar o modo já ativo não altera o movimento.
- Robôs manuais não recebem cargas automaticamente. Ao chegar, continuam no modo manual, com `disponivel=false`. Voltar ao modo automático os disponibiliza novamente quando estiverem aptos.
- O destino deve estar no mapa e ter rota acessível. A navegação usa o mesmo planejador, as permissões de passagem e as confirmações de sensores do modo automático.
- A bateria deve permitir alcançar o destino e preservar acesso a um carregador com margem de segurança. Um destino em uma estação de recarga inicia a recarga. O encaminhamento automático por bateria baixa permanece ativo também no modo manual.
- Falha ou bateria baixa interrompe o trajeto manual. A recuperação de uma falha não reinicia esse trajeto; é necessário enviar outro destino. A ocupação física de passagens continua preservada até a saída confirmada.
- Reenvie o mesmo ID com os mesmos dados para recuperar a resposta sem repetir a ação. Reutilizar o ID com outros dados é recusado. A deduplicação é mantida em memória para até 1024 pedidos, preservando os pendentes; ela não persiste entre execuções.
- Mensagens entregues como retidas são recusadas. O limite é de 4096 bytes por mensagem e 256 mensagens aguardando processamento. Mensagens acima desses limites são descartadas e registradas no terminal; sem confirmação, o cliente pode reenviar o mesmo pedido.

Trocas de modo e destinos confirmados são registrados no SQLite com timestamp. O protocolo IPC usa a versão 2 para transportar o modo de operação, exigindo que central e processos de robôs utilizem a mesma versão do executável.

## Testes

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

Os testes são opcionais; quando ativados, o CMake procura GoogleTest instalado ou obtém a versão 1.14.0. Os testes de modo manual integram a suíte padrão; os testes de leitura e deduplicação de comandos JSON são incluídos com `ENABLE_MQTT=ON`. O CI está configurado somente para Ubuntu.

Consulte a [arquitetura da Etapa 1](docs/arquitetura-etapa1.md) para funcionamento, sincronização, evidências e pendências.
