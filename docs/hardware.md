# Hardware matrix

Recorded **2026-09-19**. Device serials omitted.

| Host | Flipper | Firmware origin | Target | Result |
| --- | --- | --- | --- | --- |
| Omarchy 4.0.4-1, Qt 6.11.2, libudev 261 | Flipper Zero named Flippie | Momentum (`mntm-dev`) | f7 | USB, CLI, RPC 0.25, Remote, Files, CLI, Apps, Backup, Momentum index, Dev inspector, ufbt create/build |

Window/reconnect (unplug/replug, panel close/reopen) confirmed **2026-09-19**.

Dev create/build confirmed **2026-09-19** with ufbt 0.2.6 (pipx) and Momentum SDK `d3f89dfe`.

Not exercised on this host:

- Official firmware as the running image
- A second Flipper at the same time
- STM32 DFU write (identity-only listing is implemented; flashing is disabled)

Probe and ping durations are live fields on the device snapshot (`rpc.probeMs`, `rpc.pingMs`) after a Connected read. They are measurements, not a benchmark suite.
