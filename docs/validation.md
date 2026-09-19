# Validation record

Date: **2026-09-18 (US/Eastern)**. Host: Omarchy **4.0.4-1**, Qt **6.11.2**,
libudev **261**, GCC **16.2.1**. Automated validation was followed by a
physical Momentum Flipper pass. Device serials are intentionally omitted here.

## Automated checks

| Check | Result |
| --- | --- |
| CMake Debug and RelWithDebInfo builds, warning-as-error backend | Pass |
| CTest foundation suite: 14 test methods (+ setup/cleanup), including RPC framing and screen PNG | Pass |
| Python process/IPC suite: 4 tests | Pass |
| `omarchy plugin validate .` | Pass, exit 0 |
| `qmllint -I /usr/share/omarchy/shell` on all five QML files | Pass, no diagnostics |
| `udevadm verify udev/70-omaflip.rules` | Pass |
| Production `omaflip --scan` | Pass on the attached Momentum device after recognition repair. Empty inventory remains the disconnected result; no substituted data |

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

## Physical acceptance — partial

| Workflow | Result |
| --- | --- |
| USB arrival and by-id resolution | Pass after repair; production scan identifies the device and resolves its by-id path |
| CLI device and power reads | Pass; `Flipper Zero`, configured name `Flippie`, region `US`, and complete power data returned |
| Firmware identification | Pass on Momentum `mntm-dev`, build date 2026-09-03; origin reported as Momentum rather than guessed |
| Installed panel | Pass; visibly reached Connected and showed the same name, firmware, origin, and battery snapshot |
| Refresh | Pass; second read produced a newer UTC timestamp and updated power values |
| Serial ownership | Pass; no open serial descriptor remained after either completed read |
| Disable/re-enable and service replacement | Pass; old backend exited, one replacement backend started, and the installed panel reconnected |
| QML/runtime diagnostics | Pass for OmaFlip; no plugin-specific warning or error during the connected panel check |

The first physical scan exposed a real compatibility defect: Momentum used the
configured device name as the USB product string instead of the stock `Flipper
Control Virtual ComPort` text. Discovery and the udev rule now accept a variable
product string only when VID/PID `0483:5740` and manufacturer `Flipper Devices
Inc.` match. A regression test covers this case. The full automated suite,
plugin validation, QML lint, and udev-rule validation passed again after repair.

A second independent probe after the original Codex session hit its usage limit
reconfirmed production scan, a Connected CLI device/power read, a newer Refresh
timestamp, and no leftover serial descriptor. Device details now prefer
`hardware_region_provisioned` (`US`) over the numeric OTP region code.

Discovery now matches Flipper devices from udev properties and only reads USB
sysfs attributes for VID `0483`. A full USB-tree sysfs walk blocked the backend
on `manufacturer_show` (root hub) while Remote was in use, which froze
connection. That is repaired.

RPC on this attached Momentum device: ping succeeded, protobuf **0.25**, and
storage info for `/ext` returned. `/int` reported the same totals as `/ext` on
this firmware; that is recorded as observed, not treated as proof of a separate
internal volume. Window interaction is left to manual testing.

Milestone 1 remaining window/reconnect cases were confirmed by the tester on
**2026-09-19** on the attached Momentum device (unplug/replug, panel
close/reopen). Optional two-device, permission-denied, and official-firmware
guest-image rows are still not in the hardware matrix.

**Milestone 1 window/reconnect is recorded as confirmed.
Milestone 2 RPC is implemented and verified on this Momentum device. Milestone 3
remote and Milestone 4 files are implemented and reported working on the panel.
Milestone 5 CLI is implemented and reported working. Milestone 6 Apps is
implemented and reported working. Milestone 7 device management is implemented and reported working.
Milestone 8 Momentum is implemented and reported working.
Milestone 9 Developer is implemented and create/build was reported working
with ufbt 0.2.6.
Milestone 10 release polish is implemented. Milestone 1 window/reconnect is
confirmed on this Momentum device. GPIO writes and DFU flash stay disabled.**
