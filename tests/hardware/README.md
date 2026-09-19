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
3. Inspect the resolved by-id path, origin, power fields and timestamp. Retry
   and confirm a new timestamp. Unsupported fields must say Unavailable.
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
