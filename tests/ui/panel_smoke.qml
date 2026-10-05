import QtQuick
import Quickshell

ShellRoot {
    id: suite
    FakeService { id: fake }
    QtObject {
        id: host
        property var service: fake
        function setting(name, fallback) { return fallback; }
        function persist(name, value) {}
    }
    Panel { id: panel; hostWidget: host; manageIpc: false }
    property int failures: 0
    function check(condition, description) {
        if (!condition) { failures++; console.error("OMAFLIP_FAIL " + description); }
        else console.log("OMAFLIP_PASS " + description);
    }
    function findNamed(item, name) {
        if (item.objectName === name && item.visible) return item;
        for (const child of item.children || []) {
            const found = findNamed(child, name);
            if (found) return found;
        }
        return null;
    }
    function run() {
        const tools = ["remote", "files", "cli", "apps", "manage", "dev", "companion"];
        panel.open();
        check(panel.opened, "actual Ui.Panel controller opens");
        const catcher = popupWindow().contentItem[0];
        catcher.forceActiveFocus();
        catcher.tabRequested(1);
        const firstFocus = panel.focusedChild(catcher);
        catcher.tabRequested(1);
        const secondFocus = panel.focusedChild(catcher);
        check(!!firstFocus && !!secondFocus && firstFocus !== secondFocus, "Tab advances from the currently focused control");
        panel.navigate("remote");
        const remoteNav = findNamed(catcher, "omaflip-navigation-remote");
        check(!!remoteNav, "persistent navigation focus target exists");
        if (remoteNav) {
            remoteNav.forceActiveFocus();
            const calls = fake.calls.length;
            catcher.moveRequested(0, 1);
            check(panel.focusedChild(catcher).objectName === "omaflip-navigation-files" && fake.calls.length === calls,
                "sidebar arrows move focus instead of sending device input");
        }
        panel.navigate("overview");
        let previous = "";
        for (const tool of tools) {
            const before = fake.calls.length;
            panel.navigate(tool);
            check(panel.currentPage === tool, "navigate " + tool);
            const active = [panel.remoteOpen, panel.filesOpen, panel.cliOpen, panel.appsOpen, panel.manageOpen, panel.devOpen, panel.companionOpen].filter(Boolean).length;
            check(active === 1, "exclusive " + tool);
            const names = fake.calls.slice(before).map(c => c.name);
            check(names.indexOf(tool + "Start") >= 0, "start " + tool);
            if (previous) check(names.indexOf(previous + "Stop") >= 0, "stop previous " + previous);
            const count = fake.calls.length;
            panel.navigate(tool);
            check(fake.calls.length === count && panel.currentPage === tool, "same page is idempotent " + tool);
            previous = tool;
        }
        for (const page of ["settings", "diagnostics", "overview"]) {
            panel.navigate(page);
            check(panel.currentPage === page && !panel.sessionOpen, "non-session " + page);
        }
        panel.navigate("remote");
        fake.selectedKey = "another";
        check(!panel.sessionOpen, "device switch releases session");
        panel.navigate("files");
        fake.selectedDevice = null;
        check(!panel.sessionOpen && !panel.canUseTools, "disconnect releases session");
        for (const tool of tools) panel.navigate(tool);
        check(!panel.sessionOpen && panel.currentPage === "overview", "disconnected tools unavailable");
        fake.selectedDevice = fake.makeDevice(false);
        check(!panel.canUseTools && panel.status.indexOf("DFU") >= 0, "unconfirmed DFU status and gate");
        for (const tool of tools) panel.navigate(tool);
        check(!panel.sessionOpen, "DFU cannot open tools");
        fake.reset(); fake.ready = false;
        panel.navigate("remote");
        check(!panel.canUseTools && !panel.sessionOpen, "unavailable service gate");
        fake.ready = true;
        for (let cycle = 0; cycle < 12; cycle++) {
            panel.open(); panel.navigate(tools[cycle % tools.length]); panel.close();
            check(!panel.opened && !panel.sessionOpen, "open close cycle " + cycle);
        }
        console.log("OMAFLIP_PANEL_DONE failures=" + failures);
        Qt.quit();
    }
    property int capturePhase: 0
    function popupWindow() {
        for (const object of panel.data) if (object.contentItem && object.fittedContentWidth) return object;
        return null;
    }
    Timer {
        interval: 400; running: true; repeat: true
        onTriggered: {
            if (suite.capturePhase === 0) {
                fake.closeSessions(); panel.open(); panel.navigate("overview"); suite.capturePhase++; return;
            }
            if (suite.capturePhase === 1 || suite.capturePhase === 3) {
                const popup = suite.popupWindow();
                suite.check(!!popup && popup.backingWindowVisible, "actual hosted Wayland popup is mapped");
                if (popup) {
                    const name = suite.capturePhase === 1 ? "panel-overview.png" : "panel-remote.png";
                    // Include the actual installed host's card background/chrome.
                    popup.contentItem[0].parent.parent.grabToImage(result => {
                        const path = Quickshell.env("OMAFLIP_TEST_ARTIFACTS") + "/" + name;
                        suite.check(result.saveToFile(path), "save actual hosted render " + path);
                    });
                }
                suite.capturePhase++; return;
            }
            if (suite.capturePhase === 2) { panel.navigate("remote"); suite.capturePhase++; return; }
            running = false; panel.navigate("overview"); suite.run();
        }
    }
}
