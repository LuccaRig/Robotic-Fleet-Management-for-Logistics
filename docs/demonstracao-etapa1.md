# Roteiro de demonstração e evidências da Etapa 1

## Situação da verificação

**Pendente de execução autorizada.** Os comandos, cenários e testes descritos abaixo foram preparados, mas não foram executados nesta implementação. Não há tempos medidos, resultados aprovados, capturas ou traces de execução para apresentar como evidência.

A revisão estática e a organização dos commits não substituem a execução. O relatório deve conter resultados observados depois da validação.

## Matriz de cenários

Execute os comandos a partir da raiz do repositório, depois de preparar o executável conforme o README.

| Cenário | Comando | Condição de conclusão prevista |
|---|---|---|
| Base | `./build/app/simulador_exec --cenario base --banco logs/etapa1-base.db` | Coleta, transporte e entrega de uma caixa, com retorno do robô à disponibilidade. |
| Cooperativo | `./build/app/simulador_exec --cenario cooperativo --banco logs/etapa1-cooperativo.db` | Entregas de cargas com 2, 3 e 4 integrantes, mantendo a fila. |
| Disponibilidade | `./build/app/simulador_exec --cenario disponibilidade --banco logs/etapa1-disponibilidade.db` | Carga permanece pendente com um robô e avança depois de disponibilizar o segundo. |
| Passagens | `./build/app/simulador_exec --cenario passagens --banco logs/etapa1-passagens.db` | Seis cargas, uso de capacidades 1, 2 e 3 e espera observada em fila, sem ultrapassar capacidade. |
| Rota de equipe | `./build/app/simulador_exec --cenario rota-equipe --banco logs/etapa1-rota.db` | Equipe de quatro usa a alternativa de capacidade quatro e evita os três trechos menores. |
| Falhas | `./build/app/simulador_exec --cenario falhas --banco logs/etapa1-falhas.db` | Cancelamento antes de acoplar, suspensão durante transporte, recuperação explícita e entrega. |
| Recarga | `./build/app/simulador_exec --cenario recarga --banco logs/etapa1-recarga.db` | Bateria baixa provoca recarga e retorno somente após confirmação de carga completa. |
| Processos | `./build/app/simulador_exec --cenario processos --banco logs/etapa1-processos.db` | ID duplicado é recusado; processo é interrompido, recolhido e recriado com nova geração após remoção explícita. |

O cenário completo só imprime sua conclusão quando as condições programadas forem satisfeitas. O prazo de 120 segundos detecta ausência de progresso. Uma expiração exige investigação; ela não demonstra, sozinha, um limite de capacidade do hardware.

## Demonstração interativa

```sh
./build/app/simulador_exec --cenario cooperativo --interativo --banco logs/etapa1-interativo.db
```

1. Consultar `status`, `cargas` e `passagens`.
2. Registrar os PIDs apresentados e compará-los com o PID do processo central.
3. Consultar cargas durante a reunião da equipe e após o acoplamento.
4. Injetar `falha ID 1` em um integrante durante transporte.
5. Observar a suspensão da missão e a ausência de novos movimentos.
6. Limpar com `falha ID 0` e restaurar a bateria se necessário.
7. Confirmar que a missão permanece suspensa até `retomar MISSÃO`.
8. Acompanhar a entrega e encerrar com `sair`.

O roteiro automático de recuperação não é aplicado no modo interativo. Os IDs e números de missão devem ser obtidos das consultas e não presumidos após cancelamentos.

## Consultas de rastreabilidade

Se o cliente `sqlite3` estiver instalado, estas consultas ajudam a colher evidências. São instruções futuras, sem execução registrada aqui.

```sql
SELECT id, started_at_ms, ended_at_ms, status
FROM runs ORDER BY id DESC;

SELECT id, timestamp, occurred_at_ms, robot_id, generation,
       cargo_id, mission_id, event_type, description
FROM robot_events
WHERE run_id = (SELECT MAX(id) FROM runs)
ORDER BY id;

SELECT t.run_id, t.mission_id, t.cargo_id, t.bay_id,
       t.destination, t.status, COUNT(m.robot_id) AS integrantes
FROM cargo_transports AS t
LEFT JOIN cargo_members AS m
  ON m.run_id = t.run_id AND m.mission_id = t.mission_id
WHERE t.run_id = (SELECT MAX(id) FROM runs)
GROUP BY t.run_id, t.mission_id, t.cargo_id, t.bay_id, t.destination, t.status;

SELECT t.cargo_id, t.mission_id, m.robot_id,
       t.pickup_at_ms, t.delivered_at_ms
FROM cargo_transports AS t
JOIN cargo_members AS m
  ON m.run_id = t.run_id AND m.mission_id = t.mission_id
WHERE t.run_id = (SELECT MAX(id) FROM runs)
ORDER BY t.mission_id, m.robot_id;
```

Não abra o banco de inspeção versionado em modo de escrita para esses experimentos. Prefira os bancos `etapa1-*.db`, ignorados pelo Git.

## Cobertura automatizada preparada

| Grupo | Casos preparados |
|---|---|
| Protocolo e IPC | Round-trip, versão incompatível, truncamento, limite de tamanho, fragmentação e EOF. |
| Estado corrente | Geração, sequência, revisão, leitura regressiva e desconexão. |
| Agente | Permissão antes de movimento, sensor confirmado, distância por segmentos, falha transitória, revisão antiga, comando inválido, início/parada repetidos e recarga completa. |
| Logística | Espera por equipe completa, barreiras de chegada/acoplamento/entrega, cancelamento e suspensão de equipe parcialmente acoplada. |
| Passagens | Capacidade, oposição de fluxo, manutenção de ocupação após cancelamento e reserva indivisível. |
| Hardware | Propostas de toda a equipe antes de avançar e rejeição de revisão antiga. |
| Persistência | Casos preexistentes do coletor/buffer, migração de banco antigo, participantes, deduplicação e propagação de erro de gravação. |
| Integração | Oito cenários com processos reais e encerramento durante operação. |

Os testes unitários de domínio usam chamadas por passos. Integrações usam esperas por condição com prazo. As pausas curtas entre consultas não são critérios de sucesso.

## Material para o relatório

Após execução, preencher uma tabela para cada cenário:

| Campo | Registro a produzir |
|---|---|
| Ambiente | Sistema operacional, compilador, versões de CMake/SQLite e hardware. |
| Revisão | Hash do commit utilizado. |
| Configuração | Cenário, quantidade de robôs, cargas e capacidades. |
| Resultado esperado | Invariante ou condição que o cenário deve comprovar. |
| Resultado observado | Estado final e dados efetivamente consultados. |
| Evidência | Trecho do terminal, PIDs, consulta SQLite ou saída dos testes. |
| Situação | Aprovado, reprovado ou pendente, com justificativa. |

O relatório da Etapa 1 pode reutilizar o diagrama e as tabelas de sincronização da documentação de arquitetura. A apresentação deve explicar a diferença entre processo e thread, o IPC, as barreiras de equipe, a reserva indivisível de passagens e a separação entre estado corrente e histórico.

## Critérios pendentes para considerar a entrega validada

- Compilar em macOS e Linux.
- Executar testes unitários e integrações.
- Confirmar que nenhum filho permanece após parada normal e interrupção.
- Confirmar que o histórico foi drenado e a execução marcada como encerrada.
- Verificar participantes e timestamps de cada carga entregue.
- Coletar evidências e registrar eventuais falhas.

Não há análise experimental de escalonabilidade nesta etapa. A representação de transporte é lógica, não física: integrantes podem compartilhar posição de carga, áreas abertas não possuem controle geral de colisão e a recuperação de integrante acoplado não faz transferência física automática de carga.
