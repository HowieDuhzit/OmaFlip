pragma ComponentBehavior: Bound
import QtQuick
import qs.Commons

Column {
    id: root
    property var service: null
    property color foreground: Color.foreground
    readonly property color accentColor: Color.accent
    readonly property bool inputActive: destinationInput.activeFocus || editor.activeFocus
    spacing: Style.space(12)

    readonly property var appsState: {
        const device = service && service.selectedDevice
        return device && device.apps && typeof device.apps === "object" ? device.apps : ({open: false})
    }
    readonly property var apps: appsState.apps || []
    readonly property bool busy: !!appsState.busy
    readonly property bool available: !!service && !!appsState.open && !!appsState.ready
    readonly property bool actionable: available && !busy && confirmKind === "" && pendingSavePath === ""
    readonly property bool scriptDirty: scriptDraft !== savedScriptText
    property int cursor: 0
    property string confirmKind: ""
    property string confirmPath: ""
    property string confirmHost: ""
    property string destDir: "/ext/apps/Misc"
    property string seenError: ""
    property string scriptDraft: ""
    property string scriptPath: ""
    property string savedScriptText: ""
    property string scriptSnapshotText: ""
    property string pendingSavePath: ""
    property string pendingSaveText: ""
    property bool sawSaveBusy: false

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
        if (!actionable || !apps[index]) return
        cursor = index
        destDir = "/ext/apps/" + (apps[index].category || "Misc")
    }
    function launch() {
        const item = current()
        if (actionable && item) service.appsLaunch(item.path)
    }
    function requestRemove() {
        const item = current()
        if (!actionable || !item) return
        confirmKind = "remove"
        confirmPath = item.path
    }
    function openScript() {
        const item = current()
        if (actionable && item && item.kind === "js") service.appsRead(item.path)
    }
    function saveScript() {
        if (!actionable || !appsState.script || !appsState.script.path) return
        pendingSavePath = appsState.script.path
        pendingSaveText = scriptDraft
        sawSaveBusy = false
        service.appsWrite(pendingSavePath, pendingSaveText)
    }
    function runConfirm() {
        if (!available || busy) return
        if (confirmKind === "remove") service.appsRemove(confirmPath)
        else if (confirmKind === "install" || confirmKind === "overwrite") {
            if (!confirmHost || !destDir.trim()) return
            service.appsInstall(confirmHost, destDir.trim(), confirmKind === "overwrite")
        }
        confirmKind = ""
        // Keep the pending source until the backend reports success or an
        // overwrite conflict; retry must refer to the same local file.
        confirmPath = ""
    }
    function offerDrop(urls) {
        if (!actionable || !urls || !urls.length) return
        confirmKind = "install"
        confirmHost = localPath(urls[0])
    }

    onServiceChanged: if (!service) {
        confirmKind = ""
        confirmPath = ""
        confirmHost = ""
        seenError = ""
        pendingSavePath = ""
        pendingSaveText = ""
        sawSaveBusy = false
        scriptPath = ""
        scriptSnapshotText = ""
    }
    onAppsChanged: cursor = Math.min(cursor, Math.max(0, apps.length - 1))
    onCursorChanged: list.positionViewAtIndex(cursor, ListView.Contain)
    Connections {
        target: root.service
        function onDevicesChanged() {
            const script = root.appsState.script || {}
            if (script.path && (script.path !== root.scriptPath ||
                (!root.pendingSavePath && (script.text || "") !== root.scriptSnapshotText))) {
                root.scriptPath = script.path
                root.scriptSnapshotText = script.text || ""
                root.savedScriptText = root.scriptSnapshotText
                root.scriptDraft = root.savedScriptText
            }
            if (root.pendingSavePath) {
                if (root.appsState.error || !root.appsState.open || !root.appsState.ready) {
                    root.pendingSavePath = ""
                    root.pendingSaveText = ""
                    root.sawSaveBusy = false
                } else if (root.appsState.busy) root.sawSaveBusy = true
                else if (root.sawSaveBusy) {
                    if (script.path === root.pendingSavePath) root.savedScriptText = root.pendingSaveText
                    root.pendingSavePath = ""
                    root.pendingSaveText = ""
                    root.sawSaveBusy = false
                }
            }
            const code = root.appsState.errorCode
            const key = (code || "") + (root.confirmHost || "")
            if (code === "exists" && root.confirmHost && key !== root.seenError) {
                root.seenError = key
                root.confirmKind = "overwrite"
            }
            if (!code) root.seenError = ""
        }
    }

    Row {
        width: parent.width
        spacing: Style.space(8)
        Text {
            width: Math.max(0, parent.width - appStatus.implicitWidth - parent.spacing)
            text: "Apps on SD card"
            color: root.foreground
            font.family: Style.font.family
            font.pixelSize: Style.font.body
            font.bold: true
            elide: Text.ElideRight
        }
        Text {
            id: appStatus
            text: root.busy ? "Working…" : root.apps.length + " installed"
            color: root.busy ? root.accentColor : Qt.alpha(root.foreground, 0.65)
            font.family: Style.font.family
            font.pixelSize: Style.font.caption
        }
    }
    Rectangle {
        width: parent.width
        height: errorLabel.implicitHeight + Style.space(20)
        visible: !!root.appsState.error
        radius: Style.cornerRadius
        color: Qt.alpha(Color.urgent, 0.09)
        border.color: Qt.alpha(Color.urgent, 0.35)
        Text {
            id: errorLabel
            anchors.fill: parent
            anchors.margins: Style.space(10)
            text: "App operation failed\n" + (root.appsState.error || "")
            textFormat: Text.PlainText
            wrapMode: Text.WrapAnywhere
            color: root.foreground
            font.family: Style.font.family
            font.pixelSize: Style.font.caption
        }
    }

    Rectangle {
        width: parent.width
        height: Style.space(244)
        radius: Style.cornerRadius
        color: Qt.alpha(root.foreground, 0.035)
        border.width: 1
        border.color: drop.containsDrag ? root.accentColor : Qt.alpha(root.foreground, 0.16)
        DropArea {
            id: drop
            anchors.fill: parent
            enabled: root.actionable
            onDropped: event => root.offerDrop(event.urls)
        }
        ListView {
            id: list
            anchors.fill: parent
            anchors.margins: Style.space(6)
            clip: true
            spacing: Style.space(3)
            visible: root.confirmKind === ""
            enabled: root.actionable
            boundsBehavior: Flickable.StopAtBounds
            model: root.apps
            delegate: Rectangle {
                id: appRow
                required property var modelData
                required property int index
                width: list.width
                height: Style.space(54)
                radius: Style.cornerRadius
                color: root.cursor === index ? Qt.alpha(root.accentColor, 0.14) : Qt.alpha(root.foreground, hover.containsMouse ? 0.07 : 0)
                border.width: root.cursor === index ? 1 : 0
                border.color: Qt.alpha(root.accentColor, 0.4)
                Rectangle {
                    id: kindBadge
                    anchors.left: parent.left
                    anchors.leftMargin: Style.space(10)
                    anchors.verticalCenter: parent.verticalCenter
                    width: Style.space(36)
                    height: Style.space(28)
                    radius: Style.cornerRadius
                    color: Qt.alpha(root.foreground, 0.06)
                    Text {
                        anchors.centerIn: parent
                        text: appRow.modelData.kind === "js" ? "JS" : "APP"
                        color: root.accentColor
                        font.family: Style.font.family
                        font.pixelSize: Style.font.caption
                        font.bold: true
                    }
                }
                Column {
                    anchors.left: kindBadge.right
                    anchors.leftMargin: Style.space(10)
                    anchors.right: parent.right
                    anchors.rightMargin: Style.space(10)
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Style.space(2)
                    Text {
                        width: parent.width
                        text: appRow.modelData.name
                        textFormat: Text.PlainText
                        elide: Text.ElideMiddle
                        color: root.foreground
                        font.family: Style.font.family
                        font.pixelSize: Style.font.body
                    }
                    Text {
                        width: parent.width
                        text: (appRow.modelData.category || "Misc") + (appRow.modelData.kind === "js" ? " · JavaScript" : " · Flipper application")
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        color: Qt.alpha(root.foreground, 0.65)
                        font.family: Style.font.family
                        font.pixelSize: Style.font.caption
                    }
                }
                MouseArea {
                    id: hover
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: root.select(appRow.index)
                    onDoubleClicked: {
                        root.select(appRow.index)
                        if (appRow.modelData.kind === "js") root.openScript()
                        else root.launch()
                    }
                }
            }
        }
        Text {
            anchors.centerIn: parent
            visible: root.confirmKind === "" && root.apps.length === 0
            text: root.busy ? "Reading apps from the Flipper…" : !root.appsState.open ? "App session unavailable.\nOpen Apps on a connected device." : root.appsState.error ? "Could not load applications.\nUse Refresh to retry." : "No applications installed.\nDrop a .fap or .js file to install."
            color: Qt.alpha(root.foreground, 0.7)
            font.family: Style.font.family
            font.pixelSize: Style.font.body
            width: parent.width - Style.space(40)
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
        }
        Column {
            anchors.centerIn: parent
            width: parent.width - Style.space(32)
            spacing: Style.space(12)
            visible: root.confirmKind !== ""
            Text {
                width: parent.width
                text: root.confirmKind === "remove" ? "Remove this application?" : root.confirmKind === "overwrite" ? "Replace installed file?" : "Install on device?"
                color: root.foreground
                font.family: Style.font.family
                font.pixelSize: Style.font.body
                font.bold: true
                wrapMode: Text.Wrap
            }
            Text {
                width: parent.width
                wrapMode: Text.WrapAnywhere
                textFormat: Text.PlainText
                maximumLineCount: 5
                elide: Text.ElideMiddle
                color: Qt.alpha(root.foreground, 0.8)
                font.family: Style.font.family
                font.pixelSize: Style.font.caption
                text: {
                    if (root.confirmKind === "remove") return root.confirmPath + "\nThis removes the file from the SD card."
                    if (root.confirmKind === "overwrite") return "Replace the installed file with " + root.basename(root.confirmHost) + "?\nDestination: " + root.destDir
                    return root.basename(root.confirmHost) + "\nDestination: " + root.destDir
                }
            }
            Flow {
                width: parent.width
                spacing: Style.space(8)
                ToolButton {
                    text: root.confirmKind === "remove" ? "Remove" : root.confirmKind === "overwrite" ? "Replace" : "Install"
                    foreground: root.foreground
                    destructive: root.confirmKind === "remove" || root.confirmKind === "overwrite"
                    primary: !destructive
                    enabled: root.available && !root.busy && (root.confirmKind === "remove" || (!!root.confirmHost && !!root.destDir.trim()))
                    onTriggered: root.runConfirm()
                }
                ToolButton {
                    text: "Cancel"
                    foreground: root.foreground
                    onTriggered: { root.confirmKind = ""; root.confirmHost = ""; root.confirmPath = "" }
                }
            }
        }
    }
    Text {
        width: parent.width
        visible: root.confirmKind === ""
        text: drop.containsDrag ? "Drop to install · Confirmation required" : "Double-click to launch an app or edit JavaScript."
        color: Qt.alpha(root.foreground, 0.65)
        font.family: Style.font.family
        font.pixelSize: Style.font.caption
        wrapMode: Text.Wrap
    }
    Flow {
        width: parent.width
        spacing: Style.space(6)
        visible: root.confirmKind === ""
        Repeater {
            model: [
                {label: "Launch", op: "launch"}, {label: "Edit JS", op: "edit"},
                {label: "Exit app", op: "exit"}, {label: "Refresh", op: "refresh"},
                {label: "Remove", op: "remove"}
            ]
            ToolButton {
                required property var modelData
                text: modelData.label
                foreground: root.foreground
                primary: modelData.op === "launch"
                destructive: modelData.op === "remove"
                enabled: root.actionable && (modelData.op === "edit" ? !!root.current() && root.current().kind === "js" : modelData.op === "launch" || modelData.op === "remove" ? !!root.current() : true)
                onTriggered: {
                    if (modelData.op === "launch") root.launch()
                    else if (modelData.op === "edit") root.openScript()
                    else if (modelData.op === "remove") root.requestRemove()
                    else if (modelData.op === "exit") root.service.appsExit()
                    else if (modelData.op === "refresh") root.service.appsRefresh()
                }
            }
        }
    }
    Column {
        width: parent.width
        spacing: Style.space(6)
        visible: root.confirmKind === ""
        Text {
            text: "Install destination"
            color: Qt.alpha(root.foreground, 0.75)
            font.family: Style.font.family
            font.pixelSize: Style.font.caption
        }
        InputField {
            id: destinationInput
            width: parent.width
            foreground: root.foreground
            placeholderText: "/ext/apps/Misc"
            text: root.destDir
            enabled: root.actionable
            onTextChanged: if (text !== root.destDir) root.destDir = text
        }
        Text {
            width: parent.width
            text: "Drop one .fap or .js file into the list. The destination is confirmed before installation."
            color: Qt.alpha(root.foreground, 0.65)
            font.family: Style.font.family
            font.pixelSize: Style.font.caption
            wrapMode: Text.Wrap
        }
    }
    Column {
        width: parent.width
        spacing: Style.space(8)
        visible: !!(root.appsState.script && root.appsState.script.path) && root.confirmKind === ""
        Text {
            width: parent.width
            text: "JavaScript editor" + (root.scriptDirty ? " · Unsaved changes" : "")
            color: root.foreground
            font.family: Style.font.family
            font.pixelSize: Style.font.body
            font.bold: true
            wrapMode: Text.Wrap
        }
        Text {
            width: parent.width
            text: (root.appsState.script && root.appsState.script.path) || ""
            textFormat: Text.PlainText
            elide: Text.ElideMiddle
            color: Qt.alpha(root.foreground, 0.65)
            font.family: Style.font.family
            font.pixelSize: Style.font.caption
        }
        Rectangle {
            width: parent.width
            height: Style.space(150)
            radius: Style.cornerRadius
            color: Qt.alpha(root.foreground, 0.04)
            border.width: 1
            border.color: editor.activeFocus ? root.accentColor : Qt.alpha(root.foreground, 0.2)
            Flickable {
                id: editorScroll
                anchors.fill: parent
                anchors.margins: Style.space(10)
                contentWidth: width
                contentHeight: Math.max(height, editor.contentHeight)
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                function ensureCursorVisible() {
                    const rect = editor.cursorRectangle
                    if (rect.y < contentY) contentY = rect.y
                    else if (rect.y + rect.height > contentY + height) contentY = rect.y + rect.height - height
                }
                TextEdit {
                    id: editor
                    width: editorScroll.width
                    height: Math.max(editorScroll.height, contentHeight)
                    text: root.scriptDraft
                    color: root.foreground
                    selectionColor: root.accentColor
                    selectedTextColor: Color.background
                    font.family: Style.font.family
                    font.pixelSize: Style.font.caption
                    wrapMode: TextEdit.WrapAnywhere
                    textFormat: TextEdit.PlainText
                    selectByMouse: true
                    activeFocusOnTab: true
                    readOnly: !root.actionable
                    onTextChanged: root.scriptDraft = text
                    onCursorRectangleChanged: editorScroll.ensureCursorVisible()
                    Keys.onEscapePressed: event => { focus = false; event.accepted = false }
                }
            }
        }
        Flow {
            width: parent.width
            spacing: Style.space(8)
            ToolButton {
                text: "Save JS"
                foreground: root.foreground
                primary: true
                enabled: root.actionable && root.scriptDirty
                onTriggered: root.saveScript()
            }
            ToolButton {
                text: "Run saved JS"
                foreground: root.foreground
                enabled: root.actionable && !!(root.appsState.script && root.appsState.script.path)
                onTriggered: root.service.appsLaunch(root.appsState.script.path)
            }
        }
        Text {
            width: parent.width
            visible: root.scriptDirty
            text: "Run uses the saved file on the device. Save your changes first to run this draft."
            color: Qt.alpha(root.foreground, 0.65)
            font.family: Style.font.family
            font.pixelSize: Style.font.caption
            wrapMode: Text.Wrap
        }
    }
}
