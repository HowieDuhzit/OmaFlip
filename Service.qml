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
    property string framePng: ""
    property string frameData: ""
    property int frameOrientation: 0
    property var firmware: ({})
    property var packs: ({})
    property string backendVersion: ""
    readonly property bool running: backend.running
    readonly property var selectedDevice: {
        for (let device of devices) if (device.key === selectedKey) return device
        return null
    }
    readonly property string state: !ready ? "Error" : (selectedDevice ? selectedDevice.state : (devices.length ? "Detecting" : "Disconnected"))

    function send(request) {
        if (backend.running) backend.write(JSON.stringify(request) + "\n")
    }
    function configure(enabled, preferred, notifyConnect, notifyError) {
        autoConnect = enabled
        preferredDevice = preferred || ""
        send({
            op: "configure",
            autoConnect: enabled,
            preferredDevice: preferredDevice,
            notifyConnect: notifyConnect !== false,
            notifyError: notifyError !== false
        })
    }
    function refresh() {
        if (selectedKey) send({op: "refresh", key: selectedKey})
    }
    function selectDevice(key) { send({op: "select", key: key}) }
    function remoteStart() { if (selectedKey) send({op: "remoteStart", key: selectedKey}) }
    function remoteStop() {
        if (selectedKey) send({op: "remoteStop", key: selectedKey})
        framePng = ""
        frameData = ""
        frameOrientation = 0
    }
    function input(button, type) { if (selectedKey) send({op: "input", key: selectedKey, button: button, type: type || "short"}) }
    function screenshot() { if (selectedKey) send({op: "screenshot", key: selectedKey}) }
    property var fileJobs: []
    function pumpFiles() {
        if (!fileJobs.length) return
        const device = selectedDevice
        if (!device || !device.files || device.files.busy) return
        send(fileJobs.shift())
    }
    function enqueueFile(job) {
        job.key = selectedKey
        fileJobs.push(job)
        pumpFiles()
    }
    function filesStart() { if (selectedKey) send({op: "filesStart", key: selectedKey}) }
    function filesStop() { if (selectedKey) { fileJobs = []; send({op: "filesStop", key: selectedKey}) } }
    function filesList(path) { enqueueFile({op: "filesList", path: path}) }
    function filesPreview(path) { enqueueFile({op: "filesPreview", path: path}) }
    function filesDownload(path, overwrite) { enqueueFile({op: "filesDownload", path: path, overwrite: !!overwrite}) }
    function filesUpload(path, hostPath, overwrite) { enqueueFile({op: "filesUpload", path: path, hostPath: hostPath, overwrite: !!overwrite}) }
    function filesMkdir(path) { enqueueFile({op: "filesMkdir", path: path}) }
    function filesRename(oldPath, newPath) { enqueueFile({op: "filesRename", oldPath: oldPath, newPath: newPath}) }
    function filesDelete(path, recursive) { enqueueFile({op: "filesDelete", path: path, recursive: !!recursive}) }
    property bool cliRestart: false
    function cliStart() { if (selectedKey) send({op: "cliStart", key: selectedKey}) }
    function cliStop() { if (selectedKey) send({op: "cliStop", key: selectedKey}) }
    function cliSend(text) { if (selectedKey) send({op: "cliSend", key: selectedKey, text: text}) }
    function cliInterrupt() { if (selectedKey) send({op: "cliInterrupt", key: selectedKey}) }
    function cliSave() { if (selectedKey) send({op: "cliSave", key: selectedKey}) }
    function cliReconnect() {
        if (!selectedKey) return
        cliRestart = true
        send({op: "cliStop", key: selectedKey})
    }
    function appsStart() { if (selectedKey) send({op: "appsStart", key: selectedKey}) }
    function appsStop() { if (selectedKey) send({op: "appsStop", key: selectedKey}) }
    function appsRefresh() { if (selectedKey) send({op: "appsRefresh", key: selectedKey}) }
    function appsLaunch(path) { if (selectedKey) send({op: "appsLaunch", key: selectedKey, path: path}) }
    function appsExit() { if (selectedKey) send({op: "appsExit", key: selectedKey}) }
    function appsRemove(path) { if (selectedKey) send({op: "appsRemove", key: selectedKey, path: path}) }
    function appsInstall(hostPath, destDir, overwrite) {
        if (selectedKey) send({op: "appsInstall", key: selectedKey, hostPath: hostPath, destDir: destDir || "", overwrite: !!overwrite})
    }
    function appsRead(path) { if (selectedKey) send({op: "appsRead", key: selectedKey, path: path}) }
    function appsWrite(path, text) { if (selectedKey) send({op: "appsWrite", key: selectedKey, path: path, text: text}) }
    function manageStart() { if (selectedKey) send({op: "manageStart", key: selectedKey}) }
    function manageStop() { if (selectedKey) send({op: "manageStop", key: selectedKey}) }
    function backupCreate() { if (selectedKey) send({op: "backupCreate", key: selectedKey}) }
    function backupRefresh() { if (selectedKey) send({op: "backupRefresh", key: selectedKey}) }
    function backupRestore(archive, confirmOrigin, confirmVersion) {
        if (selectedKey) send({op: "backupRestore", key: selectedKey, archive: archive, confirmOrigin: !!confirmOrigin, confirmVersion: !!confirmVersion})
    }
    function firmwareCheck(provider) {
        send({op: "firmwareCheck", key: selectedKey, channel: "release", provider: provider || "official"})
    }
    function firmwareDownload() { send({op: "firmwareDownload", key: selectedKey}) }
    function firmwareApply(replaceOrigin) { if (selectedKey) send({op: "firmwareApply", key: selectedKey, replaceOrigin: !!replaceOrigin}) }
    function packsCheck() { send({op: "packsCheck"}) }
    function packsDownload(id) { send({op: "packsDownload", id: id}) }
    function packsInstall() { if (selectedKey) send({op: "packsInstall", key: selectedKey}) }
    function packsRemove(name) { if (selectedKey) send({op: "packsRemove", key: selectedKey, name: name}) }
    function devStart() { if (selectedKey) send({op: "devStart", key: selectedKey}) }
    function devStop() { if (selectedKey) send({op: "devStop", key: selectedKey}) }
    function devProject(path) { if (selectedKey) send({op: "devProject", key: selectedKey, path: path}) }
    function devCreate(appId) { if (selectedKey) send({op: "devCreate", key: selectedKey, appId: appId}) }
    function devBuild() { if (selectedKey) send({op: "devBuild", key: selectedKey}) }
    function devLint() { if (selectedKey) send({op: "devLint", key: selectedKey}) }
    function devUpdateSdk() { if (selectedKey) send({op: "devUpdateSdk", key: selectedKey}) }
    function devInstallUfbt() { if (selectedKey) send({op: "devInstallUfbt", key: selectedKey}) }
    function devDeploy(overwrite) { if (selectedKey) send({op: "devDeploy", key: selectedKey, overwrite: !!overwrite}) }
    function devInspect(kind, arg) { if (selectedKey) send({op: "devInspect", key: selectedKey, kind: kind, arg: arg || ""}) }
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
            if (snapshot.protocol !== 1)
                throw new Error("Unsupported OmaFlip backend protocol")
            if (snapshot.type === "frame") {
                if (snapshot.key === selectedKey && snapshot.data) {
                    frameData = snapshot.data
                    frameOrientation = snapshot.orientation || 0
                }
                return
            }
            if (snapshot.type !== "snapshot" || !Array.isArray(snapshot.devices))
                throw new Error("Unsupported OmaFlip backend protocol")
            devices = snapshot.devices
            selectedKey = snapshot.selected
            backendVersion = snapshot.version || ""
            serviceError = snapshot.error.reason || ""
            ready = true
            firmware = snapshot.firmware || {}
            packs = snapshot.packs || {}
            pumpFiles()
            if (cliRestart) {
                const device = selectedDevice
                if (!device || !device.cli || !device.cli.open) {
                    cliRestart = false
                    cliStart()
                }
            }
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
