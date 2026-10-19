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

## Testes

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

Os testes são opcionais; quando ativados, o CMake procura GoogleTest instalado ou obtém a versão 1.14.0. A validação local no macOS aprovou o build e os 46 casos. O CI está configurado somente para Ubuntu, ainda sem execução registrada.

Consulte a [arquitetura da Etapa 1](docs/arquitetura-etapa1.md) para funcionamento, sincronização, evidências e pendências.
