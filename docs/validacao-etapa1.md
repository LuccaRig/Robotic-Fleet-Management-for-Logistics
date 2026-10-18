# Validação funcional da Etapa 1

## Ambiente e revisão

- Data: 18/10/2026.
- Código validado: `66016ea853a5d7ee906ca2ada4c3253e88708b0a`, branch `feat/etapa1-nucleo`.
- Sistema: macOS 14.4.1, arquitetura arm64.
- Compilador: Apple Clang 15.0.0, C++17.
- CMake: 4.4.4.
- SQLite: 3.43.2, fornecido pelo SDK do macOS.
- GoogleTest: 1.14.0, preparado por FetchContent.

## Comandos executados

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

A primeira compilação encontrou duas macros `EXPECT_THROW` na mesma linha, que geravam rótulos internos duplicados. A linha foi separada. Os avisos de política de extração do GoogleTest, alvo SQLite obsoleto e vínculo duplicado também foram corrigidos.

As primeiras integrações identificaram uma corrida na retomada de missão e perda de mensagens finais no encerramento. A logística passou a rejeitar leituras antigas e a exigir sensores atuais antes da retomada. O transporte passou a decodificar dados já disponíveis mesmo com prazo de consulta zero; o gestor drena as mensagens finais antes de fechar o canal ou recolher o processo. Foram acrescentados testes de regressão, e o roteiro de falhas foi corrigido para reconhecer a retomada mesmo após a mudança de fase.

A rodada completa abaixo ocorreu depois dessas correções, com o build mantido estável durante a execução.

## Resultado final

| Verificação | Resultado |
|---|---|
| Build da aplicação e dos testes | Aprovado. |
| Testes unitários | 37 aprovados, nenhuma falha. |
| Testes de integração | 9 aprovados, nenhuma falha. |
| CTest | 2 de 2 grupos aprovados, total de 46 casos GoogleTest. |
| Processos de robô após os testes | Nenhum processo remanescente. |
| Banco de inspeção versionado | Preservado, sem alteração. |

O CTest registrou 3,30 segundos para o grupo unitário e 100,61 segundos para o grupo de integração, com duração total de 103,95 segundos. Esses tempos descrevem a execução da suíte neste ambiente, incluindo sua infraestrutura. Não são medidas de latência de controle nem garantias de escalonamento em tempo real.

## Casos de integração

Tempos reportados pelo GoogleTest para cada caso na rodada final:

| Caso | Tempo | Resultado observado pelo teste |
|---|---|---|
| Encerramento durante operação | 567 ms | Processo com PID próprio encerrado; mensagem final recebida; nenhum filho restante. |
| Base | 5.089 ms | Carga entregue, robô liberado e execução encerrada no SQLite. |
| Cooperativo | 46.232 ms | Cargas de tamanhos 2, 3 e 4 concluídas com participantes registrados. |
| Disponibilidade | 7.396 ms | Espera por equipe completa e avanço após novo robô. |
| Passagens | 9.221 ms | Trechos de capacidades 1, 2 e 3 utilizados com espera observada e sem ultrapassar a capacidade. |
| Rota de equipe | 7.796 ms | Equipe de quatro utiliza os trechos alternativos de capacidade quatro. |
| Falhas | 8.953 ms | Cancelamento antes de acoplar, suspensão durante transporte, recuperação e entrega. |
| Recarga | 14.713 ms | Bateria baixa, recarga completa, retorno e conclusão da carga. |
| Processos | 129 ms | ID duplicado recusado e processo recriado com nova geração após remoção explícita. |

Cada cenário também verifica a ausência de processos filhos ao final, o encerramento da sessão no banco e, quando há cargas, a existência de participantes persistidos.

## Cobertura unitária

Os 37 casos cobrem mapa, BFS, comandos e estados do agente, distância por segmentos, falhas transitórias, bateria, protocolo, fragmentação, EOF, gerações, revisões, barreiras de equipe, reserva de passagens, hardware colaborativo, fila do coletor, concorrência dos produtores, migração e deduplicação.

O teste de falha de escrita instala um trigger SQLite que rejeita a inserção. A mensagem `Coletor de dados: bloqueado` nesse caso é esperada e comprova a propagação do erro provocado; o teste passou.

## Evidências locais

Os arquivos abaixo ficam no diretório de build, ignorado pelo Git:

- `build/evidencias/ctest-final.log`: resultado agregado da rodada final.
- `build/evidencias/testes-finais.log`: saída completa dos 46 casos da rodada final.
- `build/Testing/Temporary/LastTest.log`: log gerado pelo CTest, sujeito a substituição em novas execuções.

Os bancos dos testes são temporários e normalmente removidos ao concluir cada caso. Os resultados persistidos são inspecionados pelos próprios testes antes da remoção. Para produzir bancos permanentes para apresentação, use os comandos com `--banco logs/etapa1-*.db` descritos no roteiro de demonstração.

## Pendências e limites

- Build e testes no Linux ainda não foram executados localmente; o CI está configurado exclusivamente para Ubuntu. Os resultados de macOS acima correspondem à validação local.
- A apresentação interativa e as consultas manuais específicas do roteiro não fazem parte da rodada automatizada registrada.
- Falta consolidar o relatório em PDF, exportar o diagrama e conferir o detalhamento do Moodle.
- Uma rodada funcional aprovada não comprova ausência de todas as condições de corrida, tolerância a todas as falhas ou limite máximo de robôs.
- A análise experimental de tempo de resposta e escalonabilidade continua prevista para a Etapa 2.
