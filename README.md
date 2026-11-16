# Gerenciamento de frota robótica para logística

Trabalho de Automação em Tempo Real da UFMG. O núcleo da Etapa 1 usa C++17, processos independentes por robô, sockets locais para IPC e SQLite para histórico.

## Início rápido: compilar e executar tudo

No Ubuntu ou Debian, instale os pré-requisitos:

```sh
sudo apt update
sudo apt install build-essential cmake libsqlite3-dev libmosquitto-dev libcjson-dev \
  mosquitto python3 python3-venv
```

Na raiz do repositório, prepare o Python e compile com MQTT:

```sh
python3 -m venv visualization/.venv
visualization/.venv/bin/python -m pip install -r visualization/requirements.txt
cmake -S . -B build-mqtt -DENABLE_MQTT=ON
cmake --build build-mqtt --parallel 2
```

Inicie a aplicação:

```sh
./build-mqtt/app/fleet_launcher
```

Abra **http://127.0.0.1:8080** e escolha o cenário; os serviços da simulação iniciam após a confirmação. `Ctrl+C` encerra a aplicação. O lançador ainda depende de Python, Paho MQTT, Mosquitto e dos arquivos do projeto.

## Executar apenas a simulação C++

```sh
cmake -S . -B build -DBUILD_TESTING=OFF
cmake --build build --parallel 2
./build/app/simulador_exec --cenario base
./build/app/simulador_exec --cenario cooperativo --interativo
```

Cenários: `base`, `cooperativo`, `disponibilidade`, `escala`, `estresse-passagens`, `passagens`, `rota-equipe`, `falhas`, `recarga` e `processos`. `escala` usa um mapa de 32x20 com 15 robôs e 8 cargas. Para testar o controle de capacidade e filas das passagens com carga elevada, execute `./build/app/simulador_exec --cenario estresse-passagens`: o mapa 50x30 inclui 100 robôs, 60 cargas com equipes de 1 a 4 integrantes e passagens com capacidades 1, 2, 3 e 4. O teste termina somente após entregar todas as cargas, observar fila e atingir a capacidade de cada passagem.

O modo automático encerra ao concluir as entregas, com prazo máximo de 120 segundos. No modo interativo, use `ajuda` para listar comandos e `sair` para encerrar.

O banco padrão é `logs/etapa1.db`. Use `--banco logs/etapa1-demo.db` para outro arquivo. O banco de inspeção versionado é preservado pelas execuções padrão.

## Tela de operação e simulação

A aplicação web local reúne o mapa, a frota, os controles e o histórico. Para compilar e executar, use o [início rápido](#in%C3%ADcio-r%C3%A1pido-compilar-e-executar-tudo); detalhes de configuração e operação estão no [guia da tela](visualization/README.md).

## Mapa estático compartilhado

Os cenários usam `config/galpao.txt`, `passagens` usa `config/galpao-passagens.txt`, `escala` usa `config/galpao-escala.txt` e `estresse-passagens` usa `config/galpao-estresse-passagens.txt`. Esses arquivos substituem o layout fixo no código e são compartilhados com a tela de operação. Dimensões, obstáculos, baias, destinos, carregadores e capacidades não são publicados por MQTT.

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

O MQTT é opcional para a simulação C++ sem a interface web. A operação integrada pela tela exige MQTT ativado: disponibilize os headers e as bibliotecas libmosquitto e cJSON e configure com `-DENABLE_MQTT=ON`. O CMake procura as duas dependências; instalações fora dos diretórios padrão podem ser indicadas por `CMAKE_PREFIX_PATH`. A cJSON é usada para validar os comandos recebidos e não é necessária com MQTT desativado.

O broker deve ser executado separadamente. O endereço padrão é `localhost:1883`, configurável pelas variáveis `MQTT_HOST` e `MQTT_PORT` ou pelos argumentos `--mqtt-host` e `--mqtt-port`, que têm precedência. A porta deve ser um inteiro entre 1 e 65535. Quando o MQTT está desativado, essas configurações são ignoradas.

```sh
./build/app/simulador_exec --cenario cooperativo --interativo --mqtt-host localhost --mqtt-port 1883
```

O módulo `IntegracaoMqtt`, em `communication/`, concentra a configuração, a serialização, a publicação da frota e a recepção de comandos. O cliente usa a thread de rede da libmosquitto: seus callbacks acompanham a conexão e enfileiram mensagens, sem modificar os robôs ou a logística. O ciclo central valida e executa os pedidos. A conexão inicial é assíncrona e as reconexões usam intervalos progressivos de até 10 segundos, com renovação da assinatura de comandos. A publicação da telemetria depende da conexão com o broker; recusas da assinatura de comandos são registradas sem interromper a telemetria. Falhas no canal de telemetria não encerram a execução com hardware local; a perda de comunicação com o hardware remoto segue o prazo descrito abaixo. O encerramento solicita a desconexão e aguarda o término da thread antes de destruir o cliente, conforme o [ciclo de vida da libmosquitto](https://mosquitto.org/api/files/mosquitto-h.html#mosquitto_loop_stop).

As publicações mantêm QoS 1 e mensagens retidas:

| Tópico | Conteúdo |
|---|---|
| `fleet/telemetry/fleet` | Snapshot com `versao:2`, `robos`, `cargas`, `missoes` e ocupação/fila de `passagens`, incluindo robôs desconectados. |
| `fleet/telemetry/robots/<id>` | Estado individual com posição, destino, estado operacional, bateria, falha, conexão, missão e demais identificadores. |

As coordenadas usam objetos JSON, como `"posicao":{"x":10,"y":5}` e `"destino":{"x":12,"y":5}`. Os campos booleanos usam `true` e `false`; `revisao` identifica a revisão do comando. O campo `modo` contém `manual` ou `automatico`. O mesmo estado serializado alimenta os tópicos individual e de frota.

Um robô desconectado continua sendo publicado com `conectado=false` e `disponivel=false`, substituindo seu estado retido anterior. A interface deve usar o snapshot de frota como referência dos IDs conhecidos na execução atual, pois tópicos individuais de execuções anteriores podem permanecer retidos no broker. A publicação continua a cada ciclo enquanto há conexão; após reconectar, o próximo ciclo envia o estado atual. No encerramento normal, a aplicação tenta publicar um último snapshot antes de desconectar. Não há garantia de entrega desse último snapshot se a rede estiver indisponível ou se o processo for interrompido abruptamente.

A integração publica telemetria e recebe comandos de modo, destino manual, falha, bateria, inclusão de cargas e robôs e retomada de missões. O histórico remoto é consultado pela ponte HTTP, com eventos individuais e eventos das equipes de cada robô.

## Comunicação com o simulador

A tela inicia o hardware simulado em um processo separado, reutilizando `AdaptadorHardwareLocal`. O simulador executa seu próprio ciclo de 20 ms, recebe comandos físicos e publica sensores quando há mudanças e a cada 500 ms. Também anuncia os robôs disponíveis; esses anúncios fazem o Gestor criar os processos com a geração correspondente.

A central usa `--simulador-mqtt` e recebe as mensagens sem bloquear o ciclo de coordenação. Sem essa opção, a execução usa o adaptador local, inclusive com MQTT desativado. Com um broker disponível, os processos podem ser iniciados em terminais distintos:

```sh
./build-mqtt/app/simulador_exec --simulador --cenario cooperativo --mapa config/galpao.txt
./build-mqtt/app/simulador_exec --cenario cooperativo --interativo --simulador-mqtt
```

Ambos aceitam `--mqtt-host` e `--mqtt-port`. Cada broker deve atender uma única dupla de central e simulador; o lançador web já fornece esse isolamento.

| Tópico | Conteúdo |
|---|---|
| `fleet/simulation/commands` | Setpoints, comandos físicos de movimento, acoplamento e parada, intervenções em bateria/falha e disponibilização ou retirada de robôs. |
| `fleet/simulation/events` | Leituras de sensores e anúncios de disponibilidade ou retirada dos robôs. |

O transporte usa QoS 1, mensagens sem retenção e a serialização binária existente. Rotas completas permanecem entre a central e o processo do robô por IPC; o simulador recebe apenas o setpoint e o contexto físico da equipe. Não há pedidos remotos para avançar ciclos, espelhar observações ou chamar funções internas do adaptador.

As confirmações de movimento e acoplamento vêm dos sensores. A central reenvia o último comando físico pendente a cada 500 ms, e geração, revisão e passo impedem repetir movimentos ou consumir bateria novamente. A recarga avança no simulador enquanto o robô está no destino de recarga. O simulador não precisa receber consultas para continuar seu ciclo.

Após três segundos sem atualizações do simulador ou sem confirmação de um comando físico pendente, a central encerra a operação de forma controlada. Comandos JSON de operação aceitam até 16384 bytes para transportar lotes de até 60 cargas; as demais mensagens MQTT mantêm o limite de 4096 bytes. A fila do canal de simulação comporta 4096 mensagens e seu esgotamento é tratado como erro.

O cenário `cooperativo` começa com duas filas: tamanhos 2 e 4 na baia 1, e 1 e 3 na baia 2, usando os dois destinos. O coletor registra os participantes de cada evento coletivo em `robot_event_members`, sem duplicar o evento. Bancos anteriores recuperam esses vínculos a partir dos transportes já registrados.

Builds e testes desta revisão passaram com MQTT ativado e desativado, incluindo broker real e missões consecutivas. A validação encontrou e corrigiu uma corrida entre sensores antigos e o status de uma nova revisão. Resultados e cenários estão no [guia da interface](visualization/README.md#validação). A [conferência com o enunciado](docs/conformidade-enunciado.md) distingue o funcionamento atendido das diferenças de arquitetura e entregas finais pendentes.

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

- `definir_falha` injeta ou remove defeito, inclusive durante uma missão. `definir_bateria` aceita inteiros de 0 a 100. No modo local, a resposta confirma a atualização dos sensores; com simulador MQTT, confirma o encaminhamento da intervenção. O resultado físico é acompanhado pela telemetria.
- `adicionar_carga` exige ID positivo e único na execução, tamanho de um a quatro e baia e destino existentes. A carga entra no fim da fila da baia, sem ultrapassar a carga que já aguardava uma equipe. A resposta confirma sua inclusão na logística.
- `adicionar_robo` exige ID não negativo e livre e posição transitável fora de uma passagem. Com simulador MQTT, a resposta confirma o pedido de disponibilização; o anúncio do simulador inicia a criação do processo. Aguarde `conectado=true` no snapshot da frota para confirmar prontidão e obter sua geração.
- `retomar_missao` exige missão suspensa e todos os integrantes conectados, da geração original, sem falha, com bateria acima de 15 e estado de suspensão confirmado. A equipe precisa estar na mesma posição lógica e, para transportar, estar completamente acoplada e ter rota compatível. A resposta confirma a autorização pela logística; as confirmações e o avanço da equipe seguem por IPC e telemetria.

IDs de cargas, missões e gerações recebidos em JSON ficam limitados a `9007199254740991`, para preservar a representação inteira do parser. A recuperação de falha e a reposição de bateria não retomam uma missão suspensa automaticamente.

As regras de operação são:

- Uma troca efetiva de modo ou um novo destino exige robô parado, sem falha, desacoplado e fora de missão de carga ou recarga. Durante um comando pendente, outro pedido para o mesmo robô é recusado. Solicitar o modo já ativo não altera o movimento.
- Robôs manuais não recebem cargas automaticamente. Ao chegar, continuam no modo manual, com `disponivel=false`. Voltar ao modo automático os disponibiliza novamente quando estiverem aptos.
- O destino deve estar no mapa e ter rota acessível. A navegação usa o mesmo planejador, as permissões de passagem e as confirmações de sensores do modo automático.
- A bateria deve permitir alcançar o destino e preservar acesso a um carregador com margem de segurança. Um destino em uma estação de recarga inicia a recarga. O encaminhamento automático por bateria baixa permanece ativo também no modo manual.
- Falha ou bateria baixa interrompe o trajeto manual. A recuperação de uma falha não reinicia esse trajeto; é necessário enviar outro destino. A ocupação física de passagens continua preservada até a saída confirmada.
- Reenvie o mesmo ID com os mesmos dados para recuperar a resposta sem repetir a ação. Reutilizar o ID com outros dados é recusado. A deduplicação é mantida em memória para até 1024 pedidos, preservando os pendentes; ela não persiste entre execuções.
- Mensagens entregues como retidas são recusadas. Comandos de operação aceitam até 16384 bytes e a fila comporta 256 mensagens aguardando processamento. Mensagens acima desses limites são descartadas e registradas no terminal; sem confirmação, o cliente pode reenviar o mesmo pedido.

Trocas de modo e destinos confirmados são registrados no SQLite com timestamp. O protocolo IPC usa a versão 2 para transportar o modo de operação, exigindo que central e processos de robôs utilizem a mesma versão do executável.

## Testes

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

Os testes são opcionais; quando ativados, o CMake procura GoogleTest instalado ou obtém a versão 1.14.0. Os testes de modo manual integram a suíte padrão; os testes de leitura e deduplicação de comandos JSON são incluídos com `ENABLE_MQTT=ON`. O CI está configurado somente para Ubuntu.

Consulte a [arquitetura da Etapa 1](docs/arquitetura-etapa1.md) para funcionamento, sincronização, evidências e pendências.
