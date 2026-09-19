import QtQuick
import Quickshell
import Quickshell.Io

Item {
    id: root
    property var shell: null
    property var manifest: null
    property var devices: []
    property string selectedKey: ""
    property string serviceError: ""
    property string diagnostics: ""
    property bool ready: false
    property bool autoConnect: false
    property string preferredDevice: ""
    readonly property bool running: backend.running
    readonly property var selectedDevice: {
        for (let device of devices) if (device.key === selectedKey) return device
        return null
    }
    readonly property string state: !ready ? "Error" : (selectedDevice ? selectedDevice.state : (devices.length ? "Detecting" : "Disconnected"))

    function send(request) {
        if (backend.running) backend.write(JSON.stringify(request) + "\n")
    }
    function configure(enabled, preferred) {
        autoConnect = enabled
        preferredDevice = preferred || ""
        send({op: "configure", autoConnect: enabled, preferredDevice: preferredDevice})
    }
    function refresh() {
        if (selectedKey) send({op: "refresh", key: selectedKey})
    }
    function selectDevice(key) { send({op: "select", key: key}) }
    function restart() {
        serviceError = ""
        if (backend.running) {
            restarting = true
            backend.running = false
        } else backend.running = true
    }
    property bool restarting: false
    function accept(data) {
        try {
            const snapshot = JSON.parse(data)
            if (snapshot.protocol !== 1 || snapshot.type !== "snapshot" || !Array.isArray(snapshot.devices))
                throw new Error("Unsupported OmaFlip backend protocol")
            devices = snapshot.devices
            selectedKey = snapshot.selected
            serviceError = snapshot.error.reason || ""
            ready = true
        } catch (error) { serviceError = String(error) }
    }
    Process {
        id: backend
        command: [decodeURIComponent(Qt.resolvedUrl("scripts/run-backend").toString().replace(/^file:\/\//, ""))]
        stdinEnabled: true
        running: true
        onStarted: root.configure(root.autoConnect, root.preferredDevice)
        stdout: SplitParser { onRead: data => root.accept(data) }
        stderr: SplitParser {
            onRead: data => {
                root.diagnostics = (root.diagnostics + data + "\n").slice(-16384)
                root.serviceError = data
            }
        }
        onExited: (exitCode, exitStatus) => {
            root.ready = false
            root.devices = []
            root.selectedKey = ""
            if (!root.serviceError) root.serviceError = "OmaFlip backend stopped (exit " + exitCode + "). Choose Restart service."
            if (root.restarting) {
                root.restarting = false
                Qt.callLater(() => { backend.running = true })
            }
        }
    }
    Component.onDestruction: backend.running = false
}
