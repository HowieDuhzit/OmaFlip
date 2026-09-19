# Development

Build with `./scripts/build`; run process tests with `python tests/test_ipc.py`.
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

Use [validation.md](validation.md) for hardware acceptance. Test serial timeout,
unsupported commands, permission errors, connection loss mid-response, and
repeated reconnects. Preserve exact errors and never display stale values after
a failed read. Do not run competing device clients during hardware testing.

All fields rendered from device data use plain text. Diagnostics may contain
serial numbers and local paths; screenshots for the repository must be cropped
to OmaFlip itself. Actual connected screenshots await actual hardware.
