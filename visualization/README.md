# Operação e simulação

Aplicação web local para acompanhar e operar a frota. O mapa é o mesmo arquivo em `config/` usado pela central. Navegação, filas, acoplamento, bateria e coordenação continuam no núcleo C++.

## Preparação

Requisitos: Python 3.10+, Mosquitto disponível no `PATH` e executável da central compilado com `ENABLE_MQTT=ON`. A dependência Python é o [Eclipse Paho MQTT](https://eclipse.dev/paho/files/paho.mqtt.python/html/client.html), usando a API de callbacks versão 2.

Na raiz do repositório:

```sh
python3 -m venv visualization/.venv
visualization/.venv/bin/python -m pip install -r visualization/requirements.txt
```

A extensão do snapshot MQTT exige recompilar a central antes da primeira execução desta interface. Quando for executar essa etapa:

```sh
cmake -S . -B build-mqtt -DENABLE_MQTT=ON -DBUILD_TESTING=ON
cmake --build build-mqtt --parallel 2
```

## Executar a demo

```sh
visualization/.venv/bin/python visualization/server.py
```

Abra **http://127.0.0.1:8080**. Um único comando inicia um broker local isolado, a central em modo interativo e o servidor web. A aplicação permanece aberta após as entregas, permitindo adicionar novas cargas e robôs. `Ctrl+C` encerra os processos iniciados pelo comando.

O cenário padrão é `cooperativo`, com quatro robôs e três cargas. Para começar sem cargas e explorar o controle manual:

```sh
visualization/.venv/bin/python visualization/server.py --cenario processos
```

Opções:

| Opção | Padrão | Uso |
|---|---|---|
| `--executavel` | `build-mqtt/app/simulador_exec` | Executável compilado com MQTT ativado. |
| `--cenario` | `cooperativo` | Um dos cenários existentes. O roteiro automático de demonstração não é executado; as intervenções são feitas na tela. |
| `--mapa` | Mapa correspondente ao cenário | Arquivo compartilhado de configuração do galpão. |
| `--banco` | `logs/operacao.db` | Banco da operação, que preserva execuções anteriores. A tela consulta a execução mais recente. |
| `--porta` | `8080` | Porta HTTP, acessível somente na máquina local. |
| `--broker` | Mosquitto encontrado no `PATH` | Caminho do executável Mosquitto. |

Os caminhos de executável, mapa e banco são resolvidos a partir da raiz do repositório. O broker usa uma porta TCP disponível, com retenção e sessões restritas àquela demonstração. Os logs da central e do broker ficam em `logs/operacao-central.log` e `logs/operacao-mqtt.log`.

## Operação

- **Mapa:** seleção de robôs e cargas, zoom, enquadramento, arraste e alternância de rótulos e cargas. As passagens mostram ocupação/capacidade e informam a fila ao passar o ponteiro. Robôs na mesma posição lógica são distribuídos visualmente ao redor da célula.
- **Frota:** lista de robôs na lateral esquerda, com seleção, status e bateria de cada integrante. Os controles e os demais dados do robô selecionado ficam na coluna imediatamente à direita da lista. As duas colunas têm rolagem independente. A seleção também pode ser feita pelo mapa. A distância é apresentada em células, sem pressupor uma escala em metros.
- **Controle manual:** altere o modo com o robô livre, escolha um local conhecido ou informe coordenadas. O botão “No mapa” permite selecionar a célula; o envio exige uma ação explícita no controle. O marcador de destino não é uma rota planejada.
- **Simulação:** gere/remova falhas, ajuste a bateria, adicione robôs e cargas e solicite retomadas. A central valida cada pedido e preserva suas regras de segurança e sincronização.
- **Métricas:** uma faixa compacta mostra cargas em operação e na fila, entregas, distância percorrida e alertas. Os valores acompanham o estado da simulação.
- **Cargas:** botão na tela principal que abre um diálogo com todas as cargas, estados, motivos de espera, origem, destino e integrantes. Os filtros separam fila, operação, suspensão e entrega. “Localizar” fecha o diálogo e destaca a carga no mapa. Não há uma página separada de Logística.
- **Histórico:** botão na tela principal que abre um diálogo com eventos por robô e rastreabilidade de transportes, incluindo participantes e horários de coleta e entrega. Há filtros e paginação de 50 registros; horários usam Brasília. A consulta usa SQLite somente para leitura.

O estado é recebido por MQTT e consultado pelo navegador a cada 400 ms, sem requisições sobrepostas. A frequência cai quando a aba está oculta. Dados sem atualização por três segundos bloqueiam novos comandos, preservando os últimos estados visíveis. Os comandos usam IDs únicos e aguardam a resposta da central. Em caso de resposta ausente ou falha na requisição, “Reenviar” reutiliza o mesmo ID, aproveitando a deduplicação existente.

## Organização

```text
visualization/
  server.py          Inicialização, ponte HTTP/MQTT e consulta do histórico
  requirements.txt   Uma dependência Python: paho-mqtt
  web/
    index.html       Estrutura e controles acessíveis
    styles.css       Identidade visual e adaptação de tamanho
    app.js           Estado da interface, comandos e histórico
    map.js           Leitura do mapa e desenho SVG
    ui.js            Formatação, ícones e avisos
    favicon.svg      Marca local
```

O navegador não calcula rotas, não aloca equipes e não simula estados. Não há framework, bundler, CDN ou dependência de rede para os recursos visuais. Os ícones e o mapa são SVG, e as fontes usam as disponíveis no sistema. A interface respeita a preferência por movimento reduzido e oferece navegação por teclado, foco visível, rótulos e mensagens de confirmação.

A ponte disponibiliza `GET /api/config`, `GET /api/state`, `POST /api/commands` e `GET /api/history`. Os comandos são encaminhados ao tópico já existente `fleet/commands`; respostas vêm de `fleet/commands/results`. O snapshot `fleet/telemetry/fleet` preserva `robos` e acrescenta `versao`, `cargas`, `missoes` e `passagens`. A geometria permanece no arquivo local, sem publicação por MQTT.

## Validação

A revisão visual atual usa azul, títulos diretos, botões padronizados e métricas compactas. A lista de robôs fica à esquerda, com os dados e controles do selecionado na coluna imediatamente ao lado. O nome anterior, a barra horizontal superior e o rodapé foram removidos. Cargas e histórico abrem em diálogos na tela principal. A revisão foi verificada no navegador: seleção sincronizada entre lista, controles e mapa, abertura e fechamento do histórico, consulta de cargas e localização de uma carga no mapa. Os cenários completos de comandos descritos abaixo foram exercitados na versão anterior do layout.

Em 15/11/2026, os builds e as suítes completas passaram no macOS com MQTT desativado (50 testes unitários e 11 de integração) e ativado (58 unitários e 11 de integração). A dependência Python utilizada foi paho-mqtt 2.2.0.

Após a revisão visual, as mesmas suítes passaram novamente nas duas configurações, sem recompilação. A sintaxe dos módulos JavaScript e do servidor Python também foi verificada.

A validação no navegador cobriu as entregas do cenário cooperativo, modo manual/automático, destinos por coordenadas e por clique no mapa, recusa de destino bloqueado, ajuste de bateria, geração e remoção de falhas, inclusão de robô e cargas, suspensão de equipe em transporte, recusa de retomada com falha presente, recuperação e entrega da missão retomada. Também foram verificados histórico, rastreabilidade, filtros, paginação, zoom, camadas do mapa e layouts de desktop e celular, sem transbordamento horizontal ou erros no console.

Uma pausa temporária do broker confirmou o bloqueio de comandos após dados desatualizados e a recuperação automática após a retomada. Verificações HTTP confirmaram o snapshot completo e a recusa de JSON inválido, ação desconhecida, origem externa e corpo acima do limite.

A validação identificou e corrigiu uma regressão temporária na distância exibida: leituras físicas de sensores agora preservam o total calculado pelo processo do robô. O teste existente de atualização da frota cobre a preservação do total e seu avanço pelos relatos do robô.
