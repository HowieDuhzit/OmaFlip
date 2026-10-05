#!/usr/bin/env python3
"""Validate exact archive inventory, safely extract, optionally rebuild/test it."""
import argparse
import importlib.util
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import sys
import tarfile
import tempfile

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
from release import inventory, verify_documents


def safe_extract(archive, destination, expected_root):
    destination = Path(destination)
    if destination.is_symlink() or (destination.exists() and any(destination.iterdir())):
        raise ValueError("Refusing nonempty or symlink extraction destination")
    with tarfile.open(archive, "r:gz") as tar:
        members, seen, total = [], set(), 0
        for member in tar:
            name = member.name
            parts = PurePosixPath(name).parts
            if (not parts or parts[0] != expected_root or name.startswith("/") or "\\" in name
                    or any(part in (".", "..") for part in name.split("/"))
                    or name in seen or not (member.isdir() or member.isfile())
                    or member.mode & 0o7000 or member.size > 512 * 1024 * 1024):
                raise ValueError(f"Unsafe archive member: {name}")
            total += member.size
            if total > 2 * 1024 * 1024 * 1024 or len(members) >= 10000:
                raise ValueError("Archive exceeds release extraction bounds")
            seen.add(name)
            members.append(member)
        # Entire archive validated before any destination writes. No links or devices.
        destination.mkdir(parents=True, exist_ok=True)
        for member in members:
            target = destination / member.name
            if member.isdir():
                target.mkdir(parents=True, exist_ok=True)
            else:
                target.parent.mkdir(parents=True, exist_ok=True)
                handle = tar.extractfile(member)
                if handle is None:
                    raise ValueError(f"Unreadable archive file: {member.name}")
                with handle, target.open("xb") as output:
                    shutil.copyfileobj(handle, output)
                target.chmod(member.mode)
    return destination / expected_root


def verify(directory, destination, rebuild=False, trusted_preflight=None):
    directory = Path(directory).resolve()
    verify_documents(directory)
    release = json.loads((directory / "RELEASE-MANIFEST.json").read_text())
    source = json.loads((directory / "SOURCE-MANIFEST.json").read_text())
    if trusted_preflight:
        # Explicit user-selected installed skill, never a validator from the plugin.
        spec = importlib.util.spec_from_file_location("trusted_release_preflight", trusted_preflight)
        assert spec and spec.loader
        trusted = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = trusted
        spec.loader.exec_module(trusted)
        findings = []
        trusted.validate_release_directory(directory, release["source"], release["version"],
                                           lambda *args: findings.append(args))
        if findings:
            raise ValueError(f"Trusted release preflight rejected assets: {findings}")
        print("PASS: installed trusted release_preflight asset schema", flush=True)
    if len(release["artifacts"]) != 1:
        raise ValueError("Expected exactly one platform archive")
    root = safe_extract(directory / release["artifacts"][0]["name"], destination,
                        "omaflip-" + release["version"])
    files = inventory(root)
    if files != source["snapshotFiles"]:
        raise ValueError("Extracted source/binary inventory differs from SOURCE-MANIFEST.json")
    sbom = json.loads((directory / "SBOM.spdx.json").read_text())
    spdx_files = {item["fileName"].removeprefix("./"): item for item in sbom["files"]}
    if set(spdx_files) != {item["name"] for item in files}:
        raise ValueError("SPDX file coverage differs from exact extracted inventory")
    for item in files:
        sums = {value["algorithm"]: value["checksumValue"] for value in spdx_files[item["name"]]["checksums"]}
        if sums != {"SHA256": item["sha256"], "SHA1": item["sha1"]}:
            raise ValueError("SPDX file integrity mismatch")
    print(f"PASS: safely extracted {len(files)} exact manifested/SPDX files to {root}", flush=True)
    if rebuild:
        env = dict(os.environ, PYTHONDONTWRITEBYTECODE="1")
        subprocess.run([sys.executable, root / "plugin/tests/run", "--build-dir", Path(destination) / "rebuild"],
                       cwd=root / "plugin", check=True, env=env)
        print("PASS: extracted source rebuild + backend/IPC/static checks", flush=True)
    return root


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("release_dir", type=Path)
    parser.add_argument("--extract-to", type=Path, help="Retain extraction/rebuild evidence in a new or empty directory")
    parser.add_argument("--rebuild", action="store_true")
    parser.add_argument("--trusted-preflight", type=Path, help="Explicit path to installed skill's trusted release_preflight.py")
    args = parser.parse_args()
    if args.extract_to:
        verify(args.release_dir, args.extract_to.resolve(), args.rebuild, args.trusted_preflight)
    else:
        scratch = Path(os.environ.get("TMPDIR", Path.home() / ".cache/omaflip"))
        scratch.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="omaflip-extracted-", dir=scratch) as directory:
            verify(args.release_dir, Path(directory), args.rebuild, args.trusted_preflight)


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError, tarfile.TarError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        sys.exit(1)
