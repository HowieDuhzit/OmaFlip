#!/usr/bin/env python3
"""Installer filesystem regressions with isolated HOME and call-recording shell stubs.

--host additionally exercises the installed Omarchy removal script in the isolated
HOME. It does not contact or modify the production shell or prove shell lifecycle.
"""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
ID = "io.github.howieduhzit.omaflip"
HOST_REMOVE = Path("/usr/share/omarchy/bin/omarchy-plugin-remove")
HOST_REQUESTED = "--host" in sys.argv
if HOST_REQUESTED:
    sys.argv.remove("--host")


class InstallRegression(unittest.TestCase):
    def setUp(self):
        scratch = Path(os.environ.get("TMPDIR", Path.home() / ".hermes/cache/scratch"))
        scratch.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix="omaflip-install-test-", dir=scratch)
        self.addCleanup(self.temp.cleanup)
        self.home = Path(self.temp.name)
        self.target = self.home / ".config/omarchy/plugins" / ID
        self.bin = self.home / ".local/lib/omaflip/omaflip"
        self.bin.parent.mkdir(parents=True)
        self.bin.write_text("#!/bin/sh\nexit 0\n")
        self.bin.chmod(0o755)
        tools = self.home / "test-tools"
        tools.mkdir()
        stub = """#!/usr/bin/env python3
import json, os, sys
from pathlib import Path
with Path(os.environ['TEST_CALLS']).open('a') as f:
    f.write(json.dumps([Path(sys.argv[0]).name, *sys.argv[1:]]) + '\\n')
if sys.argv[1:] == ['shell', 'listPlugins']:
    print('[]')
"""
        for name in ("omarchy", "omarchy-shell"):
            path = tools / name
            path.write_text(stub)
            path.chmod(0o755)
        self.log = self.home / "calls.jsonl"
        self.env = dict(os.environ, HOME=str(self.home), XDG_CONFIG_HOME=str(self.home / ".config"),
                        PATH=str(tools) + os.pathsep + os.environ["PATH"], TEST_CALLS=str(self.log),
                        PYTHONDONTWRITEBYTECODE="1")
        self.settings = self.home / ".config/omarchy/shell.json"
        self.settings.parent.mkdir(parents=True, exist_ok=True)
        self.settings.write_text('{"TEST_ONLY":"unrelated configuration"}\n')
        self.initial_settings = self.settings.read_bytes()

    def install(self):
        return subprocess.run(["bash", str(ROOT / "scripts/install-dev")], cwd=ROOT,
                              env=self.env, capture_output=True, text=True, timeout=30)

    def calls(self):
        return [json.loads(line) for line in self.log.read_text().splitlines()] if self.log.exists() else []

    def test_fresh_install_copies_release_surface_and_preserves_settings(self):
        result = self.install()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        for name in ("manifest.json", "Panel.qml", "Service.qml", "preview.png", "qml/InputField.qml",
                     "qml/ToolButton.qml", "packaging/release.py", "tests/run", "service/companion.cpp"):
            self.assertEqual((self.target / name).read_bytes(), (ROOT / name).read_bytes(), name)
        self.assertFalse((self.target / "build").exists())
        self.assertFalse((self.target / ".git").exists())
        self.assertEqual(self.settings.read_bytes(), self.initial_settings)
        self.assertIn(["omarchy", "plugin", "enable", ID, "--section", "right"], self.calls())

    def test_existing_install_and_modified_files_are_not_overwritten(self):
        self.target.mkdir(parents=True)
        marker = self.target / "user-modified.qml"
        marker.write_text("TEST_ONLY custom file\n")
        before = marker.read_bytes()
        result = self.install()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Refusing to overwrite", result.stderr)
        self.assertEqual(marker.read_bytes(), before)
        self.assertEqual(self.calls(), [])
        self.assertEqual(self.settings.read_bytes(), self.initial_settings)

    def test_missing_backend_does_not_create_install(self):
        self.bin.unlink()
        result = self.install()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Run scripts/build first", result.stderr)
        self.assertFalse(self.target.exists())
        self.assertEqual(self.calls(), [])


if HOST_REQUESTED:
    def test_host_removal_backs_up_modified_install_without_touching_other_state(self):
        self.assertTrue(HOST_REMOVE.exists(), "--host requires installed Omarchy removal tooling")
        self.assertEqual(self.install().returncode, 0)
        modified = self.target / "TEST_ONLY-user-file.txt"
        modified.write_text("preserve this modification\n")
        neighbor = self.target.parent / "TEST_ONLY-unrelated-plugin"
        neighbor.mkdir()
        (neighbor / "keep.txt").write_text("unrelated\n")
        result = subprocess.run(["bash", str(HOST_REMOVE), ID, "--yes"], env=self.env,
                                capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertFalse(self.target.exists())
        backups = list(self.target.parent.glob("." + ID + ".bak.*"))
        self.assertEqual(len(backups), 1)
        self.assertEqual((backups[0] / modified.name).read_text(), "preserve this modification\n")
        self.assertEqual((neighbor / "keep.txt").read_text(), "unrelated\n")
        self.assertEqual(self.settings.read_bytes(), self.initial_settings)
        self.assertTrue(self.bin.exists(), "documented user-local backend leftover remains")
    setattr(InstallRegression, "test_host_removal_backs_up_modified_install_without_touching_other_state",
            test_host_removal_backs_up_modified_install_without_touching_other_state)


if __name__ == "__main__":
    unittest.main()
