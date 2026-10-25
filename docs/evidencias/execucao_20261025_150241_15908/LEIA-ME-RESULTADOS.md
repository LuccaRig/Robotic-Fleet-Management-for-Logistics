# Evidências de execução — ATR / Etapa 1

**Data:** 2026-10-25T15:02:41.719298-03:00
**Repositório:** `/home/ubuntu/ATR/TP/Robotic-Fleet-Management-for-Logistics`
**Commit:** `b1147e30575527e514917c2331bb117769b7b80d`

## Testes automatizados

- Casos identificados no XML: **48**
- Aprovados: **48**
- Falharam: **0**
- Ignorados: **0**
- Resultado do CTest: **aprovado**

## Cenários de demonstração e SQLite

| Cenário | Execução | SQLite | Eventos | Cargas entregues |
|---|---|---|---:|---:|
| base | OK | OK | 42 | 1 |
| cooperativo | OK | OK | 593 | 3 |
| disponibilidade | OK | OK | 94 | 1 |
| passagens | OK | OK | 287 | 6 |
| rota-equipe | OK | OK | 181 | 1 |
| falhas | OK | OK | 114 | 1 |
| recarga | OK | OK | 48 | 1 |
| processos | OK | OK | 11 | 0 |

## Pastas e anexos produzidos

- `logs/`: configuração, compilação, CTest e execução dos cenários.
- `xml/`: saída estruturada do GoogleTest por executável.
- `casos_de_teste.csv`: relação de casos e seus resultados.
- `destaques/`: recortes textuais dos testes centrais para consulta/captura.
- `cenarios/<nome>/`: banco SQLite e tabelas CSV por cenário.
- `resumo.json`: metadados, códigos de retorno e consistência dos bancos.

