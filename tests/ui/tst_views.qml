import QtQuick
import QtTest
import qs.Ui as Ui
import "qml" as Oma

Item {
    id: scene
    width: 900; height: 900
    FakeService { id: fake }
    // Match Service's snapshot dependency direction for Apps save regressions.
    QtObject {
        id: snapshots
        property var devices: []
        readonly property var selectedDevice: devices[0] || null
        property var calls: []
        function appsWrite(path, text) { calls = calls.concat([{name: "appsWrite", args: [path, text]}]); }
    }
    Ui.PanelKeyCatcher {
        id: catcher
        anchors.fill: parent
        property int moves: 0
        property int activations: 0
        blocked: !!(loader.item && loader.item.inputActive)
        onMoveRequested: moves++
        onActivateRequested: activations++
        Loader { id: loader; width: 480 }
    }
    TestCase {
        id: tests
        name: "OmaFlipViews"
        when: windowShown
        function load(name) {
            loader.setSource(Qt.resolvedUrl("qml/" + name + ".qml"), {service: fake});
            tryCompare(loader, "status", Loader.Ready);
            wait(30);
            return loader.item;
        }
        function descendants(item) {
            let out = [];
            for (const child of item.children || []) out = out.concat([child], descendants(child));
            return out;
        }
        function field(view, hint) {
            const result = descendants(view).find(item => item.placeholderText && item.placeholderText.indexOf(hint) >= 0);
            verify(!!result, "InputField containing " + hint);
            return result;
        }
        function init() { loader.source = ""; fake.reset(); catcher.moves = 0; catcher.activations = 0; }
        function cleanup() { loader.source = ""; }
        function test_all_views_data() {
            return ["RemoteView", "FilesView", "CliView", "AppsView", "ManageView", "DevView"].map(name => ({tag: name, view: name}));
        }
        function test_all_views(data) {
            const view = load(data.view);
            verify(view.implicitHeight > 0);
            view.service = null;
            wait(10);
            verify(view.implicitHeight > 0, "null service remains renderable");
        }
        function test_remote_widths_data() {
            let rows = [];
            for (const width of [64, 127, 256, 360, 480, 760])
                for (const orientation of [0, 1, 2, 3]) rows.push({tag: width + "-" + orientation, width: width, orientation: orientation});
            return rows;
        }
        function test_remote_widths(data) {
            const view = load("RemoteView");
            view.width = data.width; fake.frameOrientation = data.orientation; wait(10);
            const screen = findChild(view, "remoteScreen"), pad = findChild(view, "directionPad");
            verify(screen.width > 0 && screen.width <= view.width, "LCD fits " + data.width);
            verify(pad.width <= view.width, "direction pad fits");
            verify(screen.height <= 384, "LCD height capped");
            for (const dir of ["up", "down", "left", "right", "ok"]) {
                const button = findChild(view, "remote-" + dir);
                verify(button.x >= 0 && button.x + button.width <= pad.width + 0.01, dir + " fits pad");
                button.triggered();
                compare(fake.calls[fake.calls.length - 1].args[0], dir);
            }
        }
        function test_keyboard_does_not_hijack_data() {
            return [{tag: "cli", view: "CliView", hint: "Enter a command"},
                    {tag: "dev", view: "DevView", hint: "Absolute project"},
                    {tag: "apps", view: "AppsView", hint: "/ext/apps"},
                    {tag: "files", view: "FilesView", hint: "name"}];
        }
        function test_keyboard_does_not_hijack(data) {
            const view = load(data.view);
            if (data.view === "FilesView") view.requestMkdir();
            const input = field(view, data.hint);
            input.text = ""; input.forceActiveFocus();
            tryCompare(view, "inputActive", true);
            for (const character of "hjkl x") keyClick(character);
            compare(input.text, "hjkl x");
            compare(catcher.moves, 0); compare(catcher.activations, 0);
            keyClick(Qt.Key_Left); keyClick(Qt.Key_Backspace);
            compare(catcher.moves, 0);
            input.focus = false; catcher.forceActiveFocus();
            keyClick(Qt.Key_Down);
            compare(catcher.moves, 1, "navigation resumes outside editor");
        }
        function test_files_delete_confirmation() {
            const view = load("FilesView"); view.requestDelete();
            compare(view.confirmKind, "delete"); compare(fake.calls.length, 0);
            view.confirmKind = ""; view.runConfirm(); compare(fake.calls.length, 0);
            view.requestDelete(); view.runConfirm();
            compare(fake.calls.length, 1); compare(fake.calls[0].name, "filesDelete");
            compare(fake.calls[0].args[0], "/ext/sample.txt");
        }
        function test_apps_remove_confirmation() {
            const view = load("AppsView"); view.requestRemove();
            compare(view.confirmKind, "remove"); compare(fake.calls.length, 0);
            view.confirmKind = ""; view.runConfirm(); compare(fake.calls.length, 0);
            view.requestRemove(); view.runConfirm(); compare(fake.calls[0].name, "appsRemove");
        }
        function test_manage_restore_and_firmware_confirmation() {
            const view = load("ManageView"); view.requestRestore();
            compare(view.confirmKind, "restore"); compare(fake.calls.length, 0);
            view.runConfirm(); compare(fake.calls[0].name, "backupRestore");
            fake.calls = []; view.requestApply();
            compare(view.confirmKind, "apply"); compare(fake.calls.length, 0);
            view.confirmKind = ""; view.runConfirm(); compare(fake.calls.length, 0);
            view.requestApply(); view.runConfirm(); compare(fake.calls[0].name, "firmwareApply");
        }
        function publish(device) {
            fake.selectedDevice = JSON.parse(JSON.stringify(device)); wait(1);
            // Publish a backend snapshot only after dependent bindings settle.
            // Real Service updates devices/selectedDevice as one snapshot.
            fake.devicesChanged(); wait(1);
        }
        function snapshot(device) { snapshots.devices = [JSON.parse(JSON.stringify(device))]; wait(1); }
        function test_apps_script_save_acknowledgment() {
            const view = load("AppsView"), device = fake.makeDevice(true);
            snapshots.devices = []; snapshots.calls = []; view.service = snapshots;
            device.apps.script = {path: "/ext/apps/Scripts/demo.js", text: "A"}; snapshot(device);
            compare(view.scriptDraft, "A"); verify(!view.scriptDirty);
            view.scriptDraft = "B"; view.saveScript();
            compare(snapshots.calls[0].name, "appsWrite"); compare(snapshots.calls[0].args[1], "B");
            verify(view.scriptDirty); compare(view.pendingSaveText, "B");
            device.apps.busy = true; snapshot(device); verify(view.sawSaveBusy);
            device.apps.busy = false; snapshot(device);
            compare(view.pendingSavePath, ""); compare(view.savedScriptText, "B"); verify(!view.scriptDirty);
            view.scriptDraft = "A"; verify(view.scriptDirty, "editing back to old snapshot is dirty");
            view.saveScript(); device.apps.busy = true; snapshot(device);
            device.apps.busy = false; device.apps.error = "Synthetic write failure"; snapshot(device);
            compare(view.pendingSavePath, ""); compare(view.savedScriptText, "B"); verify(view.scriptDirty, "failed save stays dirty");
        }
        function test_firmware_download_without_session() {
            const view = load("ManageView"), device = fake.makeDevice(true);
            view.section = "firmware"; device.manage.ready = false; publish(device);
            fake.firmware = {url: "https://invalid.example/synthetic-only", provider: "official"};
            const button = descendants(view).find(item => item.text === "Download update" && item.triggered);
            verify(!!button); verify(button.enabled, "download does not require manage session");
            button.triggered(); compare(fake.calls[0].name, "firmwareDownload");
            fake.firmware = {path: "/synthetic/verified.tgz", provider: "official"};
            compare(button.text, "Apply verified update"); verify(!button.enabled, "apply still requires session");
            view.requestApply(); compare(view.confirmKind, "");
        }
        function test_stale_confirmation_reset_data() {
            return ["FilesView", "AppsView", "CliView", "ManageView", "DevView"].map(name => ({tag: name, view: name}));
        }
        function test_stale_confirmation_reset(data) {
            const view = load(data.view);
            if (data.view === "CliView") view.confirmText = "power off";
            else view.confirmKind = "synthetic_pending";
            if (data.view === "FilesView") { view.confirmHosts = ["/synthetic/file"]; view.nameMode = "rename"; }
            view.service = null;
            if (data.view === "CliView") compare(view.confirmText, "");
            else compare(view.confirmKind, "");
            if (data.view === "FilesView") { compare(view.confirmHosts.length, 0); compare(view.nameMode, ""); }
            view.service = fake;
            if (data.view === "CliView") compare(view.confirmText, "");
            else compare(view.confirmKind, "");
        }
        function test_dev_saved_project_ready_reconnect() {
            const device = fake.makeDevice(true); device.dev.ready = false; publish(device);
            const host = Qt.createQmlObject('import QtQuick; QtObject { function setting(name, fallback) { return name === "devProject" ? "/synthetic/saved-project" : fallback; } function persist(name, value) {} }', scene);
            const view = load("DevView"); view.hostWidget = host;
            view.fillProject(); compare(fake.calls.length, 0, "not sent while unready");
            device.dev.ready = true; publish(device);
            tryCompare(view, "lastAppliedProject", "/synthetic/saved-project");
            compare(fake.calls.filter(c => c.name === "devProject").length, 1);
            view.service = null; view.service = fake; wait(10);
            compare(fake.calls.filter(c => c.name === "devProject").length, 2, "saved project reapplied on reconnect");
            view.hostWidget = null; host.destroy();
        }
        function test_dev_deploy_confirmation() {
            const view = load("DevView");
            const device = fake.makeDevice(true);
            device.dev.project = "/synthetic/project"; device.dev.fap = "/synthetic/app.fap";
            fake.selectedDevice = device;
            field(view, "Absolute project").text = device.dev.project;
            verify(view.canDeploy);
            const deploy = descendants(view).find(item => item.text === "Deploy…" && item.triggered);
            verify(!!deploy); deploy.triggered();
            compare(view.confirmKind, "deploy");
            compare(fake.calls.filter(call => call.name === "devDeploy").length, 0);
            view.confirmKind = ""; view.runConfirm();
            compare(fake.calls.filter(call => call.name === "devDeploy").length, 0);
            deploy.triggered(); view.runConfirm();
            compare(fake.calls.filter(call => call.name === "devDeploy").length, 1);
        }
        function test_manage_pack_confirmation() {
            const view = load("ManageView"), device = fake.makeDevice(true);
            device.origin = "Momentum"; device.manage.packs = [{name: "sample"}];
            fake.selectedDevice = device;
            view.requestPackInstall(); compare(view.confirmKind, "packInstall"); compare(fake.calls.length, 0);
            view.runConfirm(); compare(fake.calls[0].name, "packsInstall");
            fake.calls = []; view.requestPackRemove(); compare(view.confirmKind, "packRemove"); compare(fake.calls.length, 0);
            view.runConfirm(); compare(fake.calls[0].name, "packsRemove"); compare(fake.calls[0].args[0], "sample");
        }
        function test_confirmation_busy_guard() {
            const view = load("FilesView"); view.requestDelete();
            const device = fake.makeDevice(true); device.files.busy = true; fake.selectedDevice = device;
            view.runConfirm(); compare(fake.calls.length, 0); compare(view.confirmKind, "delete");
        }
        function test_repeated_view_creation() {
            for (let cycle = 0; cycle < 3; cycle++)
                for (const name of ["RemoteView", "FilesView", "CliView", "AppsView", "ManageView", "DevView"]) {
                    verify(!!load(name)); loader.source = ""; wait(1);
                }
        }
        function test_cli_destructive_confirmation() {
            const view = load("CliView"), input = field(view, "Enter a command");
            for (const command of ["power off", "power reboot", "factory_reset", "storage erase /ext", "rm /ext/demo", "format", "dfu"]) {
                input.text = command; view.send(); compare(view.confirmText, command);
                compare(fake.calls.length, 0); view.confirmText = "";
            }
            input.text = "help"; view.send(); compare(fake.calls[0].name, "cliSend");
        }
    }
}
