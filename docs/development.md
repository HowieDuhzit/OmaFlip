# Development

Build with `./scripts/build`; run process tests with `python tests/test_ipc.py`.
`protoc` and libprotobuf are required so CMake can generate Flipper RPC bindings.
Qt Gui is required for screen PNG encode/decode. Filesystem path and
chunked storage RPC tests live in the same C++ suite, as do CLI help parsing
and a pseudoterminal console session. Backup metadata compatibility, official
and Momentum firmware index parsing, updater manifest paths, asset-pack
index parsing, ufbt project helpers, and desktop-notification argument allow-lists are
unit-tested; live firmware apply, `ufbt` builds, and notify-send delivery
are hardware-only.
Qt Test fixtures use pseudoterminals and visibly synthetic `TEST_ONLY` values;
they are compiled only into the test executable. `--scan` only enumerates USB
metadata and does not open device ports. `--stdio` waits for a configure command
before auto-reading hardware. No environment flag enables fake production data.

`scripts/install-dev` creates a normal user-owned plugin copy. Edit the source
checkout, validate, disable the installed plugin, copy the changed QML/scripts
and rebuilt executable, then enable again. Avoid copying `.git` or `build/`
into the plugin tree; Quickshell's watcher would otherwise reload on every
generated change. Never modify `/usr/share/omarchy/`.

Run:

```sh
omarchy plugin validate .
qmllint -I /usr/share/omarchy/shell BarWidget.qml Panel.qml Service.qml qml/*.qml
python tests/test_ipc.py
```

Use `omarchy-shell shell summon io.github.howieduhzit.omaflip '{}'` and `hide`
for UI checks. The manifest deliberately omits `keepLoaded`: disabling the
plugin must destroy the service and release the device. Omarchy rescanning is
asynchronous; the installer uses bounded retries during setup only, never at
runtime. Rootless normal operation is mandatory.

## Reproducible checks

Run `./tests/run` for the portable native/IPC/static/release/installer checks.
`python tests/test_install.py --host -v` additionally exercises the installed
Omarchy removal script under an isolated HOME, with call-recording shell stubs.
It verifies file preservation/backup behavior, not production-shell lifecycle. Hosted UI tests require an
installed Omarchy shell and a Wayland session; run `python tests/test_ui.py -v`
separately. Fixture screenshots are documented in `tests/ui/README.md` and are
never loaded by production `Service.qml`.

A Git-managed plugin checkout can be updated with `omarchy plugin update` after
closing its sessions and disabling it. Preserve local changes before updating.
After copying or updating QML, confirm the visible layout; matching files and
working backend IPC do not establish that cached QML was replaced. If the old UI
persists, use `omarchy-restart-shell`, which briefly restarts bars/panels and refuses
a live secure-lock restart. Reopen and visually verify OmaFlip afterward.

## UI structure

`Panel.qml` owns the local page selection and exclusive device-session lifecycle.
Its persistent sidebar becomes wrapped navigation when the available panel width
is narrow. Opening Settings or Diagnostics releases any tool session; closing the
panel, switching devices, or losing the selected device releases it as well.
Inactive tool views receive no service, so stale confirmations cannot operate on
a newly selected device. Developer project synchronization waits for a ready
session instead of treating hidden initialization as a successful configuration.

`qml/ToolButton.qml` centralizes themed primary/destructive/disabled/focus states.
`qml/InputField.qml` retains native text editing, selection, and keyboard focus.
The panel shortcut catcher is blocked while a tool input is focused. Navigation
and actions are focusable; confirmation targets stay explicit. Remote LCD colors
are intentionally fixed, but surrounding controls use Omarchy theme tokens.

Use `/usr/lib/qt6/bin/qmlformat` and `/usr/lib/qt6/bin/qmllint` on systems where
`/usr/bin` resolves to Qt 5. Standalone Qt tools cannot infer Quickshell's virtual
`qs.*` namespace from `-I /usr/share/omarchy/shell` alone: missing-import warnings
are not evidence of a successful hosted runtime check.

Use [validation.md](validation.md) for hardware acceptance. Test serial timeout,
unsupported commands, permission errors, connection loss mid-response, and
repeated reconnects. Preserve exact errors and never display stale values after
a failed read. Do not run competing device clients during hardware testing.

All fields rendered from device data use plain text. Diagnostics may contain
serial numbers and local paths; screenshots for the repository must be cropped
to OmaFlip itself. Actual connected screenshots await actual hardware.
