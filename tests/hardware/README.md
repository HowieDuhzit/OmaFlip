# Physical Flipper acceptance (manual, opt-in)

These tests require a real device and must not be replaced by mock snapshots.
Do not flash firmware or change access rules without the user's confirmation.
The foundation only reads information; testing reboots is an explicit on-device
action by the tester.

1. Record OS/plugin and installed firmware versions. Avoid recording USB serials
   in public results. Close qFlipper/serial clients.
2. With OmaFlip enabled, connect a Flipper using a known data cable. Verify the
   panel moves from detecting/connecting to connected and displays values that
   match the physical device. A USB descriptor alone is not a successful test.
3. Inspect the resolved by-id path, origin, power fields, RPC protobuf version,
   storage totals, and timestamp. Retry and confirm a new timestamp. Unsupported
   fields must say Unavailable. Open Remote: confirm the live screen, a button
   press, a PNG screenshot, and that the serial port is released after Close remote.
   Open Files: confirm `/ext` listing, a folder open, a text preview, a download
   into Downloads/OmaFlip, a drop-upload with destination review, overwrite
   confirmation, mkdir/rename/delete, and that the serial port is released after
   Close files. Open CLI: confirm the firmware `help` list, a command such as
   `uptime`, output filter, Save into Downloads/OmaFlip, `log` then Stop
   (Ctrl+C), and that the serial port is released after Close CLI. Remote,
   Files, and CLI must not run together. Open Apps: confirm `/ext/apps`
   inventory, launch a .fap, drop-install with destination review, remove with
   confirmation, and (if a script exists) JS edit/save/run via JS Runner. Close
   apps must release the serial port. Open Backup: create an internal backup,
   confirm sidecar metadata, and restore only a compatible archive. Check
   official firmware may download a verified package; applying it on Momentum
   replaces that firmware and needs the origin confirmation. Check Momentum
   uses `up.momentum-fw.dev`. Packs install under `/ext/asset_packs` and are
   selected on-device. Open Dev: set a project folder, Build if ufbt is
   installed, Ping in the inspector. Do not DFU-flash.
4. Unplug. Verify old values disappear. Reconnect three times. Unplug during a
   Retry. Reconnect again and confirm it recovers.
5. Close the panel and verify there is no open serial descriptor after the read.
   Disable/re-enable, restart service, restart shell; verify ownership cleanup.
6. In a deliberately inaccessible test environment, verify Permission denied
   rather than Disconnected. Use the explicit setup flow, reconnect, and retry.
   Do not remove unrelated rules or change global permissions to manufacture it.
7. With two real devices, verify separate inventory, selection, and reconnect
   of the preferred one. Duplicate descriptor handling is separately unit-tested.
8. Reboot via the device. For a bootloader test, enter DFU physically and verify
   the UI says identity unconfirmed, with no attempted flashing or serial read.
9. Repeat supported read-only flows with Official and Momentum firmware already
   installed. Firmware installation itself is outside this milestone.
10. Record observed results and failures in `docs/validation.md`; only then
    mark Milestone 1 complete and begin the RPC slice.
