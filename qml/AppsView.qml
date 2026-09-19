import QtQuick
import qs.Commons

Column {
    id: root
    property var service: null
    property color foreground: Color.foreground
    spacing: Style.space(8)

    readonly property var appsState: {
        const device = service && service.selectedDevice
        return device && device.apps && typeof device.apps === "object" ? device.apps : ({open: false})
    }
    readonly property var apps: appsState.apps || []
    property int cursor: 0
    property string confirmKind: ""
    property string confirmPath: ""
    property string confirmHost: ""
    property string destDir: "/ext/apps/Misc"
    property string seenError: ""
    property string scriptDraft: ""
    property string scriptPath: ""

    function current() { return apps[cursor] || null }
    function localPath(url) {
        let text = String(url)
        if (text.startsWith("file://")) text = decodeURIComponent(text.slice(7))
        return text
    }
    function basename(path) {
        const parts = String(path).split("/")
        return parts[parts.length - 1] || path
    }
    function select(index) {
        if (!apps[index]) return
        cursor = index
        destDir = "/ext/apps/" + (apps[index].category || "Misc")
    }
    function launch() {
        const item = current()
        if (item && service) service.appsLaunch(item.path)
    }
    function requestRemove() {
        const item = current()
        if (!item) return
        confirmKind = "remove"
        confirmPath = item.path
    }
    function openScript() {
        const item = current()
        if (item && item.kind === "js" && service) service.appsRead(item.path)
    }
    function saveScript() {
        if (!service || !appsState.script || !appsState.script.path) return
        service.appsWrite(appsState.script.path, scriptDraft)
    }
    function runConfirm() {
        if (!service) return
        if (confirmKind === "remove") service.appsRemove(confirmPath)
        else if (confirmKind === "install" || confirmKind === "overwrite")
            service.appsInstall(confirmHost, destDir, confirmKind === "overwrite")
        confirmKind = ""
        confirmHost = ""
        confirmPath = ""
    }
    function offerDrop(urls) {
        if (!urls || !urls.length) return
        confirmKind = "install"
        confirmHost = localPath(urls[0])
    }

    onAppsChanged: cursor = Math.min(cursor, Math.max(0, apps.length - 1))
    Connections {
        target: root.service
        function onDevicesChanged() {
            const script = root.appsState.script || {}
            if (script.path && script.path !== root.scriptPath) {
                root.scriptPath = script.path
                root.scriptDraft = script.text || ""
            }
            const code = root.appsState.errorCode
            const key = (code || "") + (root.confirmHost || "")
            if (code === "exists" && key !== root.seenError) {
                root.seenError = key
                root.confirmKind = "overwrite"
            }
        }
    }

    Text {
        width: parent.width
        text: appsState.ready ? (apps.length + " apps on SD") : "Reading /ext/apps…"
        textFormat: Text.PlainText
        color: root.foreground
        font.family: Style.font.family
        font.pixelSize: Style.font.body
        font.bold: true
    }
    Text {
        width: parent.width
        visible: !!appsState.error
        text: appsState.error || ""
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        color: root.foreground
        font.family: Style.font.family
        font.pixelSize: Style.font.caption
    }

    Rectangle {
        width: parent.width
        height: 200
        radius: Style.cornerRadius
        color: Qt.alpha(root.foreground, drop.containsDrag ? 0.1 : 0.04)
        border.width: 1
        border.color: Qt.alpha(root.foreground, drop.containsDrag || confirmKind ? 0.45 : 0.18)
        DropArea {
            id: drop
            anchors.fill: parent
            onDropped: drop => root.offerDrop(drop.urls)
        }
        ListView {
            id: list
            anchors.fill: parent
            anchors.margins: 4
            clip: true
            visible: confirmKind === ""
            boundsBehavior: Flickable.StopAtBounds
            model: root.apps
            delegate: Rectangle {
                required property var modelData
                required property int index
                width: list.width
                height: Style.space(26)
                radius: Style.cornerRadius
                color: Qt.alpha(root.foreground, root.cursor === index ? 0.12 : (hover.containsMouse ? 0.07 : 0))
                Row {
                    anchors.fill: parent
                    anchors.leftMargin: Style.space(8)
                    spacing: Style.space(8)
                    Text {
                        width: 28
                        text: modelData.kind === "js" ? "js" : "app"
                        color: Qt.alpha(root.foreground, 0.55)
                        font.family: Style.font.family
                        font.pixelSize: Style.font.caption
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        width: parent.width - 120
                        text: modelData.category + " / " + modelData.name
                        textFormat: Text.PlainText
                        elide: Text.ElideMiddle
                        color: root.foreground
                        font.family: Style.font.family
                        font.pixelSize: Style.font.body
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                MouseArea {
                    id: hover
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: root.select(index)
                    onDoubleClicked: {
                        root.select(index)
                        if (modelData.kind === "js") root.openScript()
                        else root.launch()
                    }
                }
            }
        }
        Text {
            anchors.centerIn: parent
            visible: confirmKind === "" && apps.length === 0 && !appsState.busy
            text: "No .fap or .js under /ext/apps. Drop a file to install."
            color: Qt.alpha(root.foreground, 0.55)
            font.family: Style.font.family
            font.pixelSize: Style.font.caption
            width: parent.width - 24
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
        }
        Column {
            anchors.centerIn: parent
            width: parent.width - Style.space(24)
            spacing: Style.space(10)
            visible: confirmKind !== ""
            Text {
                width: parent.width
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
                color: root.foreground
                font.family: Style.font.family
                font.pixelSize: Style.font.body
                text: {
                    if (confirmKind === "remove") return "Remove " + confirmPath + "?"
                    if (confirmKind === "overwrite") return "Replace the installed file with " + basename(confirmHost) + "?"
                    return "Install " + basename(confirmHost) + " into " + destDir + "?"
                }
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
                {label: "Launch", op: "launch"},
                {label: "Edit JS", op: "edit"},
                {label: "Remove", op: "remove"},
                {label: "Exit app", op: "exit"},
                {label: "Refresh", op: "refresh"}
            ]
            Rectangle {
                required property var modelData
                width: implicitWidth
                implicitWidth: btn.implicitWidth + Style.space(16)
                height: Style.space(28)
                radius: Style.cornerRadius
                color: mouse.pressed ? Qt.alpha(root.foreground, 0.2) : (mouse.containsMouse ? Qt.alpha(root.foreground, 0.12) : Qt.alpha(root.foreground, 0.05))
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
                        if (modelData.op === "launch") root.launch()
                        else if (modelData.op === "edit") root.openScript()
                        else if (modelData.op === "remove") root.requestRemove()
                        else if (modelData.op === "exit" && root.service) root.service.appsExit()
                        else if (modelData.op === "refresh" && root.service) root.service.appsRefresh()
                    }
                }
            }
        }
    }

    Column {
        width: parent.width
        spacing: Style.space(6)
        visible: !!(appsState.script && appsState.script.path) && confirmKind === ""
        Text {
            width: parent.width
            text: "Script " + ((appsState.script && appsState.script.path) || "")
            textFormat: Text.PlainText
            elide: Text.ElideMiddle
            color: Qt.alpha(root.foreground, 0.7)
            font.family: Style.font.family
            font.pixelSize: Style.font.caption
        }
        Rectangle {
            width: parent.width
            height: 90
            radius: Style.cornerRadius
            color: Qt.alpha(root.foreground, 0.04)
            border.width: 1
            border.color: Qt.alpha(root.foreground, 0.18)
            TextEdit {
                id: editor
                anchors.fill: parent
                anchors.margins: 6
                text: root.scriptDraft
                color: root.foreground
                font.family: Style.font.family
                font.pixelSize: Style.font.caption
                wrapMode: TextEdit.WrapAnywhere
                onTextChanged: root.scriptDraft = text
            }
        }
        Row {
            spacing: Style.space(6)
            Repeater {
                model: [{label: "Save JS", op: "save"}, {label: "Run JS", op: "run"}]
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
                            if (modelData.op === "save") root.saveScript()
                            else if (root.service && root.appsState.script) root.service.appsLaunch(root.appsState.script.path)
                        }
                    }
                }
            }
        }
    }
}
