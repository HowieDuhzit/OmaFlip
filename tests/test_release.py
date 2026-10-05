#!/usr/bin/env python3
"""Portable release regression tests; native extraction rebuild is an explicit gate."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def helper():
    path = ROOT / "packaging/release.py"
    assert path.is_file(), "Missing source-bound release helper"
    spec = importlib.util.spec_from_file_location("release", path)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class ReleaseTests(unittest.TestCase):
    def test_ci_trusts_only_the_exact_checked_out_workspace(self):
        workflow = (ROOT / ".github/workflows/backend.yml").read_text()
        trust = 'git config --global --add safe.directory "$GITHUB_WORKSPACE"'
        self.assertIn(trust, workflow)
        self.assertLess(workflow.index("uses: actions/checkout@"), workflow.index(trust))
        self.assertLess(workflow.index(trust), workflow.index("run: tests/run"))
        self.assertNotIn('safe.directory "*"', workflow)

    def test_clean_source_identity_rejects_files_not_bound_to_head(self):
        release = helper()
        import subprocess
        with tempfile.TemporaryDirectory() as directory:
            repo, destination = Path(directory) / "repo", Path(directory) / "plugin"
            repo.mkdir()
            destination.mkdir()
            subprocess.run(["git", "init", "-q", repo], check=True)
            (repo / "README.md").write_text("bound source")
            subprocess.run(["git", "-C", repo, "add", "README.md"], check=True)
            tree = subprocess.check_output(["git", "-C", repo, "write-tree"], text=True).strip()
            (destination / "README.md").write_text("bound source")
            release.verify_head_snapshot(repo, destination, tree)
            (destination / "README.md").write_text("not committed source")
            with self.assertRaisesRegex(ValueError, "HEAD"):
                release.verify_head_snapshot(repo, destination, tree)

    def test_staged_sources_include_build_inputs_preview_and_docs(self):
        release = helper()
        with tempfile.TemporaryDirectory() as directory:
            destination = Path(directory) / "plugin"
            release.stage_sources(ROOT, destination)
            for name in ("service/backend.cpp", "service/companion.cpp", "preview.png",
                         "README.md", "CMakeLists.txt", "proto/flipper.proto", "tests/run"):
                self.assertTrue((destination / name).is_file(), name)
            self.assertFalse((destination / "build").exists())
            self.assertFalse((destination / ".git").exists())
            self.assertFalse(any(p.name == "__pycache__" for p in destination.rglob("*")))

    def test_release_documents_match_trusted_preflight_and_detect_tampering(self):
        release = helper()
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            artifact = output / "omaflip-test.tar.gz"
            artifact.write_bytes(b"test artifact")
            source = {"repository": "https://github.com/HowieDuhzit/OmaFlip",
                      "commit": "1" * 40, "tree": "2" * 40}
            release.write_documents(output, "1.3.0", source, [], [artifact], False, 0)
            release.verify_documents(output)
            artifact.write_bytes(b"tampered artifact")
            with self.assertRaisesRegex(ValueError, "integrity"):
                release.verify_documents(output)

    def test_extraction_refuses_symlinks_and_parent_escape(self):
        import io
        import tarfile
        path = ROOT / "packaging/verify_archive.py"
        self.assertTrue(path.is_file(), "Missing bounded release extractor")
        spec = importlib.util.spec_from_file_location("verify_archive", path)
        assert spec and spec.loader
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name, symlink in (("omaflip-1.3.0/../../escape", False), ("omaflip-1.3.0/link", True)):
                archive = root / "unsafe.tar.gz"
                with tarfile.open(archive, "w:gz") as tar:
                    info = tarfile.TarInfo(name)
                    if symlink:
                        info.type, info.linkname = tarfile.SYMTYPE, "/etc/passwd"
                        tar.addfile(info)
                    else:
                        info.size = 1
                        tar.addfile(info, io.BytesIO(b"x"))
                with self.assertRaises(ValueError):
                    module.safe_extract(archive, root / "extracted", "omaflip-1.3.0")


if __name__ == "__main__":
    unittest.main()
