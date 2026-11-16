import sqlite3
import tempfile
import unittest
from pathlib import Path

from visualization.server import history


class HistoryTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.database = Path(self.temporary.name) / "historico.db"
        with sqlite3.connect(self.database) as connection:
            connection.executescript("""
                CREATE TABLE runs (id INTEGER PRIMARY KEY);
                CREATE TABLE robot_events (id INTEGER PRIMARY KEY, run_id INTEGER,
                    robot_id INTEGER, cargo_id INTEGER, mission_id INTEGER,
                    description TEXT, occurred_at_ms INTEGER);
                CREATE TABLE cargo_members (run_id INTEGER, mission_id INTEGER, robot_id INTEGER,
                    PRIMARY KEY (run_id, mission_id, robot_id));
                CREATE TABLE cargo_transports (run_id INTEGER, mission_id INTEGER);
                CREATE TABLE robot_event_members (event_id INTEGER, robot_id INTEGER,
                    PRIMARY KEY (event_id, robot_id));
                INSERT INTO runs VALUES (1), (2);
                INSERT INTO robot_events VALUES
                    (1, 1, -1, 1, 7, 'Coleta antiga', 1000),
                    (2, 2, 1, 0, 0, 'Recarga', 2000),
                    (3, 2, 2, 0, 0, 'Falha de outro robô', 3000),
                    (4, 2, -1, 1, 7, 'Coleta da equipe', 4000),
                    (5, 2, -1, 2, 8, 'Entrega de outra equipe', 5000),
                    (6, 2, 2, 1, 7, 'Movimento de outro integrante', 6000),
                    (7, 2, -1, 3, 9, 'Missão cancelada antes da coleta', 7000);
                INSERT INTO cargo_members VALUES (1, 7, 1), (2, 7, 1), (2, 7, 2), (2, 8, 3);
                INSERT INTO robot_event_members VALUES (1, 1), (4, 1), (4, 2), (5, 3), (7, 1);
                INSERT INTO cargo_transports VALUES (1, 7), (2, 7), (2, 8);
            """)

    def test_robot_history_includes_team_events_without_other_robots_events(self):
        result = history(self.database, {"robo": ["1"]})
        self.assertEqual([item["id"] for item in result["itens"]], [7, 4, 2])
        self.assertFalse(result["tem_mais"])

    def test_fleet_history_preserves_team_events_once_and_excludes_previous_runs(self):
        result = history(self.database, {})
        self.assertEqual([item["id"] for item in result["itens"]], [7, 6, 5, 4, 3, 2])

    def test_transport_history_filters_by_membership(self):
        result = history(self.database, {"visao": ["cargas"], "robo": ["1"]})
        self.assertEqual(len(result["itens"]), 1)
        self.assertEqual(result["itens"][0]["mission_id"], 7)
        self.assertIn("1", result["itens"][0]["robos"])
