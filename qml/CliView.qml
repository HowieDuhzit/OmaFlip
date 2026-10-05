import QtQuick
import qs.Commons

Column {
    id: root
    property var service: null
    property color foreground: Color.foreground
    spacing: Style.space(10)

    readonly property var cli: {
        const device = service && service.selectedDevice
        return device && device.cli && typeof device.cli === "object" ? device.cli : ({open: false})
    }
    readonly property var commands: cli.commands || []
    readonly property var history: cli.history || []
    readonly property bool canSend: !!service && !!cli.ready && !cli.busy && !cli.streaming
    readonly property bool inputActive: commandInput.activeFocus || filterInput.activeFocus
    property string filter: ""
    property int historyIndex: -1
    property string draft: ""
    property string confirmText: ""
    property bool commandsExpanded: false

    readonly property string display: {
        const text = cli.output || ""
        if (!filter) return text
        const needle = filter.toLowerCase()
        return text.split("\n").filter(line => line.toLowerCase().indexOf(needle) >= 0).join("\n")
    }

    function send() {
        const text = commandInput.text.trim()
        if (!canSend || !text || confirmText) return
        if (isDestructive(text)) { confirmText = text; return }
        historyIndex = -1
        draft = ""
        service.cliSend(text)
        commandInput.text = ""
    }
    function isDestructive(text) {
        const t = String(text).trim().toLowerCase().replace(/\s+/g, " ")
        return t === "power off" || t.indexOf("power reboot") === 0 || t === "reboot"
            || t === "factory_reset" || t.indexOf("update install") === 0
            || t.indexOf("storage erase") === 0 || t === "dfu"
            || t.indexOf("rm ") === 0 || t.indexOf("format") === 0
    }
    function historyPrev() {
        if (!history.length) return
        if (historyIndex < 0) { draft = commandInput.text; historyIndex = history.length }
        historyIndex = Math.max(0, historyIndex - 1)
        commandInput.text = history[historyIndex]
    }
    function historyNext() {
        if (historyIndex < 0) return
        historyIndex += 1
        if (historyIndex >= history.length) {
            historyIndex = -1
            commandInput.text = draft
            return
        }
        commandInput.text = history[historyIndex]
    }
    function fill(name) { commandInput.text = name; commandInput.forceActiveFocus() }

    onServiceChanged: if (!service) { confirmText = ""; historyIndex = -1 }
    onVisibleChanged: if (visible) Qt.callLater(() => commandInput.forceActiveFocus())

    Row {
        width: parent.width
        spacing: Style.space(8)
        Rectangle {
            width: Style.space(8)
            height: width
            radius: width / 2
            anchors.verticalCenter: parent.verticalCenter
            color: cli.error ? Color.urgent : (cli.ready ? Color.accent : Qt.alpha(root.foreground, 0.35))
        }
        Text {
            width: parent.width - Style.space(16)
            text: !root.service ? "No device selected" : (cli.streaming ? "Live stream · Stop to interrupt" : (cli.busy ? "CLI · working…" : (cli.ready ? "Terminal · ready" : (cli.error ? "Terminal · unavailable" : "Connecting to CLI…"))))
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            color: root.foreground
            font.family: Style.font.family
            font.pixelSize: Style.font.body
            font.bold: true
        }
    }
    Text {
        width: parent.width
        visible: !!cli.error
        text: "CLI: " + (cli.error || "")
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        color: Color.urgent
        font.family: Style.font.family
        font.pixelSize: Style.font.caption
    }

    Rectangle {
        width: parent.width
        height: Style.space(200)
        radius: Style.cornerRadius
        color: Qt.alpha(root.foreground, 0.035)
        border.width: 1
        border.color: Qt.alpha(root.foreground, 0.16)
        Flickable {
            id: logScroll
            anchors.fill: parent
            anchors.margins: Style.space(10)
            clip: true
            contentWidth: width
            contentHeight: logText.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            Text {
                id: logText
                width: logScroll.width
                text: root.display.length ? root.display : (root.filter ? "No output matches this filter." : "Waiting for the Flipper prompt…")
                textFormat: Text.PlainText
                wrapMode: Text.WrapAnywhere
                color: Qt.alpha(root.foreground, root.display.length ? 0.9 : 0.55)
                font.family: "monospace"
                font.pixelSize: Style.font.caption
            }
            onContentHeightChanged: contentY = Math.max(0, contentHeight - height)
        }
    }
    InputField {
        id: filterInput
        width: parent.width
        foreground: root.foreground
        placeholderText: "Filter terminal output…"
        onTextChanged: root.filter = text
    }

    Column {
        width: parent.width
        spacing: Style.space(6)
        Text {
            text: "COMMAND"
            color: Qt.alpha(root.foreground, 0.6)
            font.family: Style.font.family
            font.pixelSize: Style.font.caption
            font.bold: true
        }
        InputField {
            id: commandInput
            width: parent.width
            foreground: root.foreground
            font.family: "monospace"
            placeholderText: "Enter a command, e.g. help"
            enabled: root.confirmText.length === 0
            onAccepted: root.send()
            Keys.onUpPressed: root.historyPrev()
            Keys.onDownPressed: root.historyNext()
        }
        Text {
            width: parent.width
            text: "Enter to send · ↑ / ↓ command history" + (cli.busy || cli.streaming ? " · Stop before sending another command" : "")
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            color: Qt.alpha(root.foreground, 0.55)
            font.family: Style.font.family
            font.pixelSize: Style.font.caption
        }
    }

    Rectangle {
        width: parent.width
        height: confirmation.implicitHeight + Style.space(24)
        visible: root.confirmText.length > 0
        radius: Style.cornerRadius
        color: Qt.alpha(Color.urgent, 0.07)
        border.width: 1
        border.color: Qt.alpha(Color.urgent, 0.4)
        Column {
            id: confirmation
            anchors.fill: parent
            anchors.margins: Style.space(12)
            spacing: Style.space(8)
            Text {
                width: parent.width
                wrapMode: Text.WrapAnywhere
                textFormat: Text.PlainText
                color: root.foreground
                font.family: Style.font.family
                font.pixelSize: Style.font.body
                text: "Run destructive command “" + root.confirmText + "”? This can change or erase device data."
            }
            Flow {
                width: parent.width
                spacing: Style.space(8)
                ToolButton {
                    text: "Confirm command"
                    foreground: root.foreground
                    destructive: true
                    enabled: root.canSend
                    onTriggered: {
                        if (!root.canSend || !root.confirmText) return
                        root.service.cliSend(root.confirmText)
                        root.historyIndex = -1
                        root.draft = ""
                        commandInput.text = ""
                        root.confirmText = ""
                    }
                }
                ToolButton {
                    text: "Cancel"
                    foreground: root.foreground
                    onTriggered: root.confirmText = ""
                }
            }
        }
    }

    Flow {
        width: parent.width
        spacing: Style.space(6)
        visible: root.confirmText.length === 0
        ToolButton {
            text: "Send command"
            foreground: root.foreground
            primary: true
            enabled: root.canSend && commandInput.text.trim().length > 0
            onTriggered: root.send()
        }
        ToolButton {
            text: "Stop"
            foreground: root.foreground
            enabled: !!root.service && !!cli.ready && (!!cli.busy || !!cli.streaming)
            onTriggered: root.service.cliInterrupt()
        }
        ToolButton {
            text: "Save log"
            foreground: root.foreground
            enabled: !!root.service && !!cli.open && (cli.output || "").length > 0
            onTriggered: root.service.cliSave()
        }
        ToolButton {
            text: "Reconnect"
            foreground: root.foreground
            enabled: !!root.service && !root.service.cliRestart
            onTriggered: root.service.cliReconnect()
        }
    }
    Text {
        width: parent.width
        visible: !!cli.saved
        text: cli.saved ? "Log saved · " + cli.saved : ""
        textFormat: Text.PlainText
        wrapMode: Text.WrapAnywhere
        color: Qt.alpha(root.foreground, 0.65)
        font.family: Style.font.family
        font.pixelSize: Style.font.caption
    }

    ToolButton {
        text: root.commandsExpanded ? "Hide available commands" : "Browse available commands"
        foreground: root.foreground
        enabled: root.commands.length > 0 && root.confirmText.length === 0
        onTriggered: root.commandsExpanded = !root.commandsExpanded
    }
    Flow {
        width: parent.width
        spacing: Style.space(4)
        visible: root.commandsExpanded
        Repeater {
            model: root.commands
            ToolButton {
                required property var modelData
                visible: !!modelData && modelData.name !== "start_rpc_session"
                text: modelData ? modelData.name : ""
                foreground: root.foreground
                enabled: root.confirmText.length === 0
                onTriggered: root.fill(modelData.name)
            }
        }
    }
}
