# Conferência com o enunciado de ATR

Referência: Trabalho Final de ATR 2026/2, versão 001 de 21/10/2026. Esta conferência distingue o funcionamento observado das responsabilidades e entregas descritas no documento. O mapa da Figura 2 é um exemplo, e a organização das variáveis das Tabelas 1 e 2 é sugestiva.

## Funcionamento

| Requisito | Situação | Implementação e evidência |
|---|---|---|
| 4.1: anúncio MQTT de novos robôs e criação de processos | Atendido no fluxo com simulador MQTT | O simulador anuncia ID, geração e posição. A central entrega o anúncio ao Gestor, que cria o processo. A demo confirmou os quatro robôs iniciais e a inclusão de um quinto. |
| 4.2: processo por robô e pelo menos duas threads | Atendido | Cada robô usa um processo independente e threads de navegação e status, com IPC por sockets locais. |
| 4.3: setpoints, posição simulada e passagens seguras | Atendido funcionalmente | Setpoints saem da navegação e os sensores confirmam a posição. As reservas limitam a capacidade das passagens. Os testes com broker verificam a barreira de equipe e a rejeição de setpoints repetidos. |
| 4.4: status, falha, bateria, distância e indisponibilidade na recarga | Funcionamento atendido; responsabilidade distribuída de forma diferente | A demo verifica falha, parada, recuperação e recarga. A thread de status calcula a distância. A escolha do carregador e o despacho de recarga ficam na logística central, conforme a ressalva abaixo. |
| 4.5: filas iniciais por baia e equipes de um a quatro robôs | Atendido no cenário cooperativo | Baia 1: tamanhos 2 e 4. Baia 2: tamanhos 1 e 3. A fila preserva a ordem de inclusão e a entrega exige reunião, acoplamento e desacoplamento completos. |
| 4.5(a,b): robôs, caixas e status na tela | Atendido | Mapa compartilhado, lista de robôs, detalhes do selecionado, cargas pendentes e entregues, estados e bateria. |
| 4.5(c,d): manual/automático e destino arbitrário ou local conhecido | Atendido | O mesmo comando de destino atende coordenadas e locais do mapa. A demo confirmou troca de modo, destino manual e recusa de posição bloqueada. |
| 4.6: histórico por robô, rastreabilidade e timestamps em disco | Atendido | SQLite registra eventos e transportes com participantes. O filtro individual inclui eventos coletivos, mesmo antes da coleta. Há migração dos vínculos disponíveis nos bancos anteriores. |
| 4.7: tela de operação e comandos previstos | Atendido para as operações especificadas | Controles de modo/destino, simulação, inclusão de robôs/cargas e consulta do histórico estão na interface. |
| 4.8: defeitos, redução de bateria, novas caixas e novos robôs | Atendido | As quatro intervenções foram verificadas no fluxo com o processo de simulação e MQTT. |
| 4.9: tela unificada | Permitido explicitamente | Operação e simulação compartilham a interface web. |
| 6.3 e 6.5: caminhos portáveis e inicialização única | Atendido na aplicação | Os caminhos padrão são relativos ao projeto e resolvidos em execução. O lançador Python inicia broker, simulador, central, servidor web e, pelos anúncios, os processos dos robôs. |

## Diferenças de arquitetura

1. **Responsabilidade pela recarga, item 4.4:** o texto atribui à tarefa de status a decisão de redirecionar o robô a um carregador. Hoje, a logística central escolhe o carregador e envia a missão; o agente trata os sensores e o simulador calcula fisicamente a recarga. O comportamento foi validado, mas a distribuição das responsabilidades não reproduz literalmente o item.
2. **Paralelismo das tarefas:** há processos independentes de simulação e robôs, duas threads por robô, thread coletora e processamento de rede MQTT. Gestor e organizador da logística, porém, compartilham um ciclo central sequencial. Essa escolha deve ser explicitada no relatório e alinhada com a interpretação da arquitetura solicitada.

A comunicação com o simulador usa apenas comandos físicos, sensores e anúncios de disponibilidade/retirada. A geometria fica no arquivo compartilhado; o enunciado não exige publicá-la por MQTT. Não há chamadas remotas para avançar ciclos ou espelhar observações do adaptador.

## Entrega final ainda pendente

- Análise de tempo de resposta e escalabilidade, com cenário, hardware, critérios de corretude/responsividade e limite observado de robôs, conforme 5.2(b). A duração dos testes funcionais não substitui essa análise.
- Relatório final em PDF com arquitetura atual, partes críticas, sincronização, resultados e instruções de compilação/execução. O material versionado em `docs/relatorio/` ainda descreve a Etapa 1 e precisa ser atualizado e conferido.
- Vídeo de até dez minutos, apresentação do código, pacote ZIP e conferência dos detalhes do Moodle. Vídeo e pacote final não foram encontrados no repositório; podem existir fora dele.

Anticolisão descentralizada e otimização por IA com obstáculos aleatórios são extras do enunciado, não lacunas obrigatórias.

## Validação desta revisão

Os comandos e resultados consolidados estão em [visualization/README.md](../visualization/README.md#validação). A validação utiliza as configurações MQTT ativado e desativado, testes Python do histórico, integração com broker real e o fluxo da interface com o simulador separado. Ela não estabelece garantias de tempo real estrito nem um limite de escalabilidade.
