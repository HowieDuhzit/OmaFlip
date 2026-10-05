#!/usr/bin/env python3
"""Isolated real-QML regressions. Run: python tests/test_ui.py -v.

Only Panel.qml and qml/*.qml are staged; Service.qml is never imported.
Uses installed Omarchy Ui/Commons, Qt Quick Test, and Quickshell. Each
process owns a unique scratch config; production shell is never touched.
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[1]
FIXTURES = REPO / "tests/ui"
HOST = Path(os.environ.get("OMAFLIP_TEST_HOST", "/usr/share/omarchy/shell"))


class QmlRegression(unittest.TestCase):
    def run_fixture(self, fixture, engine):
        self.assertIsNotNone(shutil.which(engine), f"Required executable: {engine}")
        self.assertTrue((HOST / "Ui/Panel.qml").exists(), f"Missing installed host: {HOST}")
        scratch = Path(os.environ.get("TMPDIR", Path.home() / ".hermes/cache/scratch"))
        scratch.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="omaflip-ui-", dir=scratch) as directory:
            stage = Path(directory)
            (stage / "qs").mkdir()
            for module in ("Commons", "Ui"):
                (stage / module).symlink_to(HOST / module, target_is_directory=True)
                (stage / "qs" / module).symlink_to(HOST / module, target_is_directory=True)
            shutil.copy2(REPO / "Panel.qml", stage / "Panel.qml")
            shutil.copytree(REPO / "qml", stage / "qml")
            for source in FIXTURES.glob("*.qml"):
                shutil.copy2(source, stage / source.name)
            if engine != "quickshell":
                # Quickshell's plugin is executable-embedded and cannot load in
                # qmltestrunner. Only theme tokens are synthetic here; widgets,
                # views and the installed PanelKeyCatcher remain real QML.
                (stage / "qs/Commons").unlink()
                (stage / "qs/Commons").mkdir()
                for name in ("Color", "Style"):
                    shutil.copy2(FIXTURES / (name + ".qml"), stage / "qs/Commons" / (name + ".qml"))
                (stage / "qs/Commons/qmldir").write_text("module qs.Commons\nsingleton Color 1.0 Color.qml\nsingleton Style 1.0 Style.qml\n")
            artifacts = Path(os.environ.get("OMAFLIP_TEST_ARTIFACTS", scratch / "omaflip-ui-artifacts"))
            artifacts.mkdir(parents=True, exist_ok=True)
            if engine == "quickshell":
                for name in ("panel-overview.png", "panel-remote.png"):
                    (artifacts / name).unlink(missing_ok=True)
            env = dict(os.environ, QT_QPA_PLATFORM="wayland" if engine == "quickshell" else "offscreen", QT_QUICK_BACKEND="software",
                       QML_IMPORT_PATH=str(stage), NO_COLOR="1", OMAFLIP_TEST_ARTIFACTS=str(artifacts))
            command = ([engine, "--no-color", "--path", str(stage / fixture)] if engine == "quickshell"
                       else [engine, "-input", str(stage / fixture), "-import", str(stage), "-maxwarnings", "0"])
            try:
                result = subprocess.run(command, cwd=stage, env=env, text=True,
                                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=45)
            except subprocess.TimeoutExpired as error:
                partial = error.stdout or b""
                if isinstance(partial, bytes):
                    partial = partial.decode(errors="replace")
                (artifacts / (fixture + ".log")).write_text(partial)
                self.fail(f"{fixture} timed out; log: {artifacts / (fixture + '.log')}\n{partial}")
            output = result.stdout
            artifacts = Path(env["OMAFLIP_TEST_ARTIFACTS"])
            (artifacts / (fixture + ".log")).write_text(output)
            print(output, flush=True)
            print(f"Log: {artifacts / (fixture + '.log')}", flush=True)
            self.assertEqual(result.returncode, 0, output)
            if engine == "quickshell":
                self.assertIn("OMAFLIP_PANEL_DONE", output)
                self.assertNotIn("OMAFLIP_FAIL", output)
                for name in ("panel-overview.png", "panel-remote.png"):
                    image = artifacts / name
                    self.assertTrue(image.exists(), f"Missing actual render: {image}")
                    self.assertEqual(image.read_bytes()[:8], b"\x89PNG\r\n\x1a\n")
            else:
                self.assertIn("0 failed", output)
            # Binding/JS errors must not silently pass a smoke test.
            bad = [line for line in output.splitlines() if any(token in line for token in
                   ("ReferenceError:", "TypeError:", "Binding loop", "Cannot assign", "Unable to assign", "is not a type", "QWARN", "WARN scene"))]
            self.assertFalse(bad, "\n".join(bad))

    def test_hosted_panel(self):
        self.run_fixture("panel_smoke.qml", "quickshell")

    def test_views_and_keyboard(self):
        self.run_fixture("tst_views.qml", "/usr/lib/qt6/bin/qmltestrunner")


if __name__ == "__main__":
    unittest.main()
