# Isolated QML regressions

## Reproducible v1.3.0 documentation previews

From the checkout root, on an active Wayland session with installed Omarchy
Ui/Commons and Quickshell:

```sh
python scripts/capture-preview
# Render to a separate directory instead of replacing documentation fixtures:
python scripts/capture-preview --output "$TMPDIR/omaflip-preview-review"
# Optional synthetic Remote, explicitly separate from the actual-device capture:
python scripts/capture-preview --include-remote-fixture --output "$TMPDIR/omaflip-preview-review"
```

This starts a **separate short-lived TEST_ONLY popup**; do not run it while
someone is capturing the live panel, because the isolated popup may receive
keyboard focus. It does not restart or modify the installed plugin, shell
configuration, or production process. It grabs only its own QML card with
`grabToImage`, never a desktop screenshot. The scratch configuration is removed
on exit. The script rejects missing captures, invalid PNG headers/dimensions,
and QML errors before publishing any output.

The real repository `Panel.qml` and `qml/` run with actual installed Omarchy
Ui/Commons; this is not a generated UI mockup. `PreviewService.qml` extends the
existing call-recording `FakeService.qml`, with no backend, filesystem reads,
network operations, or device access. All displayed values are invented and
committed: battery 84%, 6.2 GiB free storage, fixture firmware version,
`/synthetic/` project/backup paths, fictional apps and directory entries, and
an explicitly fictional CLI transcript. The 128x64 monochrome LCD fixture is
a committed 1024-byte framebuffer encoded in base64, hand-composed using a
border and the words `TEST ONLY` / `LCD FIXTURE`, not read from hardware. Its
frame event is delivered after the actual Remote Canvas mounts. Companion
status and its last action are synthetic; no host action executes.

Connected fixture cards carry `TEST_ONLY` in the actual device-title field.
The disconnected card has no selected device and displays an explicit
`TEST_ONLY` fictional-fixture notice through the panel's error banner. This
notice is a fixture label, not evidence of a real backend error.

Default outputs:

- `preview.png`: byte-identical to the fixture `screenshots/connected.png`.
- `screenshots/connected.png`: Overview.
- `screenshots/files.png`, `cli.png`, `apps.png`, `backup.png` (Manage), `dev.png`,
  `companion.png`, and `disconnected.png`.
- **Never writes `screenshots/remote.png`**: that filename is reserved for the
  parent release workflow's actual installed live-device capture. Optional
  `--include-remote-fixture` writes only `screenshots/remote-fixture.png`.

All nine fixture states are rendered and validated before publication; eight
are published by default. On the verified installed host (Omarchy 4.0.4,
Qt 6.11.2), fixture cards are **697 × 626 pixels**. Theme, font and scaling
come from the installed host, so another host can change the pixel dimensions
or colors even though the fictional dataset and state order stay deterministic.
Software rendering is explicitly selected. Captures prove QML rendering only,
**not hardware/backend/companion interoperability**. Existing smoke and keyboard
regression fixtures are preserved unchanged.


Run from the repository root:

```sh
python tests/test_ui.py -v
# individually
python tests/test_ui.py -v QmlRegression.test_hosted_panel
python tests/test_ui.py -v QmlRegression.test_views_and_keyboard
```

Requirements: installed Omarchy `/usr/share/omarchy/shell/{Ui,Commons}`,
`quickshell`, `/usr/lib/qt6/bin/qmltestrunner`, and an active Wayland display
for hosted testing. `/usr/bin/qmltestrunner` can be Qt5 and is deliberately
not used. Override host modules with `OMAFLIP_TEST_HOST` and output directory
with `OMAFLIP_TEST_ARTIFACTS`.

The harness copies only repository `Panel.qml`, `qml/`, and test fixtures into
a unique temporary directory under `$TMPDIR` (fallback: Hermes scratch).
It never imports repository `Service.qml`, installs a plugin, contacts a
backend, or restarts/kills the production shell. Every backend operation
records a call in a synthetic `QtObject`. The firmware URL in the fixture
is data only and never fetched.

- **Hosted smoke:** actual Quickshell on Wayland and installed Ui/Commons.
  Verifies panel/controller lifetime, navigation, single active session,
  stop/start transitions, Tab progression and sidebar arrow routing,
  device switch/disconnect, unavailable service,
  unconfirmed DFU gating, and repeated open/close. Captures actual mapped
  Overview and Remote card renders, with `TEST_ONLY` in the header.
- **Qt6 view regressions:** actual view/widget QML and installed
  `Ui.PanelKeyCatcher`, software rendering offscreen. Theme tokens alone
  are deterministic synthetic Color/Style fixtures: Quickshell's embedded
  core plugin cannot be loaded by external qmltestrunner. Tests create
  every view with connected/null service, inject real Qt keyboard events
  into all four editable views without stealing hjkl/space/x, test remote
  geometry at six widths/four orientations, repeat view creation, verify
  destructive confirmations and cancellation/busy guards, stale session
  confirmation reset, Apps script save acknowledgment/failure baseline,
  firmware download independent of management session, and saved developer
  project reapplication after readiness/reconnect. Apps snapshot tests
  mirror production `devices -> selectedDevice` bindings, without importing
  the production service.

Raw process logs and `panel-overview.png` / `panel-remote.png` are saved under
`$TMPDIR/omaflip-ui-artifacts` by default. Staged configs are removed on exit;
logs preserve original staged source paths and QML line numbers for debugging.
JS/binding warnings fail tests. A host portal app-ID registration warning may
occur when launching the isolated Quickshell process; this is not a QML layout
warning and does not prevent mapped rendering.

The Wayland smoke briefly opens its own TEST_ONLY overlay. It does not drive
or modify any production window. It uses the test process's own Qt.quit() to
exit, and timeout termination affects only that process.
