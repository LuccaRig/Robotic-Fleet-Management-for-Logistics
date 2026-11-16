import argparse
from collections import deque
from contextlib import ExitStack, closing
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from queue import Full, Queue
from pathlib import Path
import shutil
import signal
import socket
import sqlite3
import subprocess
import tempfile
import threading
import time
from urllib.parse import parse_qs, urlsplit

try:
    import paho.mqtt.client as mqtt
except ImportError:
    raise SystemExit("Instale a dependência: python3 -m pip install -r visualization/requirements.txt")


ROOT = Path(__file__).resolve().parent.parent
WEB = Path(__file__).resolve().parent / "web"
ACTIONS = {"definir_modo", "definir_destino", "definir_falha", "definir_bateria",
           "adicionar_carga", "adicionar_lote_cargas", "adicionar_robo", "retomar_missao"}
SCENARIOS = {
    "base": "Base",
    "escala": "Escala · 15 robôs",
    "estresse-passagens": "Estresse de passagens · 100 robôs",
}
SCENARIO_MAPS = {
    "passagens": "config/galpao-passagens.txt",
    "escala": "config/galpao-escala.txt",
    "estresse-passagens": "config/galpao-estresse-passagens.txt",
}


class ScenarioSelection:
    def __init__(self):
        self.choices = Queue()
        self.lock = threading.Lock()
        self.status = "waiting"
        self.error = None

    def select(self, scenario):
        with self.lock:
            if self.status not in {"waiting", "error"}:
                return False
            previous_status, previous_error = self.status, self.error
            self.status = "starting"
            self.error = None
            try:
                self.choices.put_nowait(scenario)
            except Full:
                self.status, self.error = previous_status, previous_error
                return False
            return True

    def fail(self, error):
        with self.lock:
            self.status = "error"
            self.error = str(error)

    def snapshot(self):
        with self.lock:
            return {"estado": self.status, "erro": self.error}

    def running(self):
        with self.lock:
            self.status = "running"
            self.error = None


class Bridge:
    def __init__(self, port):
        self.lock = threading.Lock()
        self.fleet = {"robos": [], "cargas": [], "missoes": [], "passagens": []}
        self.results = deque(maxlen=128)
        self.received = None
        self.ready = False
        self.client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
        self.client.on_connect = self.on_connect
        self.client.on_subscribe = self.on_subscribe
        self.client.on_disconnect = self.on_disconnect
        self.client.on_message = self.on_message
        self.client.max_queued_messages_set(128)
        self.client.reconnect_delay_set(1, 5)
        self.client.connect_async("127.0.0.1", port)

    def on_connect(self, client, userdata, flags, reason, properties):
        if not reason.is_failure:
            client.subscribe([("fleet/telemetry/fleet", 1), ("fleet/commands/results", 1)])

    def on_subscribe(self, client, userdata, mid, reasons, properties):
        with self.lock:
            self.ready = bool(reasons) and all(not reason.is_failure for reason in reasons)

    def on_disconnect(self, client, userdata, flags, reason, properties):
        with self.lock:
            self.ready = False

    def on_message(self, client, userdata, message):
        try:
            payload = json.loads(message.payload)
            if not isinstance(payload, dict):
                return
            with self.lock:
                if message.topic == "fleet/telemetry/fleet" and isinstance(payload.get("robos"), list):
                    self.fleet = payload
                    self.received = time.monotonic()
                elif message.topic == "fleet/commands/results" and "aceito" in payload:
                    self.results.append(payload)
        except (ValueError, UnicodeError):
            return

    def snapshot(self):
        with self.lock:
            age = round((time.monotonic() - self.received) * 1000) if self.received is not None else None
            return {**self.fleet, "respostas": list(self.results), "conectado": self.ready,
                    "idade_ms": age, "atual": self.ready and age is not None and age < 3000}

    def publish(self, command):
        if not isinstance(command, dict) or command.get("acao") not in ACTIONS:
            raise ValueError("Comando de operação inválido")
        if not isinstance(command.get("id"), str) or not 1 <= len(command["id"].encode()) <= 64:
            raise ValueError("Informe um identificador de comando válido")
        if not self.snapshot()["atual"]:
            raise ConnectionError("Aguarde a conexão com a central para enviar comandos")
        payload = json.dumps(command, ensure_ascii=False, allow_nan=False)
        if len(payload.encode()) > 16384:
            raise ValueError("Comando muito longo")
        result = self.client.publish("fleet/commands", payload, qos=1, retain=False)
        if result.rc != mqtt.MQTT_ERR_SUCCESS:
            raise ConnectionError("Não foi possível encaminhar o comando à central")


def history(database, query):
    view = query.get("visao", ["eventos"])[0]
    page = int(query.get("pagina", ["0"])[0])
    robot = query.get("robo", [""])[0]
    if view not in {"eventos", "cargas"} or not 0 <= page <= 100000:
        raise ValueError("Consulta de histórico inválida")
    robot = int(robot) if robot else None
    if not database.exists():
        return {"itens": [], "tem_mais": False}
    with closing(sqlite3.connect(database.as_uri() + "?mode=ro", uri=True, timeout=0.3)) as connection:
        connection.row_factory = sqlite3.Row
        run = connection.execute("SELECT MAX(id) FROM runs").fetchone()[0]
        params = [run]
        if view == "eventos":
            sql = "SELECT id, robot_id, cargo_id, mission_id, description, occurred_at_ms FROM robot_events WHERE run_id=?"
            if robot is not None:
                sql += """ AND (robot_id=? OR (robot_id<0 AND EXISTS (
                    SELECT 1 FROM robot_event_members m WHERE m.event_id=robot_events.id
                    AND m.robot_id=?)))"""
                params.extend([robot, robot])
            sql += " ORDER BY id DESC LIMIT 51 OFFSET ?"
        else:
            sql = """SELECT t.*, (SELECT GROUP_CONCAT(m.robot_id, ', ') FROM cargo_members m
                     WHERE m.run_id=t.run_id AND m.mission_id=t.mission_id) AS robos
                     FROM cargo_transports t WHERE t.run_id=?"""
            if robot is not None:
                sql += " AND EXISTS (SELECT 1 FROM cargo_members m WHERE m.run_id=t.run_id AND m.mission_id=t.mission_id AND m.robot_id=?)"
                params.append(robot)
            sql += " ORDER BY t.mission_id DESC LIMIT 51 OFFSET ?"
        params.append(page * 50)
        rows = [dict(row) for row in connection.execute(sql, params)]
        return {"itens": rows[:50], "tem_mais": len(rows) > 50}


class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *args, bridge, config, process, database, **kwargs):
        self.bridge = bridge
        self.config = config
        self.process = process
        self.database = database
        super().__init__(*args, directory=str(WEB), **kwargs)

    def log_message(self, format, *args):
        return

    def send_json(self, payload, status=200):
        body = json.dumps(payload, ensure_ascii=False, allow_nan=False).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        url = urlsplit(self.path)
        try:
            if url.path == "/api/config":
                self.send_json(self.config)
            elif url.path == "/api/state":
                self.send_json({**self.bridge.snapshot(), "central_ativa": self.process.poll() is None})
            elif url.path == "/api/history":
                self.send_json(history(self.database, parse_qs(url.query)))
            elif url.path.startswith("/api/"):
                self.send_json({"erro": "Recurso não encontrado"}, 404)
            else:
                super().do_GET()
        except (ValueError, sqlite3.Error) as error:
            self.send_json({"erro": "Histórico temporariamente indisponível" if isinstance(error, sqlite3.Error) else str(error)}, 503 if isinstance(error, sqlite3.Error) else 400)
        except (BrokenPipeError, ConnectionResetError):
            return

    def do_POST(self):
        try:
            if urlsplit(self.path).path != "/api/commands":
                self.send_json({"erro": "Recurso não encontrado"}, 404)
                return
            origin = self.headers.get("Origin")
            if origin and origin not in {f"http://127.0.0.1:{self.server.server_port}", f"http://localhost:{self.server.server_port}"}:
                self.send_json({"erro": "Origem não permitida"}, 403)
                return
            length = int(self.headers.get("Content-Length", "0"))
            if not 0 < length <= 16384 or self.headers.get_content_type() != "application/json":
                raise ValueError("Envie um comando JSON de até 16384 bytes")
            command = json.loads(self.rfile.read(length))
            if self.process.poll() is not None:
                raise ConnectionError("A central foi encerrada")
            self.bridge.publish(command)
            self.send_json({"id": command["id"], "encaminhado": True}, 202)
        except (ValueError, UnicodeError) as error:
            self.send_json({"erro": str(error)}, 400)
        except ConnectionError as error:
            self.send_json({"erro": str(error)}, 503)


class ScenarioPickerHandler(SimpleHTTPRequestHandler):
    def __init__(self, *args, selection, **kwargs):
        self.selection = selection
        super().__init__(*args, directory=str(WEB), **kwargs)

    def log_message(self, format, *args):
        return

    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def send_json(self, payload, status=200):
        body = json.dumps(payload, ensure_ascii=False, allow_nan=False).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        url = urlsplit(self.path)
        if url.path == "/":
            self.path = "/scenario-picker.html"
            super().do_GET()
        elif url.path == "/api/scenarios":
            self.send_json([{"id": scenario, "nome": label} for scenario, label in SCENARIOS.items()])
        elif url.path == "/api/start-status":
            self.send_json(self.selection.snapshot())
        elif url.path == "/api/config":
            self.send_json({"erro": "Selecione um cenário para iniciar a operação"}, 503)
        elif url.path.startswith("/api/"):
            self.send_json({"erro": "Recurso não encontrado"}, 404)
        else:
            super().do_GET()

    def do_POST(self):
        if urlsplit(self.path).path != "/api/start":
            self.send_json({"erro": "Recurso não encontrado"}, 404)
            return
        origin = self.headers.get("Origin")
        if origin and origin not in {f"http://127.0.0.1:{self.server.server_port}",
                                     f"http://localhost:{self.server.server_port}"}:
            self.send_json({"erro": "Origem não permitida"}, 403)
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if not 0 < length <= 1024 or self.headers.get_content_type() != "application/json":
                raise ValueError("Envie a seleção do cenário como JSON")
            payload = json.loads(self.rfile.read(length))
            scenario = payload.get("cenario") if isinstance(payload, dict) else None
            if not isinstance(scenario, str) or scenario not in SCENARIOS:
                raise ValueError("Cenário inválido")
        except (ValueError, UnicodeError) as error:
            self.send_json({"erro": str(error)}, 400)
            return
        if not self.selection.select(scenario):
            self.send_json({"erro": "Uma inicialização já está em andamento"}, 409)
            return
        self.send_json({"cenario": scenario, "estado": "starting"}, 202)


def stop(process):
    process_group = process.pid
    try:
        if process.poll() is None:
            if process.stdin:
                try:
                    process.stdin.write("sair\n")
                    process.stdin.flush()
                except OSError:
                    pass
                try:
                    process.wait(timeout=8)
                except subprocess.TimeoutExpired:
                    pass
            if process.poll() is None:
                try:
                    os.killpg(process_group, signal.SIGTERM)
                except ProcessLookupError:
                    pass
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    try:
                        os.killpg(process_group, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
                    process.wait()

        try:
            os.killpg(process_group, signal.SIGTERM)
        except ProcessLookupError:
            return
        deadline = time.monotonic() + 1
        while time.monotonic() < deadline:
            try:
                os.killpg(process_group, 0)
            except ProcessLookupError:
                break
            time.sleep(0.05)
        else:
            try:
                os.killpg(process_group, signal.SIGKILL)
            except ProcessLookupError:
                pass
    finally:
        if process.stdin:
            try:
                process.stdin.close()
            except OSError:
                pass


def start_services(stack, args, scenario, executable, database):
    map_path = (ROOT / (args.mapa or SCENARIO_MAPS.get(scenario, "config/galpao.txt"))).resolve()
    if not map_path.is_file():
        raise RuntimeError(f"Mapa não encontrado: {map_path}")
    database.parent.mkdir(parents=True, exist_ok=True)
    logs = ROOT / "logs"
    logs.mkdir(exist_ok=True)
    temporary = Path(stack.enter_context(tempfile.TemporaryDirectory(prefix="atr-operacao-")))
    with socket.socket() as reservation:
        reservation.bind(("127.0.0.1", 0))
        port = reservation.getsockname()[1]
    broker_config = temporary / "mosquitto.conf"
    broker_config.write_text(f"listener {port} 127.0.0.1\nallow_anonymous true\npersistence false\n")
    broker_log = stack.enter_context((logs / "operacao-mqtt.log").open("a"))
    broker = subprocess.Popen([args.broker, "-c", str(broker_config)], stdout=broker_log,
                              stderr=subprocess.STDOUT, start_new_session=True)
    stack.callback(stop, broker)
    deadline = time.monotonic() + 5
    while True:
        if broker.poll() is not None or time.monotonic() > deadline:
            raise RuntimeError("Não foi possível iniciar o broker. Consulte logs/operacao-mqtt.log.")
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.2):
                break
        except OSError:
            time.sleep(0.05)
    bridge = Bridge(port)
    bridge.client.loop_start()
    stack.callback(bridge.client.loop_stop)
    stack.callback(bridge.client.disconnect)
    simulator_log = stack.enter_context((logs / "operacao-simulador.log").open("a"))
    simulator = subprocess.Popen([str(executable), "--simulador", "--cenario", scenario, "--mapa", str(map_path),
                                  "--mqtt-host", "127.0.0.1", "--mqtt-port", str(port)],
                                 cwd=temporary, stdout=simulator_log, stderr=subprocess.STDOUT,
                                 start_new_session=True)
    stack.callback(stop, simulator)
    central_log = stack.enter_context((logs / "operacao-central.log").open("a"))
    process = subprocess.Popen([str(executable), "--cenario", scenario, "--mapa", str(map_path), "--banco", str(database),
                                "--interativo", "--simulador-mqtt", "--mqtt-host", "127.0.0.1", "--mqtt-port", str(port)],
                               cwd=temporary, stdin=subprocess.PIPE, stdout=central_log, stderr=subprocess.STDOUT,
                               text=True, start_new_session=True)
    stack.callback(stop, process)
    config = {"mapa": map_path.read_text(), "cenario": scenario}
    return {"bridge": bridge, "config": config, "process": process, "database": database}


def main():
    parser = argparse.ArgumentParser(description="Tela unificada de operação e simulação local")
    parser.add_argument("--executavel", default="build-mqtt/app/simulador_exec")
    parser.add_argument("--cenario", choices=SCENARIOS)
    parser.add_argument("--mapa")
    parser.add_argument("--banco", default="logs/operacao.db")
    parser.add_argument("--porta", type=int, default=8080)
    parser.add_argument("--broker", default=shutil.which("mosquitto"))
    args = parser.parse_args()
    executable = (ROOT / args.executavel).resolve()
    database = (ROOT / args.banco).resolve()
    if not executable.is_file():
        parser.error("Executável não encontrado. Informe o caminho com --executavel.")
    if not args.broker or not Path(args.broker).is_file():
        parser.error("Mosquitto não encontrado. Informe o caminho com --broker.")
    if not 1 <= args.porta <= 65535:
        parser.error("A porta HTTP deve estar entre 1 e 65535")
    if args.cenario:
        with ExitStack() as stack:
            services = start_services(stack, args, args.cenario, executable, database)
            server = ThreadingHTTPServer(("127.0.0.1", args.porta),
                                         partial(Handler, **services))
            stack.callback(server.server_close)
            print(f"\nTela de operação disponível em http://127.0.0.1:{args.porta}\nCtrl+C encerra a tela, a central, o simulador e o broker.\n", flush=True)
            server.serve_forever()
        return

    selection = ScenarioSelection()
    server = ThreadingHTTPServer(("127.0.0.1", args.porta),
                                 partial(ScenarioPickerHandler, selection=selection))
    server_thread = threading.Thread(target=server.serve_forever, name="scenario-picker")
    server_thread.start()
    with ExitStack() as stack:
        try:
            print(f"\nEscolha o cenário em http://127.0.0.1:{args.porta}\nCtrl+C encerra o servidor.\n", flush=True)
            while True:
                scenario = selection.choices.get()
                try:
                    with ExitStack() as attempt:
                        services = start_services(attempt, args, scenario, executable, database)
                        stack.enter_context(attempt.pop_all())
                except (OSError, RuntimeError) as error:
                    selection.fail(error)
                    print(f"Não foi possível iniciar o cenário {scenario}: {error}", flush=True)
                    continue
                server.RequestHandlerClass = partial(Handler, **services)
                selection.running()
                print(f"\nCenário {scenario} iniciado. Tela de operação disponível em http://127.0.0.1:{args.porta}\nCtrl+C encerra a tela, a central, o simulador e o broker.\n", flush=True)
                server_thread.join()
                break
        finally:
            server.shutdown()
            server.server_close()
            server_thread.join()


if __name__ == "__main__":
    def interrupt(signum, frame):
        raise KeyboardInterrupt

    signal.signal(signal.SIGTERM, interrupt)
    try:
        main()
    except KeyboardInterrupt:
        print("\nOperação encerrada.")
    except (OSError, RuntimeError) as error:
        raise SystemExit(str(error))
