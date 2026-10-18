# Gerenciamento de frota robótica para logística

Núcleo da Etapa 1 do trabalho de Automação em Tempo Real da UFMG. Implementação em C++17 para macOS e Linux, com processos independentes por robô, IPC por sockets locais, organização de cargas colaborativas e histórico em SQLite.

## Estado da entrega

O código, os oito cenários e os testes foram preparados. **Build, testes e cenários não foram executados nesta implementação.** A validação funcional e as evidências do relatório permanecem pendentes de execução autorizada.

A inspeção realizada foi por leitura do código e revisão do diff. Isso não comprova compilação, funcionamento ou ausência de condições de corrida em execução.

## Preparação

Requisitos: compilador C++17, CMake 3.15 ou superior, SQLite 3.24 ou superior com headers de desenvolvimento e APIs POSIX. A aplicação usa `posix_spawn`, `socketpair`, `poll`, `fcntl` e `waitpid`.

No Ubuntu, as dependências são `cmake`, `build-essential` e `libsqlite3-dev`. No macOS, é necessário o Command Line Tools e CMake; o SQLite pode ser fornecido pelo sistema ou pelo Homebrew.

Os comandos abaixo são instruções para execução futura. Não foram executados como parte desta entrega.

```sh
cmake -S . -B build -DBUILD_TESTING=OFF
cmake --build build --parallel 2
./build/app/simulador_exec --cenario base
```

O executável resolve seu próprio caminho para iniciar os processos filhos. O diretório de trabalho determina onde os caminhos relativos de banco serão criados. Execute os exemplos a partir da raiz do repositório.

## Operação

```sh
./build/app/simulador_exec --cenario cooperativo
./build/app/simulador_exec --cenario base --interativo
./build/app/simulador_exec --cenario falhas --banco logs/etapa1-falhas.db
./build/app/simulador_exec --ajuda
```

Cenários disponíveis: `base`, `cooperativo`, `disponibilidade`, `passagens`, `rota-equipe`, `falhas`, `recarga` e `processos`.

O modo automático aplica o roteiro do cenário e encerra quando suas condições forem satisfeitas. O prazo máximo de 120 segundos detecta falta de progresso. O modo interativo usa a configuração inicial do cenário e aguarda comandos do operador, sem aplicar o roteiro automático.

| Comando | Efeito |
|---|---|
| `status` | Mostra estado, posição, bateria, distância, conectividade, PID e geração. |
| `cargas` | Mostra estado das cargas, missão e motivo de espera. |
| `passagens` | Mostra capacidade, ocupação e fila das passagens. |
| `robo adicionar ID X Y` | Disponibiliza um novo robô e inicia seu processo. |
| `robo remover ID` | Solicita encerramento e remoção física de robô sem missão e sem carga. |
| `robo interromper ID` | Interrompe o processo e preserva sua ocupação física no adaptador. |
| `bateria ID VALOR` | Injeta uma leitura de bateria entre 0 e 100. |
| `falha ID 1` | Injeta falha e interrompe os movimentos. |
| `falha ID 0` | Limpa a falha; uma missão suspensa exige retomada explícita. |
| `retomar MISSÃO` | Solicita retomada após recuperação e confirmação de suspensão. |
| `ajuda` | Lista comandos. |
| `sair` | Para os produtores, recolhe os processos e drena os eventos. |

O banco padrão é `logs/etapa1.db`, criado quando necessário e ignorado pelo Git. O banco de inspeção versionado não é usado pelos cenários padrão. Não aponte `--banco` para esse arquivo se desejar preservá-lo.

## Verificação preparada

Os testes ficam desativados por padrão, sem download de GoogleTest na configuração normal. Para prepará-los futuramente:

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

O CMake procura GoogleTest instalado e, se necessário, prepara a versão 1.14.0 por FetchContent. Os testes unitários usam lógica por passos; as integrações iniciam processos reais e possuem prazos de execução. A configuração de CI foi preparada para Linux e macOS, sem ter sido acionada nesta implementação.

## Documentação

- [Arquitetura, sincronização e contratos](docs/arquitetura-etapa1.md).
- [Roteiro de demonstração e evidências pendentes](docs/demonstracao-etapa1.md).

A interface gráfica e os clientes MQTT serão conectados na Etapa 2. O adaptador atual representa hardware por leituras e confirmações controladas e não pretende reproduzir a física de um galpão real.
