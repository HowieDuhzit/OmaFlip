# Release assets

Run `tests/run` for the required portable gate: QML/JSON/Python/shell parsing,
release regressions, a fresh CMake backend build, QtTest and real-process IPC.
Required tools are Python 3, Bash, Git, CMake, a C++20 compiler, pkg-config,
Qt6 Core/Gui/Network/Test (6.6+), Qt6 qmlformat, libudev and protobuf/protoc.
On Arch the parser is in `qt6-declarative`; the runner prefers the Qt6 binary
rather than a Qt5 `qmlformat` on PATH. `OMAFLIP_QMLFORMAT` overrides its location.
Missing required tools fail; they never silently skip tests. `--static` is an
explicit subset, not release/backend acceptance. `tests/run --ui` additionally
requires the installed Omarchy shell, Qt Quick Test, Quickshell and Wayland.
It does not restart or modify the production shell. Physical-device and
installation/lifecycle evidence remain separate host gates.

```sh
# Clean committed source; does NOT run scripts/build or install anything.
scripts/package --output /absolute/path/to/new-empty-release-directory
python3 packaging/release.py --verify /absolute/path/to/new-empty-release-directory
(cd /absolute/path/to/new-empty-release-directory && sha256sum --check SHA256SUMS)
python3 packaging/verify_archive.py /absolute/path/to/new-empty-release-directory \
  --extract-to /absolute/path/to/new-empty-extraction-directory --rebuild
```

The default output is `dist/<manifest-version>/`; a nonempty output directory
is never overwritten. Manifest and CMake versions must match. The package
builds/tests an isolated allowlisted snapshot, including `service/`, `proto/`,
all root plugin QML/docs, `preview.png`, tests and build/packaging scripts.
It never trusts a stale `build/omaflip`. Git metadata, builds, caches and symlinks
are excluded/rejected. Extraction is bounded, accepts only regular files and
directories under the expected root, checks every file against the manifest
and SPDX inventory, then optionally compiles/tests the extracted sources.
The extracted archive retains the existing installer scripts without changes;
review the README and destination ownership before installing.

Assets are one `omaflip-<version>-linux-<actual-host-architecture>.tar.gz`,
`RELEASE-MANIFEST.json`, `SOURCE-MANIFEST.json`, `SBOM.spdx.json`, and `SHA256SUMS`.
The release/source identity has exactly repository, full commit and Git tree.
The source manifest also inventories the exact distributed files, sizes, modes,
SHA-256 and SHA-1, and records compiler/CMake/system-library versions. The single
SPDX 2.3 package contains file checksums, relationships and a verification code.
The distribution-level license declaration is `NOASSERTION`: OmaFlip-authored
code is MIT, but the bundled third-party Flipper RPC schema license is not
established and is explicitly marked `NOASSERTION`, not covered by OmaFlip's MIT.
External system libraries are not bundled or purported to be separate packaged
components. Checksums establish integrity, not independently authenticated trust.
Gzip/tar timestamps and ownership are normalized using `SOURCE_DATE_EPOCH`
(default: source commit timestamp), but native compiler/debug paths and rolling
system libraries mean bit-for-bit binary reproducibility is **not** claimed.

Before publication, validate using the installed trusted release skill, not a
validator in this checkout:

```sh
python3 packaging/verify_archive.py /absolute/path/to/release-directory \
  --trusted-preflight /absolute/path/to/installed/omarchy-plugin-release/scripts/release_preflight.py
# Also run the complete installed release_preflight against the final clean tag,
# including downloaded draft assets, as documented by that skill.
```

`--allow-dirty` explicitly permits preparation assets only. Both manifests mark
these `publishable: false` / `workingTreeDirty: true`, and SPDX sourceInfo states
that HEAD is only the base, not the distributed snapshot identity. Every exact
snapshot file is independently hashed. Rebuild after the parent commits the
final version/preview/docs: never publish preparation assets as a clean tag.
For clean assets each packaged file is checked against the selected Git tree's
blob and executable mode, including refusal of ignored/untracked source inputs.

CI uses upstream checkout v4.3.1 commit
`34e114876b0b11c390a56381ad16ebd13914f8d5`, verified with upstream `git ls-remote`.
The official Docker Hub `library/archlinux:base-devel` OCI index is pinned to
`sha256:51dd3d24f7fba779e7c471caeee7804c50e8c134ad948e19685a1c83a42facc3`,
verified by registry response header and SHA-256 of its actual manifest bytes.
Its linux/amd64 image is
`sha256:8edb44f40a0cb5d3e44ef474f02780098ce0084e591c8641bdc01f02f185cc40`.
Arch `pacman -Syu` dependencies still roll: this pins the executable base image
and action, not the whole toolchain or transitive package supply chain.
