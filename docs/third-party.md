# Third-party provenance and licensing

OmaFlip-authored code is licensed under the root MIT LICENSE. That license does
not relicense dependencies, generated code, or third-party protocol definitions.

## Flipper RPC schemas

`proto/` contains unmodified definitions from
https://github.com/flipperdevices/flipperzero-protobuf at full revision
`1c84fa48919cbb71d1cc65236fc0ee36740e24c6`; see `proto/README.md`.
The pinned upstream snapshot publishes no LICENSE file. GitHub's current upstream
license endpoint was also unavailable during release preparation. No license or
redistribution permission is inferred from the absence of a license.

The official open-source ecosystem provides substantial affirmative context:

- Official firmware revision `7f0b6e1c14431708cfde75ae1ba13df59e868041`
  includes this exact schema revision as its `assets/protobuf` submodule:
  https://github.com/flipperdevices/flipperzero-firmware/tree/7f0b6e1c14431708cfde75ae1ba13df59e868041/assets/protobuf
  The firmware itself carries GPLv3 text.
- Official Python bindings at revision
  `412d5183064801e420b9d01443bfc9211a8294a5` declare BSD 3-Clause in
  https://github.com/flipperdevices/flipperzero_protobuf_py/blob/412d5183064801e420b9d01443bfc9211a8294a5/pyproject.toml
  That package references an older schema snapshot.

Those official distribution paths substantiate intended developer use, but do
not expressly identify the license of every file in the separate pinned schema
repository. The release SBOM therefore records NOASSERTION, not a declaration
that the ecosystem is proprietary or that redistribution is definitively unlawful.
Do not substitute another project's GPL/BSD/MIT license without establishing its
scope. The owner remains responsible for the permission/ownership attestation;
upstream clarification or qualified legal review can resolve the remaining scope
question. CMake generates C++ bindings with protoc at build time; those outputs
are not committed.

## System dependencies

Qt 6, Protocol Buffers, libudev/systemd, Quickshell, and Omarchy retain their
upstream licenses. The release archive does not bundle their shared libraries;
it requires the documented system packages. The SPDX inventory and source
manifest cover distributed files, not a claim to inventory the entire desktop.
Use upstream project/package notices when redistributing their code or libraries.

## Optional tools

Developer operations invoke separately installed ufbt and its SDK tooling.
Fliparchy is a separately supplied compatible Flipper application, not bundled or
automatically downloaded by OmaFlip. Omarchy, hyprctl, wpctl, playerctl, notify-send,
and wl-copy are external executable dependencies for their respective features.
Their presence on PATH and their upstream maintenance/provenance are separate
trust boundaries from OmaFlip source verification.

CI pins the checkout Action and container digest, but Arch packages remain rolling
inputs. Release manifests record the actual build environment. Neither a checksum,
a static marketplace result, nor a matching source tag alone establishes a
security audit or bit-for-bit toolchain reproducibility.
