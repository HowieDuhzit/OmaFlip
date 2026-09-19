# Changelog

## 1.2.0 — Remote pointer (2026-09-19)

- Map mouse on the remote screen to Flipper buttons: left click OK, right click
  Back, drag for D-pad, wheel for scroll. Swipes follow the displayed
  orientation. The RPC has no pointer coordinates.

## 1.1.0 — Dev loop (unreleased)

- Resolve `ufbt` from PATH, `~/.local/bin`, or `python3 -m ufbt`.
- Install ufbt on demand with `pipx install ufbt` when pipx is present, else
  `python3 -m pip install --user --upgrade ufbt`.
- Remember the Dev project folder and default it to Documents/OmaFlip/projects.
- Record Milestone 1 window/reconnect as confirmed on the attached Momentum device.

## 1.0.0 — Release polish (unreleased)

- Persist notify-on-connect and notify-on-error; emit desktop notifications
  with `device.added`, `device.removed`, and `device.error` through
  `notify-send` (no shell). Categories outside that list are refused.
- Record `rpc.probeMs` and `rpc.pingMs` on a Connected read and show them in
  device details with the OmaFlip version.
- Accessible name on the bar button. Manifest and backend version 1.0.0.
- Add `scripts/package` for a versioned native tarball and `docs/hardware.md`
  for the measured host/device matrix. DFU write stays disabled.

## 0.9.0 — Developer preview (unreleased)

- Add a Dev session: `ufbt` create/build/lint and SDK update (Momentum uses
  `up.momentum-fw.dev`), then RPC deploy of `dist/*.fap` under `/ext/apps`.
- RPC inspector for read-only ping, protobuf, storage, lock, datetime, device,
  power, property, and desktop status, plus audiovisual alert. GPIO writes,
  reboot, factory reset, and DFU flash stay disabled. `ufbt` is not installed
  automatically.

## 0.8.0 — Momentum preview (unreleased)

- Classify firmware origin from `firmware_origin_fork` (Official, Momentum,
  Other). Do not guess origin from a device name or version string.
- Fetch Momentum firmware from `up.momentum-fw.dev` only (release and
  development channels). Official packages stay on `update.flipperzero.one`
  and still require replace-origin confirmation on a Momentum device.
- List `/ext/asset_packs`, download verified `pack_targz` packs, and extract
  them there. Activation stays in on-device Momentum Settings. No settings
  file writes. DFU flashing is still not enabled.

## 0.7.0 — Device management preview (unreleased)

- Add internal-storage backups under `/ext/omaflip_backup` with versioned
  sidecar metadata, restore compatibility checks, and confirmation.
- Fetch the official `directory.json` index, download the `f7` `update_tgz`
  from `update.flipperzero.one`, and verify SHA-256 before install.
- Official apply uploads, extracts, and starts the SD updater only after
  confirmation. Origin mismatch (e.g. Momentum) requires an extra replace
  confirmation. DFU flashing is still not enabled.

## 0.6.0 — Apps preview (unreleased)

- Add an on-demand Apps session: inventory of `/ext/apps` `.fap` and `.js`
  files, launch, remove, drop-install, JS edit/save, and JS run via firmware
  `JS Runner`.
- Catalog APIs stay investigation-only. Official companion apps consume the
  Application Catalog; OmaFlip does not call undocumented catalog endpoints.

## 0.5.0 — CLI preview (unreleased)

- Add an on-demand CLI session that holds the serial port until Close CLI.
- Discover commands from firmware `help`, with history, output filter, save to
  Downloads/OmaFlip, reconnect, and bounded 64 KiB scrollback.
- Stream `log` / `top` / `echo` until Stop sends Ctrl+C. Block `start_rpc_session`
  from the console (use Remote or Files). Confirm power/update/factory-reset.

## 0.4.0 — Files preview (unreleased)

- Add on-demand Flipper filesystem session: list `/ext` (fallback `/`), typed
  text previews, download to Downloads/OmaFlip, drop-to-upload with destination
  review, mkdir/rename/delete, overwrite confirmation, and transfer progress.
- Bound device paths to `/ext`, `/int`, and `/any`. Transfers use official
  storage RPC with 512-byte chunks and `has_next` framing.
- Remote and Files cannot hold the serial port at the same time.

## 0.3.0 — Remote preview (unreleased)

- Add on-demand Flipper screen stream over a held RPC session, torn down when Remote closes.
- Add d-pad / OK / Back input, keyboard arrows, PNG screenshot save/copy/history.
- Decode the official 128×64 u8g2 framebuffer to a crisp scaled PNG.

## 0.2.0 — RPC preview (unreleased)

- Pin official Flipper protobuf schema `1c84fa4` and generate C++ bindings at build time.
- After CLI device/power reads, start a bounded RPC session: ping, protobuf
  version, and storage info for `/ext` and `/int`.
- Show RPC summary and SD free space in the panel. Failures leave CLI data intact.
- Add framing tests for delimited `PB.Main` encode/decode, truncation, and size bounds.

## 0.1.0 — Foundation preview (unreleased)

- Add native Omarchy bar widget, theme-aware device panel, and service lifecycle.
- Add libudev event discovery with descriptor filtering and explicit DFU ambiguity.
- Add bounded CLI device/power reads and actionable errors.
- Add device selection, compact bar mode, auto-connect preferences, and diagnostics.
- Add active-seat udev setup, native build tooling, unit and IPC tests.
- Accept firmware-customized USB product names when the Flipper VID/PID and
  manufacturer descriptor match.
- Physical Flipper acceptance remains required before Milestone 1 is complete.
