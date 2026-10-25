#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Executa testes da Etapa 1 de ATR e arquiva evidências reproduzíveis.

Uso (na raiz do repositório):
    python3 executar_testes_atr.py
    python3 executar_testes_atr.py --skip-scenarios
    python3 executar_testes_atr.py --scenarios base,cooperativo
    python3 executar_testes_atr.py --skip-build --ctest-regex unidade --skip-scenarios

Bibliotecas: apenas Python 3 padrão. Requer CMake, CTest, compilador C++17,
SQLite com headers, POSIX e GoogleTest (disponível ou obtido pelo CMake).
"""

import argparse
import csv
import datetime as dt
import hashlib
import json
import os
import platform
import re
import shlex
import shutil
import signal
import sqlite3
import subprocess
import sys
import threading
import time
import xml.etree.ElementTree as ET
import zipfile
from pathlib import Path

CENARIOS = ("base", "cooperativo", "disponibilidade", "passagens", "rota-equipe", "falhas", "recarga", "processos")
EVIDENCIAS = {
    "01_ipc_protocolo": ("IpcTest.", "ProtocolTest.", "FleetStateTest."),
    "02_concorrencia_buffers": ("BufferCollectorTest.", "DataCollectorTest."),
    "03_transporte_cooperativo": ("LogisticsTest.", "HardwareTest."),
    "04_passagens_restritas": ("PassageTest.",),
    "05_migracao_persistencia": ("StorageMigrationTest.",),
    "06_encerramento": ("Etapa1Shutdown.",),
    "07_integracao_cenarios": ("Cenarios/Etapa1Integration.",),
}


def localizar_repo(indicado):
    if indicado:
        candidatos = [Path(indicado)]
    else:
        candidatos = [Path.cwd(), Path(__file__).resolve().parent]
    for candidato in candidatos:
        for pasta in (candidato.resolve(), *candidato.resolve().parents):
            if (pasta / "CMakeLists.txt").exists() and (pasta / "tests/test_integration.cpp").exists():
                return pasta
    raise RuntimeError("Não encontrei o repositório. Execute na raiz ou informe --repo /caminho/do/projeto")


def criar_log_comando(comando, cwd, destino, timeout, ambiente=None, mostrar_tudo=False):
    """Captura stdout/stderr enquanto acompanha o processo e limita sua duração.

    A execução usa um novo grupo de processos, permitindo parar processos-filhos
    em caso de timeout (especialmente os robôs simulados).
    """
    destino.parent.mkdir(parents=True, exist_ok=True)
    inicio = time.monotonic()
    with destino.open("w", encoding="utf-8", errors="replace") as arquivo:
        arquivo.write("COMANDO: " + shlex.join([str(x) for x in comando]) + "\n")
        arquivo.write("DIRETORIO: " + str(cwd) + "\n")
        arquivo.write("INICIO: " + dt.datetime.now().astimezone().isoformat() + "\n")
        arquivo.write("=" * 78 + "\n")
        arquivo.flush()
        processo = subprocess.Popen(
            [str(x) for x in comando], cwd=str(cwd),
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            env=ambiente, text=True, errors="replace", bufsize=1,
            start_new_session=(os.name == "posix"),
        )

        def ler_saida():
            for linha in processo.stdout:
                arquivo.write(linha)
                arquivo.flush()
                limpa = re.sub(r"^\s*\d+:\s?", "", linha).strip()
                if mostrar_tudo or any(marcador in limpa for marcador in (
                    "[ RUN ", "[ PASSED ", "[ FAILED ", "[  FAILED", "100% tests passed",
                    "tests failed", "Test #", "Cenário concluído:", "Sistema encerrado.",
                    "Error", "Erro:", "-- Configuring done", "-- Build files have been written",
                )):
                    print("    " + limpa, flush=True)

        leitor = threading.Thread(target=ler_saida, daemon=True)
        leitor.start()
        try:
            retorno = processo.wait(timeout=timeout)
            esgotou = False
        except subprocess.TimeoutExpired:
            esgotou = True
            print(f"    TEMPO EXCEDIDO ({timeout}s): interrompendo processo e filhos", flush=True)
            if os.name == "posix":
                try:
                    os.killpg(processo.pid, signal.SIGTERM)
                except ProcessLookupError:
                    pass
            else:
                processo.terminate()
            try:
                processo.wait(timeout=4)
            except subprocess.TimeoutExpired:
                if os.name == "posix":
                    try:
                        os.killpg(processo.pid, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
                else:
                    processo.kill()
                processo.wait()
            retorno = 124
        finally:
            leitor.join(timeout=10)
        duracao = round(time.monotonic() - inicio, 2)
        arquivo.write("\n" + "=" * 78 + "\n")
        arquivo.write(f"RETORNO: {retorno} | TIMEOUT: {esgotou} | DURACAO_S: {duracao}\n")
    return {"comando": [str(x) for x in comando], "retorno": retorno, "timeout": esgotou,
            "duracao_s": duracao, "log": str(destino)}


def registrar_csv(caminho, colunas, linhas):
    caminho.parent.mkdir(parents=True, exist_ok=True)
    with caminho.open("w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(colunas)
        writer.writerows(linhas)


def verificar_banco(caminho_banco, pasta_saida):
    """Analisa apenas leitura; salva CSVs e retorna métricas sem modificar o banco."""
    resultado = {"banco": str(caminho_banco), "existe": caminho_banco.exists(), "valido": False}
    if not caminho_banco.exists():
        resultado["erro"] = "Banco não foi criado"
        return resultado
    try:
        conn = sqlite3.connect(caminho_banco.as_uri() + "?mode=ro", uri=True, timeout=5)
        try:
            consistencia = conn.execute("PRAGMA integrity_check").fetchall()
            chaves = conn.execute("PRAGMA foreign_key_check").fetchall()
            resultado["integrity_check"] = [x[0] for x in consistencia]
            resultado["violacoes_fk"] = len(chaves)
            tabelas = {r[0] for r in conn.execute("SELECT name FROM sqlite_master WHERE type='table'")}
            esperadas = {"runs", "robot_events", "cargo_transports", "cargo_members"}
            resultado["tabelas_esperadas"] = sorted(tabelas & esperadas)
            if not esperadas.issubset(tabelas):
                resultado["erro"] = "Tabelas ausentes: " + ", ".join(sorted(esperadas - tabelas))
                return resultado

            consultas = {
                "execucoes": "SELECT id, started_at_ms, ended_at_ms, status FROM runs ORDER BY id",
                "eventos": "SELECT id, run_id, occurred_at_ms, robot_id, team_id, bay_id, "
                           "mission_id, cargo_id, event_type, description, destination FROM robot_events ORDER BY id",
                "cargas": "SELECT run_id, mission_id, cargo_id, bay_id, destination, status, "
                          "pickup_at_ms, delivered_at_ms FROM cargo_transports ORDER BY mission_id",
                "participantes": "SELECT run_id, mission_id, robot_id FROM cargo_members ORDER BY mission_id, robot_id",
                "cargas_com_participantes": "SELECT t.run_id, t.mission_id, t.cargo_id, t.bay_id, "
                    "t.destination, t.status, GROUP_CONCAT(m.robot_id, ',') AS robos "
                    "FROM cargo_transports t LEFT JOIN cargo_members m "
                    "ON m.run_id=t.run_id AND m.mission_id=t.mission_id "
                    "GROUP BY t.run_id,t.mission_id ORDER BY t.mission_id",
            }
            contagens = {}
            for nome, sql in consultas.items():
                cursor = conn.execute(sql)
                colunas = [col[0] for col in cursor.description]
                linhas = cursor.fetchall()
                registrar_csv(pasta_saida / (nome + ".csv"), colunas, linhas)
                contagens[nome] = len(linhas)
            resultado["contagens"] = contagens
            resultado["status_execucoes"] = [r[0] for r in conn.execute("SELECT status FROM runs ORDER BY id")]
            resultado["cargas_por_status"] = dict(conn.execute(
                "SELECT status,COUNT(*) FROM cargo_transports GROUP BY status").fetchall())
            resultado["valido"] = (
                resultado["integrity_check"] == ["ok"] and len(chaves) == 0
                and bool(resultado["status_execucoes"])
                and all(s == "encerrada" for s in resultado["status_execucoes"])
            )
        finally:
            conn.close()
    except (sqlite3.Error, OSError) as e:
        resultado["erro"] = str(e)
    return resultado


def interpretar_xml(pasta_xml, diretorio_evidencias):
    """Extrai os resultados de cada caso de teste dos XMLs do GoogleTest."""
    linhas = []
    xmls = sorted(pasta_xml.rglob("*.xml"))
    for xml in xmls:
        try:
            root = ET.parse(xml).getroot()
            suites = [root] if root.tag == "testsuite" else root.findall(".//testsuite")
            for suite in suites:
                for caso in suite.findall("testcase"):
                    falhou = bool(caso.findall("failure") or caso.findall("error"))
                    ignorado = bool(caso.findall("skipped")) or caso.attrib.get("status") == "notrun"
                    status = "FALHOU" if falhou else ("IGNORADO" if ignorado else "APROVADO")
                    classe = caso.attrib.get("classname") or suite.attrib.get("name", "")
                    linhas.append({"suite": xml.name, "caso": classe + "." + caso.attrib.get("name", ""),
                                   "resultado": status, "duracao_s": caso.attrib.get("time", "")})
        except ET.ParseError as e:
            print(f"    Falha ao ler XML {xml}: {e}")
    with (diretorio_evidencias / "casos_de_teste.csv").open("w", newline="", encoding="utf-8") as arq:
        w = csv.DictWriter(arq, fieldnames=["suite", "caso", "resultado", "duracao_s"])
        w.writeheader()
        w.writerows(linhas)
    resumo = {"arquivos_xml": [str(x) for x in xmls], "total": len(linhas),
              "aprovados": sum(x["resultado"] == "APROVADO" for x in linhas),
              "falharam": sum(x["resultado"] == "FALHOU" for x in linhas),
              "ignorados": sum(x["resultado"] == "IGNORADO" for x in linhas)}
    return resumo, linhas


def extrair_destaques(log_ctest, pasta, casos):
    """Isola blocos de saída dos testes relevantes para capturas e para o relatório."""
    pasta.mkdir(parents=True, exist_ok=True)
    blocos = {}
    caso_atual = None
    if log_ctest.exists():
        for linha in log_ctest.read_text(encoding="utf-8", errors="replace").splitlines():
            limpa = re.sub(r"^\s*\d+:\s?", "", linha)
            ini = re.search(r"\[ RUN\s+\]\s+(\S+)", limpa)
            if ini:
                caso_atual = ini.group(1)
                blocos.setdefault(caso_atual, []).append(limpa)
                continue
            if caso_atual:
                blocos[caso_atual].append(limpa)
                if re.search(r"\[\s*(?:OK|FAILED|SKIPPED)\s*\]", limpa):
                    caso_atual = None
    for nome, prefixos in EVIDENCIAS.items():
        trechos = ["EVIDÊNCIAS EXTRAÍDAS DA SAÍDA REAL DO CTEST", "=" * 72]
        usados = 0
        for teste, conteudo in blocos.items():
            if any(teste.startswith(prefixo) for prefixo in prefixos):
                trechos.extend(["", "\n".join(conteudo)])
                usados += 1
        if not usados:
            trechos.append("Nenhum bloco de saída localizado. Consulte ctest-completo.log e o CSV de testes.")
        (pasta / (nome + ".txt")).write_text("\n".join(trechos) + "\n", encoding="utf-8")
    return len(blocos)


def versao(comando, cwd):
    try:
        p = subprocess.run(comando, cwd=cwd, text=True, capture_output=True, timeout=8)
        return p.stdout.strip().splitlines()[0] if p.returncode == 0 and p.stdout.strip() else "indisponível"
    except (OSError, subprocess.TimeoutExpired):
        return "indisponível"


def arquivo_resumo(destino, resumo):
    testes = resumo.get("testes", {})
    total = testes.get("total", 0)
    linhas = [
        "# Evidências de execução — ATR / Etapa 1", "",
        f"**Data:** {resumo['inicio']}",
        f"**Repositório:** `{resumo['repositorio']}`", f"**Commit:** `{resumo['ambiente']['git_commit']}`", "",
        "## Testes automatizados", "",
        f"- Casos identificados no XML: **{total}**",
        f"- Aprovados: **{testes.get('aprovados', 0)}**",
        f"- Falharam: **{testes.get('falharam', 0)}**",
        f"- Ignorados: **{testes.get('ignorados', 0)}**",
        f"- Resultado do CTest: **{'aprovado' if resumo.get('ctest', {}).get('retorno') == 0 else 'falhou / não executado'}**", "",
        "*A aprovação dos testes não comprova ausência universal de corridas, deadlocks ou inanição.*", "",
        "## Cenários de demonstração e SQLite", "",
        "| Cenário | Execução | SQLite | Eventos | Cargas entregues |", "|---|---|---|---:|---:|",
    ]
    for c, info in resumo.get("cenarios", {}).items():
        b = info.get("sqlite", {})
        linhas.append(f"| {c} | {'OK' if info['sucesso_execucao'] else 'FALHOU'} | "
                      f"{'OK' if b.get('valido') else 'FALHOU'} | "
                      f"{b.get('contagens', {}).get('eventos', '—')} | "
                      f"{b.get('cargas_por_status', {}).get('entregue', 0)} |")
    linhas.extend(["", "## Pastas e anexos produzidos", "",
                   "- `logs/`: configuração, compilação, CTest e execução dos cenários.",
                   "- `xml/`: saída estruturada do GoogleTest por executável.",
                   "- `casos_de_teste.csv`: relação de casos e seus resultados.",
                   "- `destaques/`: recortes textuais dos testes centrais para consulta/captura.",
                   "- `cenarios/<nome>/`: banco SQLite e tabelas CSV por cenário.",
                   "- `resumo.json`: metadados, códigos de retorno e consistência dos bancos.", "",
                   "**Atenção:** prints devem ser capturados a partir destes logs reais. Não há geração de screenshots fictícios.", ""])
    destino.write_text("\n".join(linhas), encoding="utf-8")


def main():
    p = argparse.ArgumentParser(description="Compila, roda testes e cenários de ATR e salva logs/SQLite/CSV/XML.")
    p.add_argument("--repo", help="Pasta raiz do projeto (por padrão detectada automaticamente)")
    p.add_argument("--build-dir", default="build-evidencias", help="Pasta de build relativa ao repo")
    p.add_argument("--output-dir", default="docs/evidencias", help="Destino das execuções (relativo ao repo)")
    p.add_argument("--parallel", type=int, default=2, help="Paralelismo da compilação (padrão: 2)")
    p.add_argument("--googletest-src", help="Fonte local do GoogleTest para configurar sem internet")
    p.add_argument("--skip-build", action="store_true", help="Usa os executáveis já compilados")
    p.add_argument("--skip-tests", action="store_true", help="Não executa o CTest")
    p.add_argument("--skip-scenarios", action="store_true", help="Não executa simulações para gerar bancos")
    p.add_argument("--scenarios", default=",".join(CENARIOS), help="Cenários separados por vírgula")
    p.add_argument("--ctest-regex", default=None, help="Seleciona suíte CTest por regex, ex.: unidade")
    p.add_argument("--timeout-tests", type=int, default=1100, help="Timeout global do CTest (s)")
    p.add_argument("--timeout-scenario", type=int, default=150, help="Timeout por cenário (s)")
    p.add_argument("--mostrar-saida", action="store_true", help="Mostra toda a saída no terminal (logs sempre completos)")
    p.add_argument("--no-zip", action="store_true", help="Não compacta a pasta de evidências ao final")
    args = p.parse_args()
    if args.parallel < 1 or args.timeout_scenario < 1 or args.timeout_tests < 1:
        p.error("Paralelismo e timeouts devem ser positivos")
    try:
        repo = localizar_repo(args.repo)
    except RuntimeError as e:
        p.error(str(e))
    cenarios = [x.strip() for x in args.scenarios.split(",") if x.strip()]
    invalidos = sorted(set(cenarios) - set(CENARIOS))
    if invalidos:
        p.error("Cenários desconhecidos: " + ", ".join(invalidos))

    build = (repo / args.build_dir).resolve()
    raiz_saida = (repo / args.output_dir).resolve()
    etiqueta = dt.datetime.now().strftime("%Y%m%d_%H%M%S") + f"_{os.getpid()}"
    saida = raiz_saida / ("execucao_" + etiqueta)
    logs = saida / "logs"
    xml = saida / "xml"
    for pasta in (logs, xml, saida / "destaques", saida / "cenarios"):
        pasta.mkdir(parents=True, exist_ok=True)

    resumo = {
        "inicio": dt.datetime.now().astimezone().isoformat(),
        "repositorio": str(repo), "build": str(build), "pasta_evidencias": str(saida),
        "ambiente": {"sistema": platform.platform(), "python": sys.version.split()[0],
                     "cmake": versao(["cmake", "--version"], repo),
                     "ctest": versao(["ctest", "--version"], repo),
                     "git_commit": versao(["git", "rev-parse", "HEAD"], repo)},
        "etapas": {}, "cenarios": {}, "testes": {},
    }
    print(f"\nREPOSITÓRIO: {repo}\nEVIDÊNCIAS: {saida}\n")
    erro = False

    try:
        pode_testar = True
        pode_cenarios = True
        if not args.skip_build:
            cmake_cmd = ["cmake", "-S", str(repo), "-B", str(build), "-DBUILD_TESTING=ON"]
            fonte_gtest = Path(args.googletest_src).resolve() if args.googletest_src else (
                repo / "build" / "_deps" / "googletest-src")
            if (fonte_gtest / "CMakeLists.txt").exists():
                cmake_cmd.append("-DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=" + str(fonte_gtest))
                print("    GoogleTest local:", fonte_gtest)
            elif args.googletest_src:
                p.error("--googletest-src não contém CMakeLists.txt: " + str(fonte_gtest))
            etapas_build = [
                ("configuracao", cmake_cmd, 240),
                ("compilacao", ["cmake", "--build", str(build), "--parallel", str(args.parallel)], 900),
            ]
            for nome, cmd, prazo in etapas_build:
                print("[ETAPA] " + nome, flush=True)
                r = criar_log_comando(cmd, repo, logs / (nome + ".log"), prazo, mostrar_tudo=args.mostrar_saida)
                resumo["etapas"][nome] = r
                if r["retorno"] != 0:
                    print(f"[FALHA] {nome}. Consulte {r['log']}")
                    erro = True
                    pode_testar = False
                    pode_cenarios = False
                    break

        if not args.skip_tests and pode_testar:
            print("[ETAPA] teste automatizados via CTest (log completo + XML GoogleTest)", flush=True)
            env = os.environ.copy()
            env["GTEST_OUTPUT"] = "xml:" + str(xml) + os.sep
            env["GTEST_COLOR"] = "no"
            cmd = ["ctest", "--test-dir", str(build), "--verbose", "--output-on-failure"]
            if args.ctest_regex:
                cmd += ["-R", args.ctest_regex]
            registro = criar_log_comando(cmd, repo, logs / "ctest-completo.log", args.timeout_tests,
                                          ambiente=env, mostrar_tudo=args.mostrar_saida)
            resumo["ctest"] = registro
            resumo["testes"], casos = interpretar_xml(xml, saida)
            n_blocos = extrair_destaques(logs / "ctest-completo.log", saida / "destaques", casos)
            resumo["blocos_identificados_no_log"] = n_blocos
            print(f"    CTest: retorno={registro['retorno']} | casos no XML={resumo['testes']['total']} | "
                  f"aprovados={resumo['testes']['aprovados']} | falharam={resumo['testes']['falharam']}")
            if registro["retorno"] != 0 or not resumo["testes"]["total"] or resumo["testes"]["falharam"]:
                erro = True
        elif args.skip_tests:
            print("[PULADO] CTest (--skip-tests)")
        else:
            print("[PULADO] CTest por falha na compilação")

        executavel = build / "app" / "simulador_exec"
        if args.skip_scenarios:
            print("[PULADO] cenários (--skip-scenarios)")
        elif not pode_cenarios or not executavel.exists():
            print("[FALHA] executável de cenários não encontrado ou build falhou")
            erro = True
        else:
            for cenario in cenarios:
                print(f"[CENÁRIO] {cenario}", flush=True)
                pasta = saida / "cenarios" / cenario
                pasta.mkdir(parents=True, exist_ok=True)
                banco = pasta / (cenario + ".db")
                comando = [str(executavel), "--cenario", cenario, "--banco", str(banco)]
                execucao = criar_log_comando(comando, repo, logs / ("cenario-" + cenario + ".log"),
                                              args.timeout_scenario, mostrar_tudo=args.mostrar_saida)
                concluiu = "Cenário concluído: " + cenario in Path(execucao["log"]).read_text(
                    encoding="utf-8", errors="replace")
                sucesso = execucao["retorno"] == 0 and concluiu
                banco_info = verificar_banco(banco, pasta)
                resumo["cenarios"][cenario] = {"execucao": execucao, "sucesso_execucao": sucesso,
                                                "sqlite": banco_info}
                print(f"    execução={'OK' if sucesso else 'FALHOU'} | "
                      f"SQLite={'OK' if banco_info['valido'] else 'FALHOU'} | "
                      f"eventos={banco_info.get('contagens', {}).get('eventos', '?')}")
                if not sucesso or not banco_info["valido"]:
                    erro = True
    except (OSError, subprocess.SubprocessError, sqlite3.Error) as e:
        print(f"[ERRO] Falha imprevista: {e}", file=sys.stderr)
        resumo["erro_imprevisto"] = str(e)
        erro = True
    finally:
        resumo["fim"] = dt.datetime.now().astimezone().isoformat()
        resumo["resultado_geral"] = "FALHOU" if erro else "CONCLUIDO_SEM_FALHAS_NAS_ETAPAS_EXECUTADAS"
        pacote = raiz_saida / (saida.name + ".zip")
        if not args.no_zip:
            resumo["arquivo_zip"] = str(pacote)
        (saida / "resumo.json").write_text(json.dumps(resumo, ensure_ascii=False, indent=2), encoding="utf-8")
        arquivo_resumo(saida / "LEIA-ME-RESULTADOS.md", resumo)
        with (saida / "manifesto_sha256.txt").open("w", encoding="utf-8") as manifesto:
            for item in sorted(saida.rglob("*")):
                if item.is_file() and item.name != "manifesto_sha256.txt":
                    sha = hashlib.sha256()
                    with item.open("rb") as entrada:
                        for bloco in iter(lambda: entrada.read(1024 * 1024), b""):
                            sha.update(bloco)
                    manifesto.write(f"{sha.hexdigest()}  {item.relative_to(saida)}\n")
        if not args.no_zip:
            try:
                with zipfile.ZipFile(pacote, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
                    for item in sorted(saida.rglob("*")):
                        if item.is_file():
                            z.write(item, arcname=str(Path(saida.name) / item.relative_to(saida)))
                print("ARQUIVO ZIP:", pacote)
            except OSError as e:
                erro = True
                print(f"[FALHA] Não foi possível gerar ZIP: {e}", file=sys.stderr)
        print("\n" + "=" * 72)
        print("RESULTADO:", resumo["resultado_geral"])
        print("RESUMO:", saida / "LEIA-ME-RESULTADOS.md")
        print("LOGS E BANCOS:", saida)
        print("=" * 72)
    return 1 if erro else 0


if __name__ == "__main__":
    sys.exit(main())
