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
VID, PID, manufacturer, and product description. The available udev devlinks
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
It sends only `device_info` and `info power`, one command at a time. Each phase
has a five-second deadline; accumulated responses are limited to 64 KiB.
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
{"protocol":1,"type":"snapshot","devices":[],"selected":"","error":{}}
```

Each device contains `key`, `id`, `state`, USB descriptors, `port`, `info`,
`power`, `error`, `warning`, `sampledAt`, `identityConfirmed`, and `rpc`.
No device object is emitted when nothing is attached. No guessed values are
added to firmware output. `rpc` explicitly says it has not been tested.

Commands (one JSON object per line):

```json
{"op":"snapshot"}
{"op":"configure","autoConnect":true,"preferredDevice":""}
{"op":"select","key":"<key returned by snapshot>"}
{"op":"refresh","key":"<key returned by snapshot>"}
```

Unknown operations/keys return a structured error snapshot. Malformed JSON is
reported to stderr; oversized input terminates the backend. stdin EOF terminates
the process and closes its ports. Diagnostics use stderr, not the JSON stream.
The channel is private inherited pipes; there is no listening network socket.
QML bounds in-memory stderr history and displays real errors. Restart is explicit
to prevent a crash/restart loop. Disable, removal, and shell shutdown own cleanup.

## Next transport slice (not implemented)

Official `.proto` files will be pinned with license/provenance and code-generation
instructions. Flipper RPC uses length-delimited protobuf `PB.Main` messages;
`command_id`, `command_status`, and `has_next` control responses. `start_rpc_session`
switches the CLI into RPC. Protocol versions and firmware support must be queried.
Do not construct guessed protobuf byte sequences. Serial CLI and RPC share one
interface; the backend must arbitrate ownership before adding either live mode.
