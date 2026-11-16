# Operação e simulação

Aplicação web local para acompanhar e operar a frota. O mapa é o mesmo arquivo em `config/` usado pela central. Navegação, filas, acoplamento, bateria e coordenação continuam no núcleo C++.

## Preparação

Requisitos: compilador C++17, CMake 3.15+, dependências C++ de MQTT (libmosquitto e cJSON), Python 3.10+, Mosquitto disponível no `PATH` e os arquivos do projeto no local usado durante a configuração do CMake. A dependência Python é o [Eclipse Paho MQTT](https://eclipse.dev/paho/files/paho.mqtt.python/html/client.html), usando a API de callbacks versão 2.

Na raiz do repositório, prepare o ambiente Python e compile o lançador e os executáveis da simulação com MQTT ativado:

```sh
python3 -m venv visualization/.venv
visualization/.venv/bin/python -m pip install -r visualization/requirements.txt
cmake -S . -B build-mqtt -DENABLE_MQTT=ON
cmake --build build-mqtt --parallel 2
```

## Executar a demo

```sh
./build-mqtt/app/fleet_launcher
```

O executável C++ inicia o servidor Python sem exigir flags. O lançador usa `visualization/.venv/bin/python` quando disponível e, caso contrário, procura `python3` no `PATH`. O Python continua sendo necessário e precisa ter a dependência Paho instalada; o executável também depende de Mosquitto, dos arquivos de visualização/configuração e do executável de simulação produzido no build. O lançador encaminha `Ctrl+C` ao servidor Python.

Abra **http://127.0.0.1:8080** e escolha um cenário na página inicial. Depois da confirmação, o servidor inicia um broker local isolado, o processo de simulação do hardware e a central em modo interativo; a tela muda automaticamente para a operação. O simulador executa seu próprio ciclo e publica sensores e anúncios de robôs; a central envia os comandos físicos por MQTT. A aplicação permanece aberta após as entregas, permitindo adicionar novas cargas e robôs. `Ctrl+C` encerra os serviços iniciados pelo servidor.

Para iniciar diretamente sem a página de escolha, passe `--cenario` ao servidor. `escala` usa 15 robôs e oito cargas. Para testar o limite das passagens com carga alta, escolha `estresse-passagens` na página inicial ou rode `visualization/.venv/bin/python visualization/server.py --cenario estresse-passagens`; o cenário usa 100 robôs, 60 cargas com equipes de 1 a 4 integrantes e quatro passagens de capacidade 1, 2, 3 e 4. Todas as entregas cruzam a divisória central pelas passagens, permitindo observar fila, ocupação e sentido no mapa. Para começar sem cargas e explorar o controle manual:

```sh
visualization/.venv/bin/python visualization/server.py --cenario processos
```

Opções:

| Opção | Padrão | Uso |
|---|---|---|
| `--executavel` | `build-mqtt/app/simulador_exec` | Executável compilado com MQTT ativado. O lançador C++ fornece automaticamente o caminho do build atual. |
| `--cenario` | Escolha no navegador | Inicia diretamente o cenário informado, sem mostrar a página de escolha. Inclui `escala` (15 robôs) e `estresse-passagens` (100 robôs, 60 cargas e quatro capacidades de passagem). O roteiro automático de demonstração não é executado; as intervenções são feitas na tela. |
| `--mapa` | Mapa correspondente ao cenário | Arquivo compartilhado de configuração do galpão. |
| `--banco` | `logs/operacao.db` | Banco da operação, que preserva execuções anteriores. A tela consulta a execução mais recente. |
| `--porta` | `8080` | Porta HTTP, acessível somente na máquina local. |
| `--broker` | Mosquitto encontrado no `PATH` | Caminho do executável Mosquitto. |

Os caminhos de executável, mapa e banco são resolvidos a partir da raiz do repositório. O broker usa uma porta TCP disponível, com retenção e sessões restritas àquela demonstração. Os logs ficam em `logs/operacao-central.log`, `logs/operacao-simulador.log` e `logs/operacao-mqtt.log`.

## Operação

- **Mapa:** seleção de robôs e cargas, zoom, enquadramento, arraste e alternância de rótulos e cargas. As passagens mostram ocupação/capacidade e informam a fila ao passar o ponteiro. Robôs na mesma posição lógica são distribuídos visualmente ao redor da célula.
- **Frota:** lista de robôs na lateral esquerda, com seleção, status e bateria de cada integrante. Os controles e os demais dados do robô selecionado ficam na coluna imediatamente à direita da lista. As duas colunas têm rolagem independente. A seleção também pode ser feita pelo mapa. A distância é apresentada em células, sem pressupor uma escala em metros.
- **Controle manual:** altere o modo com o robô livre, escolha um local conhecido ou informe coordenadas. O botão “No mapa” permite selecionar a célula; o envio exige uma ação explícita no controle. O marcador de destino não é uma rota planejada.
- **Simulação:** gere/remova falhas, ajuste a bateria, adicione robôs e cargas e solicite retomadas. A central valida cada pedido e preserva suas regras de segurança e sincronização.
- **Lotes de cargas:** em **Adicionar carga**, escolha **Adicionar lote de cargas** e informe de 1 a 60. O sistema sorteia equipes de 1–4 robôs e baias/destinos com rota compatível com a capacidade das passagens. O lote é validado e registrado atomicamente sob o mutex da logística, antes de o agendador poder selecionar cargas; se houver erro, nenhuma carga do lote é adicionada.
- **Métricas:** uma faixa compacta mostra cargas em operação e na fila, entregas, distância percorrida e alertas. Os valores acompanham o estado da simulação.
- **Cargas:** botão na tela principal que abre um diálogo com todas as cargas, estados, motivos de espera, origem, destino e integrantes. Os filtros separam fila, operação, suspensão e entrega. “Localizar” fecha o diálogo e destaca a carga no mapa. Não há uma página separada de Logística.
- **Histórico:** botão na tela principal que abre um diálogo com eventos por robô e rastreabilidade de transportes, incluindo participantes e horários de coleta e entrega. O filtro de robô também inclui eventos coletivos das equipes das quais ele participou, inclusive antes da coleta. Eventos coletivos aparecem como “Equipe”, sem duplicar os registros da frota. Há filtros e paginação de 50 registros; horários usam Brasília. A consulta usa SQLite somente para leitura.

O estado é recebido por MQTT e consultado pelo navegador a cada 400 ms, sem requisições sobrepostas. A frequência cai quando a aba está oculta. Dados sem atualização por três segundos bloqueiam novos comandos, preservando os últimos estados visíveis. Os comandos usam IDs únicos e aguardam a resposta da central. Em caso de resposta ausente ou falha na requisição, “Reenviar” reutiliza o mesmo ID, aproveitando a deduplicação existente.

## Organização

```text
visualization/
  server.py          Inicialização, ponte HTTP/MQTT e consulta do histórico
  test_scenario_picker.py  Testes HTTP da escolha e inicialização de cenários
  requirements.txt   Uma dependência Python: paho-mqtt
  web/
    scenario-picker.html  Página inicial para escolher o cenário
    scenario-picker.js    Seleção e transição para a operação
    scenario-picker.css   Estilos da página inicial
    index.html       Estrutura e controles acessíveis
    styles.css       Identidade visual e adaptação de tamanho
    app.js           Estado da interface, comandos e histórico
    map.js           Leitura do mapa e desenho SVG
    ui.js            Formatação, ícones e avisos
    favicon.svg      Marca local
```

O navegador não calcula rotas, não aloca equipes e não simula estados. Não há framework, bundler, CDN ou dependência de rede para os recursos visuais. Os ícones e o mapa são SVG, e as fontes usam as disponíveis no sistema. A interface respeita a preferência por movimento reduzido e oferece navegação por teclado, foco visível, rótulos e mensagens de confirmação.

Antes da operação, a página usa `GET /api/scenarios` para listar as opções e `POST /api/start` para iniciar a selecionada; `GET /api/start-status` informa falhas de inicialização. A ponte da operação disponibiliza `GET /api/config`, `GET /api/state`, `POST /api/commands` e `GET /api/history`. Os comandos são encaminhados ao tópico já existente `fleet/commands`; respostas vêm de `fleet/commands/results`. O snapshot `fleet/telemetry/fleet` preserva `robos` e acrescenta `versao`, `cargas`, `missoes` e `passagens`. A geometria permanece no arquivo local, sem publicação por MQTT. O processo `--simulador` reutiliza `AdaptadorHardwareLocal` e avança autonomamente. A central usa `--simulador-mqtt` para enviar comandos físicos e receber sensores e anúncios, sem chamadas remotas de funções internas. O transporte binário reutiliza o protocolo existente, mantendo as confirmações de sensores e o avanço conjunto das equipes.

## Validação

Em 16/11/2026, no macOS, os builds e as suítes completas passaram:

| Configuração | Unitários C++ | Integração C++ | Resultado |
|---|---:|---:|---|
| MQTT desativado | 53 | 11 | 64 aprovados |
| MQTT ativado | 61 | 14 | 75 aprovados |

Os três testes Python de histórico passaram com `visualization/.venv/bin/python -m unittest visualization.test_history -v`. A sintaxe JavaScript da operação foi conferida com `node --check visualization/web/app.js`. Os quatro testes de escolha de cenário passaram com `python3 -m unittest visualization.test_scenario_picker -v`. O fluxo do seletor foi aberto no Chrome, que carregou as opções e mudou para a tela de operação após a seleção.

Os testes com broker real verificaram anúncios de robôs, barreira das equipes, setpoints repetidos, recarga sem consultas da central e duas missões consecutivas com processos reais e sensores remotos. O teste de persistência cobre os participantes antes da coleta e a recuperação dos vínculos de bancos anteriores.

A validação integrada pela ponte HTTP, com o lançador e o simulador separado, confirmou:

- Quatro robôs iniciais anunciados por MQTT e quatro entregas, respeitando as filas das duas baias.
- Inclusão de um quinto robô, modo manual/automático, destino arbitrário e recusa de destino bloqueado.
- Geração e remoção de falha, redução de bateria, encaminhamento ao carregador e recarga autônoma até 100%.
- Inclusão de nova carga de dois robôs, suspensão por falha durante o transporte, recusa de retomada com falha, recuperação, retomada e entrega.
- Eventos coletivos no histórico de cada participante, timestamps e rastreabilidade da baia, destino e equipe.

No navegador, foram conferidos o mapa e os estados ao vivo, o novo robô e a consulta do histórico com os eventos coletivos identificados como equipe. A disposição visual existente foi preservada.

A interrupção do processo do simulador confirmou o encerramento controlado da central e o bloqueio de novos comandos. O encerramento do lançador recolheu central, simulador, broker e robôs sem processos restantes.

A validação encontrou e corrigiu uma corrida entre sensores da revisão anterior e status de uma nova missão. A frota agora aceita a nova revisão lógica preservando os dados físicos mais recentes. O teste de regressão reproduz a ordem das mensagens, e a demo confirmou a conclusão de missões consecutivas após a correção.

Os testes funcionais não substituem a análise de tempo de resposta e escalabilidade exigida na entrega final. A [conferência com o enunciado](../docs/conformidade-enunciado.md) registra as funcionalidades atendidas, diferenças de arquitetura e entregas pendentes.
