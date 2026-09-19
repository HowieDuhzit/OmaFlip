import QtQuick
import qs.Commons

Column {
    id: root
    property var service: null
    property var hostWidget: null
    property color foreground: Color.foreground
    spacing: Style.space(8)

    readonly property var dev: {
        const device = service && service.selectedDevice
        return device && device.dev && typeof device.dev === "object" ? device.dev : ({open: false})
    }
    readonly property var fam: dev.fam && typeof dev.fam === "object" ? dev.fam : ({})
    property string confirmKind: ""
    property string inspectKind: "ping"

    function applyProject() {
        const path = projectInput.text.trim()
        if (service) service.devProject(path)
        if (hostWidget && path) hostWidget.persist("devProject", path)
    }
    function fillProject() {
        if (projectInput.text.trim()) return
        const saved = hostWidget ? hostWidget.setting("devProject", "") : ""
        projectInput.text = saved || dev.defaultProject || ""
        if (projectInput.text) applyProject()
    }
    function runConfirm() {
        if (!service) return
        if (confirmKind === "deploy") service.devDeploy(true)
        confirmKind = ""
    }

    Text {
        width: parent.width
        text: dev.ready ? "Developer" : "Starting Dev…"
        color: root.foreground
        font.family: Style.font.family
        font.pixelSize: Style.font.body
        font.bold: true
    }
    Text {
        width: parent.width
        wrapMode: Text.Wrap
        textFormat: Text.PlainText
        color: Qt.alpha(root.foreground, 0.75)
        font.family: Style.font.family
        font.pixelSize: Style.font.caption
        text: {
            if (!dev.ufbt && !dev.ufbtCommand) return "ufbt is missing. Choose Install ufbt, then Build."
            if (!dev.ufbt) return "ufbt will run as python3 -m ufbt. Install ufbt for a PATH binary."
            const fam = root.fam.appid ? (root.fam.appid + (root.fam.name ? " · " + root.fam.name : "")) : "No application.fam"
            return "ufbt · " + fam + (dev.fap ? " · FAP ready" : "")
        }
    }
    Text {
        width: parent.width
        visible: !!dev.error
        wrapMode: Text.Wrap
        textFormat: Text.PlainText
        color: root.foreground
        font.family: Style.font.family
        font.pixelSize: Style.font.caption
        text: dev.error || ""
    }

    Text {
        width: parent.width
        text: "Project folder"
        color: Qt.alpha(root.foreground, 0.55)
        font.family: Style.font.family
        font.pixelSize: Style.font.caption
    }
    TextInput {
        id: projectInput
        width: parent.width
        color: root.foreground
        font.family: Style.font.family
        font.pixelSize: Style.font.body
        selectionColor: Qt.alpha(root.foreground, 0.25)
        selectedTextColor: root.foreground
        onAccepted: root.applyProject()
        onEditingFinished: root.applyProject()
        Component.onCompleted: root.fillProject()
    }
    onVisibleChanged: if (visible) root.fillProject()
    Text {
        width: parent.width
        text: "App ID for create"
        color: Qt.alpha(root.foreground, 0.55)
        font.family: Style.font.family
        font.pixelSize: Style.font.caption
    }
    TextInput {
        id: appIdInput
        width: parent.width
        color: root.foreground
        font.family: Style.font.family
        font.pixelSize: Style.font.body
        selectionColor: Qt.alpha(root.foreground, 0.25)
        selectedTextColor: root.foreground
        text: "hello_app"
    }

    Rectangle {
        width: parent.width
        height: 140
        radius: Style.cornerRadius
        color: Qt.alpha(root.foreground, 0.04)
        border.width: 1
        border.color: Qt.alpha(root.foreground, confirmKind ? 0.45 : 0.18)
        Flickable {
            id: logScroll
            anchors.fill: parent
            anchors.margins: 6
            clip: true
            visible: confirmKind === ""
            contentWidth: width
            contentHeight: logText.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            Text {
                id: logText
                width: logScroll.width
                text: (dev.log || "") + (dev.inspect ? "\n" + dev.inspect : "")
                textFormat: Text.PlainText
                wrapMode: Text.WrapAnywhere
                color: Qt.alpha(root.foreground, 0.9)
                font.family: Style.font.family
                font.pixelSize: Style.font.caption
            }
            onContentHeightChanged: contentY = Math.max(0, contentHeight - height)
        }
        Column {
            anchors.centerIn: parent
            width: parent.width - Style.space(24)
            spacing: Style.space(8)
            visible: confirmKind !== ""
            Text {
                width: parent.width
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
                color: root.foreground
                font.family: Style.font.family
                font.pixelSize: Style.font.body
                text: "Upload the built FAP and start it on the Flipper? This replaces an app with the same name."
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
                            onClicked: modelData.op === "ok" ? root.runConfirm() : (root.confirmKind = "")
                        }
                    }
                }
            }
        }
    }

    Flow {
        width: parent.width
        spacing: Style.space(6)
        visible: confirmKind === ""
        Repeater {
            model: [
                {label: "Install ufbt", op: "install"},
                {label: "Create", op: "create"},
                {label: "Build", op: "build"},
                {label: "Lint", op: "lint"},
                {label: "Update SDK", op: "sdk"},
                {label: "Deploy", op: "deploy"}
            ]
            Rectangle {
                required property var modelData
                width: implicitWidth
                implicitWidth: btn.implicitWidth + Style.space(16)
                height: Style.space(28)
                radius: Style.cornerRadius
                color: Qt.alpha(root.foreground, mouse.containsMouse ? 0.12 : 0.05)
                border.width: 1
                border.color: Qt.alpha(root.foreground, 0.25)
                Text {
                    id: btn
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
                        root.applyProject()
                        if (modelData.op === "install") root.service.devInstallUfbt()
                        else if (modelData.op === "create") root.service.devCreate(appIdInput.text)
                        else if (modelData.op === "build") root.service.devBuild()
                        else if (modelData.op === "lint") root.service.devLint()
                        else if (modelData.op === "sdk") root.service.devUpdateSdk()
                        else root.confirmKind = "deploy"
                    }
                }
            }
        }
    }

    Text {
        width: parent.width
        visible: confirmKind === ""
        text: "Inspector (read-only). GPIO, reboot, factory reset, and DFU stay off."
        wrapMode: Text.Wrap
        textFormat: Text.PlainText
        color: Qt.alpha(root.foreground, 0.65)
        font.family: Style.font.family
        font.pixelSize: Style.font.caption
    }
    Flow {
        width: parent.width
        spacing: Style.space(4)
        visible: confirmKind === ""
        Repeater {
            model: [
                {label: "Ping", kind: "ping"},
                {label: "Protobuf", kind: "protobuf"},
                {label: "Storage", kind: "storage"},
                {label: "Lock", kind: "lock"},
                {label: "Time", kind: "datetime"},
                {label: "Device", kind: "device"},
                {label: "Power", kind: "power"},
                {label: "Property", kind: "property"},
                {label: "Desktop", kind: "desktop"},
                {label: "Alert", kind: "alert"}
            ]
            Rectangle {
                required property var modelData
                width: implicitWidth
                implicitWidth: chip.implicitWidth + Style.space(12)
                height: Style.space(24)
                radius: Style.cornerRadius
                color: Qt.alpha(root.foreground, root.inspectKind === modelData.kind ? 0.12 : 0.04)
                border.width: 1
                border.color: Qt.alpha(root.foreground, 0.2)
                Text {
                    id: chip
                    anchors.centerIn: parent
                    text: modelData.label
                    color: root.foreground
                    font.family: Style.font.family
                    font.pixelSize: Style.font.caption
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        root.inspectKind = modelData.kind
                        if (root.service) root.service.devInspect(modelData.kind, modelData.kind === "property" ? "firmware" : "")
                    }
                }
            }
        }
    }
}
