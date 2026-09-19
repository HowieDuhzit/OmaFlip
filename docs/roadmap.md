# Vertical-slice roadmap

1. **Foundation:** implemented. Window/reconnect checks confirmed on the
   attached Momentum device.
2. **RPC:** implemented — official bindings, bounded framing, version
   negotiation, ping, storage info, protocol tests. Live Momentum RPC reads pass.
3. **Remote:** implemented — on-demand screen stream, buttons, keyboard, crisp
   scaling, PNG screenshot save/copy/history; teardown when the view closes.
4. **Files:** implemented — browse and typed previews, upload/download progress,
   drop destination review, multi-select, directories/rename/delete, overwrite
   confirmation. Confirm on the attached Momentum device.
5. **CLI and logs:** implemented — exclusive transport handoff, firmware-discovered
   commands, history, output search/filter/save, reconnect and bounded scrollback.
   Confirm on the attached Momentum device.
6. **Apps and JavaScript:** implemented — inventory and metadata from `/ext/apps`,
   .fap install/remove/launch, catalog API investigation (no catalog client),
   lightweight script edit/upload/run. Confirm on the attached Momentum device.
7. **Device management:** implemented — versioned backup metadata, compatibility
   checks, safe restore, verified official firmware packages, confirmation and
   recovery. Confirm backup on the attached Momentum device. Official apply
   replaces custom firmware and needs an explicit origin confirmation.
8. **Momentum:** implemented — origin from `firmware_origin_fork`, Momentum
   update provider on `up.momentum-fw.dev`, asset packs under `/ext/asset_packs`.
   Activation stays on-device. Confirm on the attached Momentum device.
9. **Developer:** implemented — `ufbt` create/build/lint/SDK update, FAP
   deploy/run over RPC, read-only RPC inspector. Confirm on the attached
   Momentum device. `ufbt` must already be on PATH.
10. **Release polish:** implemented — notify settings and `device.*` categories,
    probe/ping timings, accessible bar name, hardware matrix, native tarball.
    Milestone 1 window/reconnect confirmed on hardware. DFU write stays off.

At each stage: research, build the smallest real workflow, test hardware, repair
failures, commit the verified slice. Unit test requirements grow with each slice:
RPC framing/protobuf in M2, filesystem/path/transfer in M4, firmware and backup
metadata in M7. No tests claim coverage for features that do not exist.
