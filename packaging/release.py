#!/usr/bin/env python3
"""Build release assets from an isolated, explicitly inventoried source snapshot."""
from pathlib import Path
import shutil
import argparse
from datetime import datetime, timezone
import gzip
import hashlib
import json
import os
import platform
import re
import subprocess
import sys
import tarfile
import tempfile


def digest(path, algorithm="sha256"):
    value = hashlib.new(algorithm)
    with Path(path).open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def inventory(root):
    return [{"name": path.relative_to(root).as_posix(), "bytes": path.stat().st_size,
             "sha256": digest(path), "sha1": digest(path, "sha1"),
             "mode": "0755" if path.stat().st_mode & 0o111 else "0644"}
            for path in sorted(Path(root).rglob("*")) if path.is_file()]


def record(path):
    return {"name": Path(path).name, "bytes": Path(path).stat().st_size, "sha256": digest(path)}


def write_json(path, data):
    Path(path).write_text(json.dumps(data, sort_keys=True, indent=2) + "\n")


def write_documents(output, version, source, files, artifacts, dirty, epoch, build=None):
    output = Path(output)
    source_doc = {"schemaVersion": 1, "version": version, "source": source,
                  "workingTreeDirty": dirty, "publishable": not dirty,
                  "snapshotFiles": files, "buildEnvironment": build or {}}
    write_json(output / "SOURCE-MANIFEST.json", source_doc)
    spdx_files = [{"SPDXID": f"SPDXRef-File-{index}", "fileName": "./" + item["name"],
                   "checksums": [{"algorithm": "SHA256", "checksumValue": item["sha256"]},
                                 {"algorithm": "SHA1", "checksumValue": item["sha1"]}],
                   "licenseConcluded": "NOASSERTION", "licenseInfoInFiles": ["NOASSERTION"],
                   "copyrightText": "NOASSERTION",
                   **({"licenseComments": "Vendored Flipper RPC schema: upstream license is not established; NOASSERTION, not covered by OmaFlip's MIT declaration."}
                      if item["name"].startswith("plugin/proto/") else {})}
                  for index, item in enumerate(files)]
    verification = hashlib.sha1("".join(sorted(item["sha1"] for item in files)).encode()).hexdigest()
    snapshot = hashlib.sha256(json.dumps(files, sort_keys=True).encode()).hexdigest()
    sbom = {"spdxVersion": "SPDX-2.3", "dataLicense": "CC0-1.0", "SPDXID": "SPDXRef-DOCUMENT",
            "name": f"OmaFlip-{version}",
            "documentNamespace": f"https://github.com/HowieDuhzit/OmaFlip/spdx/{source['commit']}/{snapshot}",
            "creationInfo": {"created": datetime.fromtimestamp(epoch, timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
                             "creators": ["Tool: OmaFlip-packaging-release"]},
            "packages": [{"SPDXID": "SPDXRef-OmaFlip", "name": "OmaFlip", "versionInfo": version,
                          "downloadLocation": "git+" + source["repository"] + "@" + source["commit"],
                          "filesAnalyzed": True, "packageVerificationCode": {"packageVerificationCodeValue": verification},
                          "licenseConcluded": "NOASSERTION", "licenseDeclared": "NOASSERTION", "copyrightText": "NOASSERTION",
                          "licenseComments": "OmaFlip-authored code is MIT (plugin/LICENSE); the bundled upstream Flipper RPC schema license is unestablished (NOASSERTION). MIT is not asserted for the entire distribution.",
                          "sourceInfo": ("NON-PUBLISHABLE dirty snapshot; HEAD is only its base. " if dirty else "Clean source snapshot. ") +
                          "Exact distributed files in SOURCE-MANIFEST.json. External system libraries are not bundled; versions in buildEnvironment.",
                          "checksums": [{"algorithm": "SHA256", "checksumValue": digest(artifacts[0])}]}],
            "files": spdx_files,
            "relationships": [{"spdxElementId": "SPDXRef-DOCUMENT", "relationshipType": "DESCRIBES", "relatedSpdxElement": "SPDXRef-OmaFlip"}] +
                             [{"spdxElementId": "SPDXRef-OmaFlip", "relationshipType": "CONTAINS", "relatedSpdxElement": item["SPDXID"]} for item in spdx_files]}
    write_json(output / "SBOM.spdx.json", sbom)
    release = {"schemaVersion": 1, "version": version, "source": source,
               "workingTreeDirty": dirty, "publishable": not dirty,
               "artifacts": [record(path) for path in artifacts],
               "releaseDocuments": [record(output / name) for name in ("SOURCE-MANIFEST.json", "SBOM.spdx.json")]}
    write_json(output / "RELEASE-MANIFEST.json", release)
    listed = release["artifacts"] + release["releaseDocuments"] + [record(output / "RELEASE-MANIFEST.json")]
    (output / "SHA256SUMS").write_text("".join(f"{item['sha256']}  {item['name']}\n" for item in sorted(listed, key=lambda x: x["name"])))


def verify_documents(output):
    output = Path(output)
    release = json.loads((output / "RELEASE-MANIFEST.json").read_text())
    source = json.loads((output / "SOURCE-MANIFEST.json").read_text())
    sbom = json.loads((output / "SBOM.spdx.json").read_text())
    if release["schemaVersion"] != 1 or source["schemaVersion"] != 1 or source["source"] != release["source"] or source["version"] != release["version"]:
        raise ValueError("Release/source identity mismatch")
    if sbom["spdxVersion"] != "SPDX-2.3" or len(sbom["packages"]) != 1 or sbom["packages"][0]["versionInfo"] != release["version"] or not sbom["packages"][0]["downloadLocation"].endswith("@" + release["source"]["commit"]):
        raise ValueError("SBOM source identity mismatch")
    listed = {}
    for item in release["artifacts"] + release["releaseDocuments"]:
        name = item["name"]
        if not isinstance(name, str) or Path(name).name != name or name in (".", "..") or "\\" in name or name in listed:
            raise ValueError("Unsafe or duplicate release filename")
        path = output / name
        if path.is_symlink() or not path.is_file() or record(path) != item:
            raise ValueError(f"Release integrity failure: {name}")
        listed[name] = item["sha256"]
    listed["RELEASE-MANIFEST.json"] = digest(output / "RELEASE-MANIFEST.json")
    sums = {}
    for line in (output / "SHA256SUMS").read_text().splitlines():
        match = re.fullmatch(r"([0-9a-f]{64})  ([^\r\n]+)", line)
        if not match or match[2] in sums:
            raise ValueError("Malformed or duplicate checksums")
        sums[match[2]] = match[1]
    if sums != listed:
        raise ValueError("Checksum coverage mismatch")


def run(argv, cwd, capture=False):
    result = subprocess.run(list(map(str, argv)), cwd=cwd, text=True, check=True,
                            stdout=subprocess.PIPE if capture else None)
    return result.stdout.strip() if capture else ""


def archive_tree(root, archive, epoch):
    with Path(archive).open("wb") as raw, gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=epoch) as compressed:
        with tarfile.open(fileobj=compressed, mode="w") as tar:
            for path in [root] + sorted(root.rglob("*")):
                info = tar.gettarinfo(str(path), path.relative_to(root.parent).as_posix())
                info.uid = info.gid = 0
                info.uname = info.gname = ""
                info.mtime = epoch
                info.mode = 0o755 if path.is_dir() or path.stat().st_mode & 0o111 else 0o644
                if path.is_file():
                    with path.open("rb") as handle:
                        tar.addfile(info, handle)
                else:
                    tar.addfile(info)


def verify_head_snapshot(repo, snapshot, tree="HEAD"):
    entries = {}
    raw = run(["git", "ls-tree", "-rz", tree], repo, True)
    for entry in raw.split("\0"):
        if not entry:
            continue
        meta, name = entry.split("\t", 1)
        mode, kind, blob = meta.split()
        entries[name] = (mode, kind, blob)
    for path in sorted(Path(snapshot).rglob("*")):
        if not path.is_file():
            continue
        name = path.relative_to(snapshot).as_posix()
        contents = path.read_bytes()
        blob = hashlib.sha1(f"blob {len(contents)}\0".encode() + contents).hexdigest()
        mode = "100755" if path.stat().st_mode & 0o111 else "100644"
        if entries.get(name) != (mode, "blob", blob):
            raise ValueError(f"Release input does not bind to HEAD: {name}")


def build_release(repo, output, allow_dirty=False):
    if Path(output).is_symlink():
        raise ValueError("Refusing symlink release output directory")
    repo, output = Path(repo).resolve(), Path(output).resolve()
    if output.exists() and any(output.iterdir()):
        raise ValueError(f"Refusing to overwrite nonempty release directory: {output}")
    status = run(["git", "status", "--porcelain", "--untracked-files=all"], repo, True)
    dirty = bool(status) or allow_dirty
    if dirty and not allow_dirty:
        raise ValueError("Release requires a clean tree; --allow-dirty creates NON-PUBLISHABLE preparation assets only")
    remote = run(["git", "remote", "get-url", "origin"], repo, True)
    if remote.startswith("git@github.com:"):
        remote = "https://github.com/" + remote[len("git@github.com:"):]
    remote = remote.removesuffix(".git").rstrip("/")
    if not re.fullmatch(r"https://github\.com/[^/]+/[^/]+", remote):
        raise ValueError("Expected GitHub source repository")
    source = {"repository": remote, "commit": run(["git", "rev-parse", "HEAD"], repo, True),
              "tree": run(["git", "rev-parse", "HEAD^{tree}"], repo, True)}
    epoch = int(os.environ.get("SOURCE_DATE_EPOCH", run(["git", "show", "-s", "--format=%ct", "HEAD"], repo, True)))
    scratch = os.environ.get("TMPDIR", str(Path.home() / ".cache/omaflip"))
    Path(scratch).mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="omaflip-package-", dir=scratch) as temporary:
        stage = Path(temporary)
        plugin = stage / "source"
        stage_sources(repo, plugin)
        version = json.loads((plugin / "manifest.json").read_text())["version"]
        cmake_version = re.search(r"project\(OmaFlip VERSION ([^ ]+)", (plugin / "CMakeLists.txt").read_text())
        if not cmake_version or cmake_version[1] != version:
            raise ValueError("Manifest and CMake versions differ")
        staged_files = inventory(plugin)
        if not allow_dirty:
            verify_head_snapshot(repo, plugin, source["commit"])
        root = stage / f"omaflip-{version}"
        root.mkdir()
        plugin.rename(root / "plugin")
        plugin = root / "plugin"
        build = stage / "build"
        run([sys.executable, plugin / "tests/run", "--build-dir", build, "--build-type", "RelWithDebInfo"], plugin)
        # The binary is built from the exact isolated snapshot, never a possibly stale repo/build.
        (root / "bin").mkdir()
        shutil.copy2(build / "omaflip", root / "bin/omaflip")
        (root / "INSTALL.txt").write_text(f"OmaFlip {version}\n\nplugin/ is the complete buildable source.\nRead plugin/README.md before installation. Existing installer semantics are unchanged.\nDo not overwrite an existing checkout or unmanaged binary.\nFrom plugin/: scripts/build then scripts/install-dev (new plugin destinations only).\nThe optional bin/omaflip was built for linux-{platform.machine()}; requires system Qt6, protobuf and libudev.\n")
        if inventory(plugin) != staged_files:
            raise ValueError("Tests changed packaged source snapshot")
        files = inventory(root)
        environment = {"architecture": platform.machine(), "cmake": run(["cmake", "--version"], repo, True).splitlines()[0],
                       "compiler": run([os.environ.get("CXX", "c++"), "--version"], repo, True).splitlines()[0],
                       "libraries": {name: run(["pkg-config", "--modversion", name], repo, True)
                                     for name in ("Qt6Core", "Qt6Gui", "Qt6Network", "Qt6Test", "protobuf", "libudev")},
                       "buildType": "RelWithDebInfo", "sourceDateEpoch": epoch,
                       "validation": "tests/run (backend, IPC, static; UI not requested)"}
        output.mkdir(parents=True, exist_ok=True)
        archive = output / f"omaflip-{version}-linux-{platform.machine()}.tar.gz"
        archive_tree(root, archive, epoch)
        write_documents(output, version, source, files, [archive], dirty, epoch, environment)
        verify_documents(output)
        print(archive)
        if dirty:
            print("NON-PUBLISHABLE dirty snapshot; rebuild after final commit", file=sys.stderr)
    return archive


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--allow-dirty", action="store_true")
    parser.add_argument("--verify", type=Path, help="Verify an existing release directory without building")
    args = parser.parse_args()
    if args.verify:
        verify_documents(args.verify)
        print(f"PASS: release identities, digests and checksum coverage: {args.verify}")
    else:
        repo = Path(__file__).resolve().parents[1]
        version = json.loads((repo / "manifest.json").read_text())["version"]
        build_release(repo, args.output or repo / "dist" / version, args.allow_dirty)


ENTRIES = ("manifest.json", "BarWidget.qml", "Panel.qml", "Service.qml", "qml",
           "scripts", "packaging", "service", "proto", "udev", "docs", "screenshots",
           "preview.png", "CMakeLists.txt", "README.md", "LICENSE", "CONTRIBUTING.md",
           "CHANGELOG.md", "tests", ".gitignore", ".github")
EXCLUDED = {".git", "build", "dist", "CMakeFiles", "__pycache__", ".pytest_cache",
            ".cache", "CMakeCache.txt", "cmake_install.cmake"}


def stage_sources(root, destination):
    root, destination = Path(root), Path(destination)
    destination.mkdir(parents=True)
    for name in ENTRIES:
        source = root / name
        if not source.exists():
            raise ValueError(f"Required release input missing: {source}")
        paths = [source] + (sorted(source.rglob("*")) if source.is_dir() else [])
        for path in paths:
            relative = path.relative_to(root)
            if any(part in EXCLUDED for part in relative.parts) or path.suffix == ".pyc":
                continue
            if path.is_symlink():
                raise ValueError(f"Refusing release symlink: {relative}")
            target = destination / relative
            if path.is_dir():
                target.mkdir(parents=True, exist_ok=True)
            elif path.is_file():
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(path, target)
            else:
                raise ValueError(f"Not a regular release input: {relative}")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        sys.exit(1)
