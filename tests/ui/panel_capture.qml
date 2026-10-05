import QtQuick
import Quickshell

// Release documentation only: this loads the real Panel, never Service.qml.
ShellRoot {
    id: suite
    PreviewService { id: fake }
    QtObject {
        id: host
        property var service: fake
        function setting(name, fallback) { return fallback; }
        function persist(name, value) {}
    }
    Panel { id: panel; hostWidget: host; manageIpc: false }
    readonly property var pages: ["overview", "remote", "files", "cli", "apps", "manage", "dev", "companion", "disconnected"]
    property int index: 0
    property int phase: 0
    property int retries: 0
    function popupWindow() {
        for (const object of panel.data) if (object.contentItem && object.fittedContentWidth) return object;
        return null;
    }
    Timer {
        interval: 200; running: true; repeat: true
        onTriggered: {
            if (suite.phase === 0) {
                panel.navigate("overview");
                fake.prepare(suite.pages[suite.index] === "disconnected");
                panel.open();
                if (suite.pages[suite.index] !== "disconnected") panel.navigate(suite.pages[suite.index]);
                suite.phase = 1; suite.retries = 0; return;
            }
            if (suite.phase === 1) {
                const popup = suite.popupWindow();
                if (!popup || !popup.backingWindowVisible) {
                    if (++suite.retries > 30) { console.error("OMAFLIP_CAPTURE_FAIL popup unmapped"); Qt.quit(); }
                    return;
                }
                // Simulate an LCD event after Remote's Canvas has mounted.
                if (suite.pages[suite.index] === "remote") fake.frameData = fake.fixtureFrame;
                suite.phase = 2; return;
            }
            if (suite.phase === 2) {
                const popup = suite.popupWindow();
                const card = popup.contentItem[0].parent.parent;
                if (card.width < 100 || card.height < 100) { console.error("OMAFLIP_CAPTURE_FAIL invalid card geometry"); Qt.quit(); return; }
                suite.phase = 3;
                // Only the actual QML popup card, never desktop pixels.
                const requested = card.grabToImage(result => {
                    const path = Quickshell.env("OMAFLIP_TEST_ARTIFACTS") + "/panel-" + suite.pages[suite.index] + ".png";
                    if (!result.saveToFile(path)) { console.error("OMAFLIP_CAPTURE_FAIL " + path); Qt.quit(); return; }
                    console.log("OMAFLIP_CAPTURE_SAVED " + suite.pages[suite.index] + " " + card.width + "x" + card.height);
                    suite.index++;
                    if (suite.index === suite.pages.length) {
                        panel.close(); console.log("OMAFLIP_CAPTURE_DONE count=" + suite.pages.length); Qt.quit();
                    } else suite.phase = 0;
                });
                if (!requested) { console.error("OMAFLIP_CAPTURE_FAIL grab rejected"); Qt.quit(); }
            }
        }
    }
}
