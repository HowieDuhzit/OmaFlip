# Architecture

```text
Omarchy / existing Quickshell process
  BarWidget.qml -> Panel.qml -> qml/Action.qml, qml/InfoRow.qml
        | scoped shell.serviceFor(plugin ID)
  Service.qml (one instance, not keepLoaded)
        | one child process, newline-delimited JSON over stdin/stdout
  omaflip --stdio (C++20, QtCore event loop)
        |-- Discovery: libudev monitor + QSocketNotifier, USB/TTY enumeration
        |-- Backend: inventory, selection, centralized state transitions
        `-- SerialProbe per active read: POSIX termios, QSocketNotifier, deadlines
```

## Decisions

QtCore is already a native runtime dependency of the desktop. C++ supports
libudev, POSIX descriptors, and later generated official protobuf bindings
without an embedded web runtime. qFlipper's implementation was studied for
transport behavior; its application/backend is not embedded or copied.
Qt SerialPort is unnecessary for the small Linux-only foundation. The shell
never loads a custom native QML module, keeping protocol failures out of the
shell process. A Qt Test pseudoterminal test exercises the actual serial code.

The plugin declares `bar-widget` and `service`, with its details panel nested
inside the widget. Omarchy's `Ui.BarWidget`, `Ui.Panel`, `KeyboardPanel`, and
`PanelKeyCatcher` provide the normal lifecycle, geometry, dismissal, and theme.
Service lookup is scoped to the plugin ID. Replacement bars lack this capability
by upstream design. No second Quickshell process or independent systemd service
is needed. A user-runtime `QLockFile` rejects accidental duplicate backends.

## Discovery and identity

The monitor is subscribed before startup enumeration. It listens to processed
udev events for USB devices and TTY nodes. Event batches trigger enumeration;
there is no repeating timer and no `/dev` polling. USB identity is filtered by
VID, PID, and manufacturer. Custom firmware may replace the stock USB product
string; that text is not required. The available udev devlinks
prefer `/dev/serial/by-id/`; the verified TTY devnode is the fallback.

The in-memory key is the physical USB sysfs path, which prevents duplicate
serial descriptors from merging devices. The persistent preferred identity
uses the USB serial, falling back to physical sysfs location if absent. That
fallback is port-dependent, not a promise of global identity. Ambiguous duplicate
serials do not auto-select. Serial and DFU identities are intentionally distinct;
STM32 DFU VID/PID alone cannot prove the model or correlate a reboot.

On removal/identity change, any read is canceled and old fields are discarded.
Reappearance is probed again when auto-connect is enabled. Several attached
devices require selection unless a unique preferred identity is available.

## States and reads

`Disconnected -> Detecting -> Connecting -> Connected` is the normal path.
Bootloader candidates end in `Bootloader`. Serial ownership produces `Busy`;
permissions, timeouts, protocol response errors, and I/O failures produce `Error`.
Retry permits `Error/Busy/Connected -> Connecting`. Every state permits removal.
`Updating` and `Rebooting` are reserved states, with no implemented operations.
`allowedTransition` is centralized and tested, not scattered UI flags.

Auto-connect is configured by QML before readings are enabled. A probe opens
the descriptor nonblocking with close-on-exec, obtains flock/TIOCEXCL, saves
termios, sets raw 115200 mode, toggles DTR, and waits for the firmware CLI prompt.
It sends `device_info` and `info power`, then `start_rpc_session` and delimited
`PB.Main` requests (ping, protobuf version, storage info). Each phase has a
five-second deadline; accumulated responses are limited to 64 KiB. RPC frames
are bounded to 16 KiB. If RPC does not complete, CLI device/power data is kept.
ANSI sequences are removed and key/value fields normalized from dots to
underscores. Zero is a valid power value. Power failure can produce an explicit
warning while preserving successful device information.

Completion/failure/cancellation lowers DTR, restores termios, releases exclusive
access, and closes the fd. No port is held between reads. As with other serial
programs, flock is cooperative and TIOCEXCL cannot evict an already-open client.
Do not intentionally run competing clients against one device.

## IPC v1

The backend writes complete JSON snapshots on startup, changes, and commands:

```json
{"protocol":1,"type":"snapshot","version":"1.0.0","devices":[],"selected":"","error":{}}
```

Each device contains `key`, `id`, `state`, USB descriptors, `port`, `info`,
`power`, `error`, `warning`, `sampledAt`, `identityConfirmed`, and `rpc`.
`rpc` may include `probeMs` and `pingMs` after a Connected read.
No device object is emitted when nothing is attached. No guessed values are
added to firmware output. `rpc` is an object with `available`, `protobuf`,
`ping`, optional `storage` / `storage_ext` / `storage_int`, `summary`, and
`reason` when the session fails.

Commands (one JSON object per line):

```json
{"op":"snapshot"}
{"op":"configure","autoConnect":true,"preferredDevice":"","notifyConnect":true,"notifyError":true}
{"op":"select","key":"<key returned by snapshot>"}
{"op":"refresh","key":"<key returned by snapshot>"}
```

Unknown operations/keys return a structured error snapshot. Malformed JSON is
reported to stderr; oversized input terminates the backend. stdin EOF terminates
the process and closes its ports. Diagnostics use stderr, not the JSON stream.
The channel is private inherited pipes; there is no listening network socket.
QML bounds in-memory stderr history and displays real errors. Restart is explicit
to prevent a crash/restart loop. Disable, removal, and shell shutdown own cleanup.

## Remote session

`{"op":"remoteStart"}` opens a held RPC session: CLI banner, `start_rpc_session`,
ping, `gui_start_screen_stream`. Unsolicited `gui_screen_frame` messages are
emitted as raw 128×64 u8g2 bytes
`{"protocol":1,"type":"frame","key":"...","data":"...","orientation":0}` — not
stuffed into snapshots. The panel paints them in place. `input` sends
`gui_send_input_event` PRESS then SHORT then RELEASE. Mouse on the painted
screen maps to those keys (click OK, right-click Back, drag/wheel as D-pad);
`visual-*` buttons are rotated to physical keys using the current frame
orientation. There is no pointer coordinate in official GUI RPC. `screenshot`
writes PNG under Pictures/OmaFlip and records up to eight paths.
`remoteStop` sends `gui_stop_screen_stream` and closes the port. Refresh is
refused while Remote or Files owns the serial.

## Files session

`{"op":"filesStart"}` opens a held RPC session: CLI banner, `start_rpc_session`,
ping, then `storage_list` for `/ext` (falls back to `/` if the SD list fails).
The listing lives on the device snapshot as `files` (`path`, `entries`,
`preview`, `transfer`, `error` / `errorCode`). Device paths are normalized and
must stay under `/ext`, `/int`, or `/any`; `..` cannot escape those roots.

Commands while the session is open: `filesList`, `filesPreview`, `filesDownload`,
`filesUpload`, `filesMkdir`, `filesRename`, `filesDelete`. Firmware storage
writes always replace (`CREATE_ALWAYS`); overwrite confirmation is host-side
via `errorCode: exists` unless `overwrite` is true. Read/write chunks are 512
bytes with `has_next`, matching firmware `MAX_DATA_SIZE`. Downloads land in
`Downloads/OmaFlip`. Transfers are capped at 32 MiB. `filesStop` sends
`stop_session` and releases the port. Remote and Files are mutually exclusive.

## CLI session

`{"op":"cliStart"}` opens a held CLI session (not RPC): DTR, banner, `help`.
Discovered commands live on `cli.commands`. `cliSend` writes a sanitized line
plus CR and waits for `>: `, except documented streaming commands (`log`,
`echo`, `top` unless `top 0`) which run until `cliInterrupt` sends Ctrl+C
(0x03). `start_rpc_session` is refused so the console cannot leave CLI mode.
Scrollback is 64 KiB. `cliSave` writes `Downloads/OmaFlip/cli-*.txt`.
`cliStop` releases the port. Remote, Files, and CLI are mutually exclusive.

## Apps session

`{"op":"appsStart"}` opens a held RPC session, lists `/ext/apps` then each
category folder, and collects `.fap` / `.js` files. Launch uses
`app_start_request`: FAP path as `name`, JavaScript as `name=JS Runner` with
the script path in `args` (official `js_app` firmware name). Install writes
under `/ext/apps/<category>` (default Misc or Scripts). Remove is a storage
delete after confirmation. Script edit is a 32 KiB text read/write.
`appsStop` sends `stop_session`. Remote, Files, CLI, and Apps are mutually
exclusive.

The Flipper Application Catalog is consumed by official companion apps and
Flipper Lab. OmaFlip does not call catalog HTTP APIs.

## Device management

`{"op":"manageStart"}` opens a held RPC session and lists `/ext/omaflip_backup`.
`backupCreate` writes firmware `storage_backup_create` of `/int` plus a JSON
sidecar (`omaflip_backup` version, firmware version/origin, hardware target).
Restore refuses a target mismatch, and requires confirmation for origin or
version mismatch.

`firmwareCheck` takes `provider` `official` or `momentum`. Official fetches
`https://update.flipperzero.one/firmware/directory.json` (that host only).
Momentum fetches `https://up.momentum-fw.dev/firmware/directory.json`
(release and development only). `firmwareDownload` stores the `update_tgz`
under Downloads/OmaFlip/firmware after SHA-256 verification.
`firmwareApply` uploads the tgz, `storage_tar_extract`, `system_update_request`
with `update.fuf` derived from the package filename, then reboot UPDATE.
Origin mismatch requires `replaceOrigin`. No DFU writes.

`packsCheck` fetches `https://up.momentum-fw.dev/asset-packs/directory.json`.
`packsDownload` stores a verified `pack_targz` under Downloads/OmaFlip/asset-packs.
`packsInstall` extracts into `/ext/asset_packs`. `packsRemove` deletes a folder
there. Packs are activated in on-device Momentum Settings; OmaFlip does not
write Momentum settings files.

## Developer session

`{"op":"devStart"}` opens a held RPC session for deploy and inspector.
`devProject` sets an absolute folder. `devCreate` / `devBuild` / `devLint` /
`devUpdateSdk` run `ufbt` as a QProcess (argument list, not a shell). Momentum
SDK updates use `--index-url=https://up.momentum-fw.dev/firmware/directory.json`.
`devDeploy` uploads `dist/*.fap` under `/ext/apps/<category>` and starts it.
`devInspect` is an allow-list of read-only RPC commands. GPIO writes, reboot,
factory reset, and DFU/`ufbt flash` are not exposed.

Desktop notifications use `notify-send` with categories `device.added`,
`device.removed`, and `device.error` only. They are off when the matching
configure flag is false.

Dev also searches `~/.local/bin` and can run `python3 -m ufbt`. `devInstallUfbt`
runs `python3 -m pip install --user --upgrade ufbt` as an argument list.

## Next transport slice (not implemented)

DFU write is not implemented. Do not treat official firmware as a Momentum
update.
