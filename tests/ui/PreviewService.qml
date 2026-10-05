import QtQuick

// All values below are invented release-preview fixtures, not device reads.
// FakeService records calls only; no production Service or process is loaded.
FakeService {
    id: root
    frameData: ""
    readonly property string fixtureFrame: "/wEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAf//AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA//8AAAAAAAAAAAAAAAAAAAABAf8BAQD/kZGRgQBGiZGRcgAAAQH/AQEAAAA8QoGBgUI8AAD/BzjA/wAA/4CAgIAAAwzwDAMAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAD//wAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAP//AAAAAAAAAAAAAAAA8AAAAAAAwCAQEBBgAADwEBAQIMAAAAAA8BAQEBAA8AAAMMCAYBAAEBDwEBAA8AAAAPAAAPAQEBDgAADwEBAQEAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA//8AAAAAAAAAAAAAAAAPCAgICAADDAgICAYAAA8ICAgEAwAAAAAPAQEBAAAPAAAMAwEGCAAAAA8AAAAHCAgIBwAADwEBAw4AAA8JCQkIAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAD//wAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAP//gICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICA/w=="
    function previewDevice() {
        const d = makeDevice(true);
        d.info = {hardware_name: "TEST_ONLY · OmaFlip preview", firmware_version: "fixture-1.3.0", firmware_origin_fork: "Official"};
        d.power = {charge_level: 84, charge_state: "Fixture battery"};
        d.rpc = {storage: "6.2 GiB free", summary: "Synthetic RPC"};
        d.files.entries = [{name: "apps", type: "directory", size: 0}, {name: "infrared", type: "directory", size: 0}, {name: "nfc", type: "directory", size: 0}, {name: "README-fixture.txt", type: "file", size: 1024}];
        d.cli.output = "TEST_ONLY fictional CLI transcript\n>: help\nhelp         Show command list\ninfo         Device information\nstorage      Storage operations\n\nNo commands were sent to hardware.";
        d.cli.history = ["help"];
        d.cli.commands = [{name: "help"}, {name: "info"}, {name: "storage"}];
        d.apps.apps = [{name: "Fixture clock", path: "/ext/apps/Tools/fixture_clock.fap", kind: "fap", category: "Tools"}, {name: "Fixture notes", path: "/ext/apps/Misc/fixture_notes.fap", kind: "fap", category: "Misc"}];
        d.manage.backups = [{name: "fixture-backup.tgz", archive: "/synthetic/fixture-backup.tgz", compat: "ok", origin: "Official", version: "fixture-1.3.0"}];
        d.dev.project = "/synthetic/projects/fixture_app";
        d.dev.defaultProject = d.dev.project;
        d.dev.fam = {appid: "fixture_app", name: "Fixture application"};
        d.dev.fap = "/synthetic/projects/fixture_app/dist/fixture_app.fap";
        d.companion.status = "TEST_ONLY · simulated companion session";
        d.companion.lastAction = "Fixture volume action (not executed)";
        for (const name of ["files", "cli", "apps", "manage", "dev", "companion"]) d[name].open = false;
        d.remote = false;
        return d;
    }
    function prepare(disconnected) {
        calls = [];
        selectedDevice = disconnected ? null : previewDevice();
        serviceError = disconnected ? "TEST_ONLY · fictional disconnected fixture. No hardware operations." : "";
    }
}
