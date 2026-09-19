"""Real backend process tests; no physical device required, no production mocks."""
import json
import os
import pathlib
import subprocess
import tempfile
import unittest

BINARY = pathlib.Path(__file__).resolve().parents[1] / "build" / "omaflip"


class IpcTests(unittest.TestCase):
    def test_snapshot_and_owner_shutdown(self):
        with tempfile.TemporaryDirectory(prefix="omaflip-test-") as runtime:
            env = dict(os.environ, XDG_RUNTIME_DIR=runtime)
            result = subprocess.run(
                [BINARY, "--stdio"], input='{"op":"snapshot"}\n{"op":"invalid"}\n',
                text=True, capture_output=True, env=env, timeout=5, check=True,
            )
            events = [json.loads(line) for line in result.stdout.splitlines()]
            self.assertGreaterEqual(len(events), 3)
            self.assertEqual(events[0]["protocol"], 1)
            self.assertEqual(events[-1]["error"]["code"], "invalid_request")
            self.assertFalse(pathlib.Path(runtime, "omaflip.lock").exists())

    def test_bad_input_is_contained(self):
        with tempfile.TemporaryDirectory(prefix="omaflip-test-") as runtime:
            result = subprocess.run(
                [BINARY, "--stdio"], input='not json\n{"op":"snapshot"}\n', text=True,
                capture_output=True, timeout=5, env=dict(os.environ, XDG_RUNTIME_DIR=runtime),
            )
            self.assertEqual(result.returncode, 0)
            self.assertIn("Invalid JSON", result.stderr)
            self.assertEqual(json.loads(result.stdout.splitlines()[-1])["type"], "snapshot")

    def test_exclusive_backend(self):
        with tempfile.TemporaryDirectory(prefix="omaflip-test-") as runtime:
            env = dict(os.environ, XDG_RUNTIME_DIR=runtime)
            owner = subprocess.Popen([BINARY, "--stdio"], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env)
            try:
                self.assertEqual(json.loads(owner.stdout.readline())["protocol"], 1)
                other = subprocess.run([BINARY, "--stdio"], input="", text=True, capture_output=True, timeout=3, env=env)
                self.assertEqual(other.returncode, 1)
                self.assertIn("Another OmaFlip backend", other.stderr)
            finally:
                owner.communicate(timeout=3)

    def test_service_termination_cleans_up(self):
        with tempfile.TemporaryDirectory(prefix="omaflip-test-") as runtime:
            process = subprocess.Popen(
                [BINARY, "--stdio"], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                stderr=subprocess.PIPE, env=dict(os.environ, XDG_RUNTIME_DIR=runtime),
            )
            try:
                self.assertEqual(json.loads(process.stdout.readline())["type"], "snapshot")
                process.terminate()
                process.communicate(timeout=3)
                self.assertEqual(process.returncode, 0)
                self.assertFalse(pathlib.Path(runtime, "omaflip.lock").exists())
            finally:
                if process.poll() is None:
                    process.kill()
                    process.communicate()


if __name__ == "__main__":
    unittest.main()
