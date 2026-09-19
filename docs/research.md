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
  `info power`, plus `help`/`log`/`top`/`echo`. `log` streams until Ctrl+C
  (ETX 0x03); `log ?` prints levels and returns. `targets/f7/furi_hal/furi_hal_info.c` and `furi_hal_power.c`
  define actual field names. `lib/toolbox/cli/shell/cli_shell_line.c` defines
  the prompt. `applications/services/rpc/rpc.c` uses delimited nanopb messages.
- Upstream `scripts/flipper/utils/cdc.py` uses pyserial discovery; useful as a
  reference, but the plugin needs event-driven lifecycle detection instead.
- [uFBT](https://github.com/flipperdevices/flipperzero-ufbt) is the upstream
  compact application build tool. Documented commands used by OmaFlip:
  `ufbt` (build), `ufbt create APPID=<id>`, `ufbt lint`, `ufbt update`
  `--channel=release|dev` and optional `--index-url`. `ufbt flash` /
  `flash_usb` are not wired. Deploy uses Flipper storage RPC plus
  `app_start_request`, not `ufbt launch`, so the inspector can keep the port.

## qFlipper and protobuf

- [qFlipper revision 1d26683](https://github.com/flipperdevices/qFlipper/tree/1d26683ff5b751f219a4e40443b7c8355eb39469).
  `backend/deviceregistry.cpp` verifies serial VID/PID **0483:5740**,
  manufacturer **Flipper Devices Inc.**, product **Flipper Control Virtual ComPort**;
  DFU is **0483:df11**. OmaFlip matches VID/PID plus manufacturer only; Momentum
  on this hardware advertised the configured device name as the USB product
  string, so the stock product text would reject a real Flipper.
  `helper/serialinithelper.cpp` and
  `rpc/skipmotdoperation.cpp` show DTR session initialization. `startrpcoperation.cpp`
  shows the CLI-to-RPC transition. `plugins/flipperproto0/mainrequest.cpp` uses
  delimited encoding. The firmware's messages, not UI behavior, define RPC.
- [Protobuf revision 1c84fa4](https://github.com/flipperdevices/flipperzero-protobuf/tree/1c84fa48919cbb71d1cc65236fc0ee36740e24c6).
  Pinned under `proto/`. `flipper.proto` is the `PB.Main` envelope;
  `system.proto` covers ping, protobuf version, device, and power;
  `storage.proto` covers info, list, stat, read, write, mkdir, rename, and
  delete. Official firmware `rpc_storage.c` lists `/` as `any`/`int`/`ext`,
  paginates list with `has_next`, reads/writes 512-byte chunks (`MAX_DATA_SIZE`),
  and opens writes with `FSOM_CREATE_ALWAYS` (host-side overwrite confirmation
  is required). GUI/application messages are present because the envelope
  imports them. `application.proto` `StartRequest` is consumed by firmware
  `rpc_system_app_start_process`, which calls `loader_start(name, args)`. FAP
  launch uses the `.fap` path as `name`. Official `js_app` is named **JS Runner**
  and takes the script path as `args`. CLI `js <path>` is a separate console
  command. That snapshot had no LICENSE file. qFlipper starts RPC with
  `start_rpc_session\r` and waits for the echoed command plus newline, then
  length-delimited nanopb/`PB.Main` frames.
- qFlipper's udev examples use active-seat `uaccess`. OmaFlip narrows its serial
  rule to actual Flipper descriptors and defers DFU write permission until a
  verified recovery workflow exists.

## Official firmware index

- Documented by Flipper: `https://update.flipperzero.one/firmware/directory.json`.
  Channels are `development`, `release-candidate`, and `release`. Each version
  lists files with `url`, `target` (`f7` / `f18`), `type` (`update_tgz`), and
  `sha256`. OmaFlip fetches this only on an explicit Check, allows HTTPS to
  `update.flipperzero.one` only, and refuses to apply a package whose hash
  does not match. SD updater uses `update.fuf` then reboot UPDATE. DFU write
  remains disabled.

## Application catalog

- [flipper-application-catalog](https://github.com/flipperdevices/flipper-application-catalog)
  hosts `manifest.yml` files only. Built binaries go to Flipper Application
  Mirror and are consumed by official iOS/Android companion apps and Flipper
  Lab. There is no documented public REST install API for third-party desktop
  plugins. OmaFlip does not call catalog HTTP endpoints; install is a local
  `.fap`/`.js` write under `/ext/apps`.

## Momentum

- [Momentum wiki](https://momentum-fw.dev/wiki/).
- [Firmware revision d3f89df](https://github.com/Next-Flip/Momentum-Firmware/tree/d3f89dfe2ef6b01839201598e9be1590cba80322).
  `targets/f7/furi_hal/furi_hal_info.c` exposes firmware-origin fields.
  OmaFlip classifies origin from `firmware_origin_fork` only (Official /
  Momentum / Other); it does not infer origin from USB product or version.
- Firmware index: `https://up.momentum-fw.dev/firmware/directory.json`
  (same channel/file shape as the official indexer). OmaFlip allows HTTPS to
  `up.momentum-fw.dev` only, uses `release` and `development` (not PR
  branches), downloads `update_tgz`, and verifies SHA-256. SD updater path
  is `f7-update-<filename-rest>/update.fuf` from the tgz name.
- Asset packs: `https://up.momentum-fw.dev/asset-packs/directory.json` lists
  `pack_targz` files with SHA-256. [AssetPacks.md](https://github.com/Next-Flip/Momentum-Firmware/blob/d3f89dfe2ef6b01839201598e9be1590cba80322/documentation/file_formats/AssetPacks.md)
  documents `SD/asset_packs/<pack>/Anims` and `Icons` and switching through
  on-device Momentum Settings. This does not establish a supported remote
  activation API. No undocumented settings writes are justified by that source.

## Evidence limits

Documentation was cross-checked against local installed shell and cloned source.
Source compatibility does not replace a physical device test. Some Flipper docs
render sparsely in text fetchers; command and field implementation details above
come from firmware source. A later physical Momentum pass verified customized USB product names, CLI
device/power reads, RPC ping/version/storage, and the installed Connected panel.
DFU transition was not exercised.
