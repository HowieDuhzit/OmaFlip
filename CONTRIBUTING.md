# Contributing

Build and validate using [development.md](docs/development.md). Keep changes in
functional vertical slices. Remaining Milestone 1 window and reconnect checks
are manual. Momentum updates use `up.momentum-fw.dev`, not the official
index. Official firmware apply is destructive and must keep confirmation and
origin checks. Asset packs install to `/ext/asset_packs` only; do not write
undocumented Momentum settings. Developer `ufbt` runs as an argument list, never
a shell, and must not expose `flash` / GPIO write. Desktop notifications use
`notify-send` with an allow-listed `device.*` category only.

Use current upstream source to verify protocol and shell APIs. Record source
revisions and firmware versions in test results. Device-protocol code belongs
in `service/`, not QML. Do not ship mock device values, hidden network calls,
background polling, disabled placeholder navigation, or a second shell process.

Add tests for behavior and failure boundaries. Separate simulated serial
fixtures from hardware acceptance. Do not mark USB, firmware, or UI tests passed
solely because a build or static checker passes. Never include private device
serials, desktop captures with unrelated windows, or credentials in reports.

Ordinary management/development functionality is welcome. Do not introduce
automated abuse workflows. Future destructive operations require explicit,
informed user confirmation, accurate progress, and recovery documentation.

Use semantic versioning and update `CHANGELOG.md`. Keep generated bindings
reproducible, preserve dependency licenses, and exclude build output. No Git
commit or remote publication is performed by build/install scripts.
