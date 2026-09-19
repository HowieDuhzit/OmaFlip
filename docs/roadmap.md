# Vertical-slice roadmap

1. **Foundation:** implemented; physical device acceptance pending. Do not mark
   complete or start M2 until real device/power fields and reconnects pass.
2. **RPC:** generated official bindings, bounded framing, version negotiation,
   device/power/storage requests, diagnostics and protocol tests.
3. **Remote:** on-demand screen stream, buttons, keyboard, crisp scaling, PNG
   screenshot save/copy/history; teardown when the view closes.
4. **Files:** browse and typed previews, upload/download progress, drop destination
   review, multi-select, directories/rename/delete, overwrite confirmation.
5. **CLI and logs:** exclusive transport handoff, firmware-discovered commands,
   history, output search/filter/save, reconnect and bounded scrollback.
6. **Apps and JavaScript:** inventory and metadata, .fap install/remove/launch,
   catalog API investigation, lightweight script edit/upload/run/output.
7. **Device management:** versioned backup metadata, compatibility checks, safe
   restore, verified official firmware packages, confirmation and recovery.
8. **Momentum:** verified origin detection, separate update provider, asset packs
   using documented paths, only supported remote configuration interfaces.
9. **Developer:** `ufbt` integration, project build/deploy/run/log loop, RPC
   inspector, optional advanced operations.
10. **Release polish:** larger native management surface, keyboard/accessibility,
    notification categories, full settings, performance measurements, screenshots,
    hardware matrix, distributable native packages.

At each stage: research, build the smallest real workflow, test hardware, repair
failures, commit the verified slice. Unit test requirements grow with each slice:
RPC framing/protobuf in M2, filesystem/path/transfer in M4, firmware and backup
metadata in M7. No tests claim coverage for features that do not exist.
