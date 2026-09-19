# Upstream research

Inspected 2026-09-18. Local Omarchy **4.0.4-1**, Qt **6.11.2**, libudev **261**.
Repository snapshots below were inspected outside the project; none are vendored.

## Omarchy

- [Plugin authoring guide](https://plugins.omarchy.org/develop.html).
- [Official shell contract](https://github.com/omacom/omarchy/blob/quattro/shell/README.md).
- Installed `shell/plugins/panels/clock/{BarWidget,Panel}.qml`,
  `plugins/services/media/manifest.json`, `plugins/services/battery/Service.qml`,
  `services/PluginShellApi.qml`, `Ui/{BarWidget,Panel,KeyboardPanel,PanelKeyCatcher}.qml`.
- The manifest uses schema 1. Nested widget panels are not separate `panel`
  entry points. A service can share the ID with its bar widget. The scoped
  `serviceFor` and `updateEntryInline` interfaces are used directly. No install
  script hook is invented; the native backend has an explicit build step.

## Flipper firmware and Linux tooling

- [User documentation](https://docs.flipper.net/zero),
  [CLI documentation](https://docs.flipper.net/zero/development/cli),
  [developer documentation](https://developer.flipper.net/flipperzero/doxygen/).
- [Firmware revision 7f0b6e1](https://github.com/flipperdevices/flipperzero-firmware/tree/7f0b6e1c14431708cfde75ae1ba13df59e868041).
  `applications/services/cli/cli_main_commands.c` registers `device_info` and
  `info power`. `targets/f7/furi_hal/furi_hal_info.c` and `furi_hal_power.c`
  define actual field names. `lib/toolbox/cli/shell/cli_shell_line.c` defines
  the prompt. `applications/services/rpc/rpc.c` uses delimited nanopb messages.
- Upstream `scripts/flipper/utils/cdc.py` uses pyserial discovery; useful as a
  reference, but the plugin needs event-driven lifecycle detection instead.
- [uFBT](https://github.com/flipperdevices/flipperzero-ufbt) is the upstream
  compact application build/development tool to integrate in Milestone 9.

## qFlipper and protobuf

- [qFlipper revision 1d26683](https://github.com/flipperdevices/qFlipper/tree/1d26683ff5b751f219a4e40443b7c8355eb39469).
  `backend/deviceregistry.cpp` verifies serial VID/PID **0483:5740**,
  manufacturer **Flipper Devices Inc.**, product **Flipper Control Virtual ComPort**;
  DFU is **0483:df11**. `helper/serialinithelper.cpp` and
  `rpc/skipmotdoperation.cpp` show DTR session initialization. `startrpcoperation.cpp`
  shows the CLI-to-RPC transition. `plugins/flipperproto0/mainrequest.cpp` uses
  delimited encoding. The firmware's messages, not UI behavior, define RPC.
- [Protobuf revision 1c84fa4](https://github.com/flipperdevices/flipperzero-protobuf/tree/1c84fa48919cbb71d1cc65236fc0ee36740e24c6).
  `flipper.proto` supplies the envelope and errors; `system.proto` defines device,
  power, ping, version, and update requests; `storage.proto`, `gui.proto`, and
  `application.proto` are the later slice boundaries. No guessed bindings in M1.
- qFlipper's udev examples use active-seat `uaccess`. OmaFlip narrows its serial
  rule to actual Flipper descriptors and defers DFU write permission until a
  verified recovery workflow exists.

## Momentum

- [Momentum wiki](https://momentum-fw.dev/wiki/).
- [Firmware revision d3f89df](https://github.com/Next-Flip/Momentum-Firmware/tree/d3f89dfe2ef6b01839201598e9be1590cba80322).
  `targets/f7/furi_hal/furi_hal_info.c` exposes firmware-origin fields.
  [AssetPacks.md](https://github.com/Next-Flip/Momentum-Firmware/blob/d3f89dfe2ef6b01839201598e9be1590cba80322/documentation/file_formats/AssetPacks.md)
  documents `SD/asset_packs/<pack>/Anims` and `Icons` and switching through
  on-device Momentum Settings. This does not establish a supported remote
  activation API. No undocumented settings writes are justified by that source.

## Evidence limits

Documentation was cross-checked against local installed shell and cloned source.
Source compatibility does not replace a physical device test. Some Flipper docs
render sparsely in text fetchers; command and field implementation details above
come from firmware source. Host-level USB inventory showed no Flipper during
initial testing. No physical connection, Momentum behavior, or DFU transition
is claimed as verified.
