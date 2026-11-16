import json
from functools import partial
from http.server import ThreadingHTTPServer
import os
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from urllib.error import HTTPError
from urllib.request import Request, urlopen

from visualization.server import SCENARIOS, ScenarioPickerHandler, ScenarioSelection, stop


class ScenarioPickerTest(unittest.TestCase):
    def setUp(self):
        self.selection = ScenarioSelection()
        self.server = ThreadingHTTPServer(
            ("127.0.0.1", 0),
            partial(ScenarioPickerHandler, selection=self.selection),
        )
        self.thread = threading.Thread(target=self.server.serve_forever)
        self.thread.start()
        self.base_url = f"http://127.0.0.1:{self.server.server_port}"

    def tearDown(self):
        self.server.shutdown()
        self.thread.join()
        self.server.server_close()

    def get_json(self, path):
        with urlopen(self.base_url + path) as response:
            return response.status, json.load(response)

    def post_scenario(self, scenario):
        request = Request(
            self.base_url + "/api/start",
            data=json.dumps({"cenario": scenario}).encode(),
            headers={"Content-Type": "application/json", "Origin": self.base_url},
            method="POST",
        )
        try:
            with urlopen(request) as response:
                return response.status, json.load(response)
        except HTTPError as error:
            with error:
                return error.code, json.load(error)

    def test_picker_page_and_available_scenarios_are_served(self):
        with urlopen(self.base_url + "/") as response:
            self.assertEqual(response.status, 200)
            self.assertIn("Escolha um cenário", response.read().decode())

        status, scenarios = self.get_json("/api/scenarios")
        self.assertEqual(status, 200)
        self.assertEqual({item["id"] for item in scenarios}, set(SCENARIOS))
        self.assertIn("15 robôs", next(item["nome"] for item in scenarios if item["id"] == "escala"))

    def test_invalid_scenario_does_not_start_services(self):
        status, response = self.post_scenario(["escala"])
        self.assertEqual(status, 400)
        self.assertEqual(response["erro"], "Cenário inválido")
        self.assertEqual(self.selection.snapshot()["estado"], "waiting")
        self.assertTrue(self.selection.choices.empty())

    def test_valid_scenario_is_queued_only_once(self):
        status, response = self.post_scenario("escala")
        self.assertEqual(status, 202)
        self.assertEqual(response, {"cenario": "escala", "estado": "starting"})
        self.assertEqual(self.selection.choices.get_nowait(), "escala")

        status, response = self.post_scenario("base")
        self.assertEqual(status, 409)
        self.assertIn("inicialização", response["erro"])

    def test_startup_error_is_visible_to_the_picker(self):
        self.selection.fail(RuntimeError("Mosquitto não iniciou"))
        status, response = self.get_json("/api/start-status")
        self.assertEqual(status, 200)
        self.assertEqual(response, {"estado": "error", "erro": "Mosquitto não iniciou"})

    def test_stop_terminates_process_group_descendants(self):
        with tempfile.TemporaryDirectory() as temporary:
            ready = os.path.join(temporary, "child-ready")
            stopped = os.path.join(temporary, "child-stopped")
            child_code = (
                "import pathlib,signal,sys,time; "
                "ready,stopped=map(pathlib.Path,sys.argv[1:]); "
                "ready.touch(); "
                "signal.signal(signal.SIGTERM,lambda *_:(stopped.touch(),sys.exit(0))); "
                "time.sleep(60)"
            )
            parent_code = (
                "import subprocess,sys; "
                "subprocess.Popen([sys.executable,'-c',sys.argv[1],sys.argv[2],sys.argv[3]]); "
                "print('ready',flush=True); input()"
            )
            process = subprocess.Popen(
                [sys.executable, "-c", parent_code, child_code, ready, stopped],
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                text=True,
                start_new_session=True,
            )
            self.addCleanup(stop, process)
            self.assertEqual(process.stdout.readline().strip(), "ready")
            deadline = time.monotonic() + 2
            while not os.path.exists(ready) and time.monotonic() < deadline:
                time.sleep(0.01)
            self.assertTrue(os.path.exists(ready), "child process did not start")

            stop(process)

            self.assertTrue(os.path.exists(stopped), "child process group was not terminated")
            process.stdout.close()


if __name__ == "__main__":
    unittest.main()
