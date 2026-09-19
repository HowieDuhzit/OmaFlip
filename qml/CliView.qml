import QtQuick
import qs.Commons

Column {
    id: root
    property var service: null
    property color foreground: Color.foreground
    spacing: Style.space(8)

    readonly property var cli: {
        const device = service && service.selectedDevice
        return device && device.cli && typeof device.cli === "object" ? device.cli : ({open: false})
    }
    readonly property var commands: cli.commands || []
    readonly property var history: cli.history || []
    property string filter: ""
    property int historyIndex: -1
    property string draft: ""
    property string confirmText: ""

    readonly property string display: {
        const text = cli.output || ""
        if (!filter) return text
        const needle = filter.toLowerCase()
        return text.split("\n").filter(line => line.toLowerCase().indexOf(needle) >= 0).join("\n")
    }

    function send() {
        const text = commandInput.text.trim()
        if (!service || !text) return
        if (confirmText) return
        if (isDestructive(text)) { confirmText = text; return }
        historyIndex = -1
        draft = ""
        service.cliSend(text)
        commandInput.text = ""
    }
    function isDestructive(text) {
        const t = String(text).trim().toLowerCase()
        return t === "power off" || t.indexOf("power reboot") === 0 || t === "factory_reset" || t.indexOf("update install") === 0
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

    onVisibleChanged: if (visible) Qt.callLater(() => commandInput.forceActiveFocus())

    Text {
        width: parent.width
        text: cli.ready ? (cli.streaming ? "CLI · logging" : "CLI") : "Starting CLI…"
        textFormat: Text.PlainText
        color: root.foreground
        font.family: Style.font.family
        font.pixelSize: Style.font.body
        font.bold: true
    }
    Text {
        width: parent.width
        visible: !!cli.error
        text: cli.error || ""
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        color: root.foreground
        font.family: Style.font.family
        font.pixelSize: Style.font.caption
    }

    Rectangle {
        width: parent.width
        height: 180
        radius: Style.cornerRadius
        color: Qt.alpha(root.foreground, 0.04)
        border.width: 1
        border.color: Qt.alpha(root.foreground, 0.18)
        Flickable {
            id: logScroll
            anchors.fill: parent
            anchors.margins: 6
            clip: true
            contentWidth: width
            contentHeight: logText.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            Text {
                id: logText
                width: logScroll.width
                text: root.display.length ? root.display : "Waiting for the Flipper prompt…"
                textFormat: Text.PlainText
                wrapMode: Text.WrapAnywhere
                color: Qt.alpha(root.foreground, root.display.length ? 0.9 : 0.5)
                font.family: Style.font.family
                font.pixelSize: Style.font.caption
            }
            onContentHeightChanged: contentY = Math.max(0, contentHeight - height)
        }
    }

    Text {
        width: parent.width
        visible: filterInput.text.length === 0
        text: "Filter output"
        color: Qt.alpha(root.foreground, 0.4)
        font.family: Style.font.family
        font.pixelSize: Style.font.caption
    }
    TextInput {
        id: filterInput
        width: parent.width
        color: root.foreground
        font.family: Style.font.family
        font.pixelSize: Style.font.caption
        selectionColor: Qt.alpha(root.foreground, 0.25)
        selectedTextColor: root.foreground
        onTextChanged: root.filter = text
    }

    Flow {
        width: parent.width
        spacing: Style.space(4)
        Repeater {
            model: root.commands
            Rectangle {
                required property var modelData
                visible: modelData && modelData.name !== "start_rpc_session"
                width: chip.implicitWidth + Style.space(10)
                height: Style.space(22)
                radius: Style.cornerRadius
                color: Qt.alpha(root.foreground, chipMouse.containsMouse ? 0.12 : 0.05)
                border.width: 1
                border.color: Qt.alpha(root.foreground, 0.2)
                Text {
                    id: chip
                    anchors.centerIn: parent
                    text: modelData.name
                    color: root.foreground
                    font.family: Style.font.family
                    font.pixelSize: Style.font.caption
                }
                MouseArea {
                    id: chipMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.fill(modelData.name)
                }
            }
        }
    }

    TextInput {
        id: commandInput
        width: parent.width
        color: root.foreground
        font.family: Style.font.family
        font.pixelSize: Style.font.body
        selectionColor: Qt.alpha(root.foreground, 0.25)
        selectedTextColor: root.foreground
        onAccepted: root.send()
        Keys.onUpPressed: root.historyPrev()
        Keys.onDownPressed: root.historyNext()
    }

    Column {
        width: parent.width
        spacing: Style.space(8)
        visible: confirmText.length > 0
        Text {
            width: parent.width
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
            color: root.foreground
            font.family: Style.font.family
            font.pixelSize: Style.font.body
            text: "Run destructive command “" + confirmText + "”?"
        }
        Row {
            spacing: Style.space(8)
            Repeater {
                model: [{label: "Confirm", op: "ok"}, {label: "Cancel", op: "no"}]
                Rectangle {
                    required property var modelData
                    width: 88
                    height: Style.space(28)
                    radius: Style.cornerRadius
                    color: Qt.alpha(root.foreground, 0.08)
                    border.width: 1
                    border.color: Qt.alpha(root.foreground, 0.25)
                    Text { anchors.centerIn: parent; text: modelData.label; color: root.foreground; font.family: Style.font.family; font.pixelSize: Style.font.body }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (modelData.op === "ok" && root.service) {
                                root.service.cliSend(root.confirmText)
                                commandInput.text = ""
                            }
                            root.confirmText = ""
                        }
                    }
                }
            }
        }
    }

    Flow {
        width: parent.width
        spacing: Style.space(6)
        visible: confirmText.length === 0
        Repeater {
            model: [
                {label: "Send", op: "send"},
                {label: cli.streaming || cli.busy ? "Stop" : "Stop", op: "stop"},
                {label: "Save", op: "save"},
                {label: "Reconnect", op: "reconnect"}
            ]
            Rectangle {
                required property var modelData
                width: implicitWidth
                implicitWidth: btnLabel.implicitWidth + Style.space(16)
                height: Style.space(28)
                radius: Style.cornerRadius
                color: mouse.pressed ? Qt.alpha(root.foreground, 0.2) : (mouse.containsMouse ? Qt.alpha(root.foreground, 0.12) : Qt.alpha(root.foreground, 0.05))
                border.width: 1
                border.color: Qt.alpha(root.foreground, 0.25)
                Text {
                    id: btnLabel
                    anchors.centerIn: parent
                    text: modelData.label
                    color: root.foreground
                    font.family: Style.font.family
                    font.pixelSize: Style.font.body
                }
                MouseArea {
                    id: mouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (!root.service) return
                        if (modelData.op === "send") root.send()
                        else if (modelData.op === "stop") root.service.cliInterrupt()
                        else if (modelData.op === "save") root.service.cliSave()
                        else if (modelData.op === "reconnect") root.service.cliReconnect()
                    }
                }
            }
        }
    }

    Text {
        width: parent.width
        visible: !!(cli.saved)
        text: cli.saved ? "Saved " + cli.saved : ""
        textFormat: Text.PlainText
        wrapMode: Text.WrapAnywhere
        color: Qt.alpha(root.foreground, 0.65)
        font.family: Style.font.family
        font.pixelSize: Style.font.caption
    }
}
