# Validation record

Date: **2026-09-18 (US/Eastern)**. Host: Omarchy **4.0.4-1**, Qt **6.11.2**,
libudev **261**, GCC **16.2.1**. No physical Flipper was attached during this run.

## Automated checks

| Check | Result |
| --- | --- |
| CMake Debug and RelWithDebInfo builds, warning-as-error backend | Pass |
| CTest foundation suite: 12 test methods (+ setup/cleanup) | Pass |
| Python process/IPC suite: 4 tests | Pass |
| `omarchy plugin validate .` | Pass, exit 0 |
| `qmllint -I /usr/share/omarchy/shell` on all five QML files | Pass, no diagnostics |
| `udevadm verify udev/70-omaflip.rules` | Pass |
| Production `omaflip --scan` | Empty real inventory, no substituted data |

Coverage: strict USB recognition; DFU ambiguity; stable identity across port
changes; distinct devices; legal/illegal state transitions; ANSI/key/value
parsing; fragmented prompts; misleading prompt text; permission/busy errors;
missing ports; three full pseudoterminal transactions with reopening; disconnect
during read; unsupported CLI; timeout; explicit cancellation; response size bound;
startup snapshots; invalid input/commands; single backend lock; parent EOF;
graceful SIGTERM and lock cleanup.

Pseudoterminals test the real transport code, **not real Flipper firmware**.
The Arch-container GitHub Actions workflow is provided but has not been run on
GitHub; only the recorded local commands are claimed as passed.
Permission classification is unit-tested; an actual inaccessible Flipper has
not been exercised. RPC/transfer/firmware/backup tests are deferred with their
unimplemented slices, not represented by empty tests.

## Installed-shell checks

| Workflow | Result |
| --- | --- |
| Install to normal plugin directory / enable | Pass |
| Bar label and native disconnected panel | Pass; actual screenshot in `screenshots/` |
| Shell summon / hide | Pass |
| Arrow keys, Tab, Enter, Escape | Pass with Wayland keyboard input |
| Restart backend from keyboard-selected panel action | Pass; new backend PID |
| Auto-connect / compact settings | Pass; persisted through scoped shell settings; restored to on/full label |
| Disable | Pass; backend exits |
| Re-enable | Pass; exactly one backend |
| Remove / reinstall | Pass; Omarchy removes the plugin and creates its normal backup |
| Omarchy shell restart | Pass; one replacement shell and one backend, panel loads |
| Plugin-specific runtime QML errors | None after successful load |
| Disconnected idle sample | 0 backend CPU ticks over a 5-second sample; one process |

Shell discovery is asynchronous: the first enable immediately after a rescan
reported unknown, then enabling succeeded. The installer now uses bounded
retries. The host retained an old nested QML component across reinstall until
the shell restart; restart confirmed the latest controls. One immediate IPC
call during shell startup timed out; the subsequent shell ping and panel
interaction succeeded. These transient host behaviors are not hardware evidence.

The idle sample is a brief observation, not an exhaustive performance benchmark.
Unrelated pre-existing shell/plugin warnings are outside this change. No second
Quickshell process was started alongside the running shell.

## Required physical acceptance — all pending

- Normal Flipper USB arrival and successful CLI device/power reads.
- Compare displayed values to the actual device; record firmware version.
- Missing-access error with real udev permissions, then successful setup.
- Physical unplug/replug, repeated reconnects, and unplug during a read.
- More than one actual Flipper, selection and preferred identity after reconnect.
- Device reboot and transition through firmware/bootloader modes.
- Official and Momentum firmware separately.
- Closing/restarting/disabling the service while it owns a real serial port.
- Mouse click on the bar in addition to tested IPC/keyboard opening.

**Milestone 1 is not complete until physical acceptance passes. Milestone 2 has
not started.** The full release workflow (remote, files, apps, logs, backup,
firmware updates, Momentum management) is not available in this preview.
