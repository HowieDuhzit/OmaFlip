import QtQuick

// Deliberately no Io, Process, network, filesystem or production Service import.
QtObject {
    id: root
    property bool ready: true
    property string serviceError: ""
    property string diagnostics: "Synthetic test backend; no device operations"
    property string selectedKey: "fixture"
    property var selectedDevice: makeDevice(true)
    property var devices: selectedDevice ? [selectedDevice] : []
    property string frameData: ""
    property int frameOrientation: 0
    property var firmware: ({path: "/synthetic/verified.tgz", provider: "official"})
    property var packs: ({catalog: [], path: "/synthetic/pack.zip"})
    property var calls: []
    function makeDevice(confirmed) {
        return {key: "fixture", identityConfirmed: confirmed, state: confirmed ? "Connected" : "DFU",
            info: {hardware_name: "TEST_ONLY Synthetic Flipper", firmware_version: "test", firmware_origin_fork: "Official"},
            origin: "Official", power: {}, rpc: {}, screenshots: [],
            files: {open: confirmed, ready: true, path: "/ext", parent: "/", entries: [{name: "sample.txt", type: "file", size: 7}]},
            cli: {open: confirmed, ready: true, history: ["help"], commands: [{name: "help"}], output: "fixture output"},
            apps: {open: confirmed, ready: true, apps: [{name: "Demo", path: "/ext/apps/Misc/demo.fap", kind: "fap", category: "Misc"}], script: {}},
            manage: {open: confirmed, ready: true, backups: [{archive: "/synthetic/backup.tgz", compat: "ok"}], packs: []},
            dev: {open: confirmed, ready: true, project: "", defaultProject: "", fam: {}, fap: ""},
            companion: {open: confirmed, status: "Synthetic only"}};
    }
    function record(name, args) {
        calls = calls.concat([{name: name, args: args || []}]);
        if (selectedDevice && (name.endsWith("Start") || name.endsWith("Stop"))) {
            const session = name.replace(/(Start|Stop)$/, "");
            const device = JSON.parse(JSON.stringify(selectedDevice));
            if (session === "remote") device.remote = name.endsWith("Start");
            else if (device[session]) device[session].open = name.endsWith("Start");
            selectedDevice = device;
        }
    }
    function closeSessions() {
        const device = makeDevice(true);
        for (const name of ["files", "cli", "apps", "manage", "dev", "companion"]) device[name].open = false;
        device.remote = false; selectedDevice = device;
    }
    function reset() {
        ready = true; selectedKey = "fixture"; selectedDevice = makeDevice(true); calls = [];
        firmware = {path: "/synthetic/verified.tgz", provider: "official"};
        packs = {catalog: [], path: "/synthetic/pack.zip"};
    }
    function remoteStart() { record("remoteStart"); }
    function remoteStop() { record("remoteStop"); }
    function filesStart() { record("filesStart"); }
    function filesStop() { record("filesStop"); }
    function cliStart() { record("cliStart"); }
    function cliStop() { record("cliStop"); }
    function appsStart() { record("appsStart"); }
    function appsStop() { record("appsStop"); }
    function manageStart() { record("manageStart"); }
    function manageStop() { record("manageStop"); }
    function devStart() { record("devStart"); }
    function devStop() { record("devStop"); }
    function companionStart() { record("companionStart"); }
    function companionStop() { record("companionStop"); }
    function input(button) { record("input", [button]); }
    function cliSend(text) { record("cliSend", [text]); }
    function cliInterrupt() { record("cliInterrupt"); }
    function filesDelete(path, recursive) { record("filesDelete", [path, recursive]); }
    function filesDownload(path, overwrite) { record("filesDownload", [path, overwrite]); }
    function filesUpload(path, host, overwrite) { record("filesUpload", [path, host, overwrite]); }
    function filesMkdir(path) { record("filesMkdir", [path]); }
    function filesRename(from, to) { record("filesRename", [from, to]); }
    function appsWrite(path, text) { record("appsWrite", [path, text]); }
    function firmwareDownload() { record("firmwareDownload"); }
    function appsRemove(path) { record("appsRemove", [path]); }
    function appsInstall(host, target, overwrite) { record("appsInstall", [host, target, overwrite]); }
    function backupRestore(path, origin, version) { record("backupRestore", [path, origin, version]); }
    function firmwareApply(replace) { record("firmwareApply", [replace]); }
    function packsInstall() { record("packsInstall"); }
    function packsRemove(name) { record("packsRemove", [name]); }
    function devProject(path) { record("devProject", [path]); }
    function devDeploy(overwrite) { record("devDeploy", [overwrite]); }
}
