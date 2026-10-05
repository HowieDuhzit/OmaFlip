pragma ComponentBehavior: Bound
import QtQuick
import qs.Commons

Column {
    id: root
    property var service: null
    property color foreground: Color.foreground
    readonly property color accentColor: Color.accent
    readonly property bool inputActive: nameInput.activeFocus
    spacing: Style.space(12)

    readonly property var files: {
        const device = service && service.selectedDevice
        return device && device.files && typeof device.files === "object" ? device.files : ({open: false})
    }
    readonly property var entries: files.entries || []
    readonly property bool busy: !!files.busy
    readonly property bool available: !!service && !!files.open
    readonly property bool actionable: available && !busy && confirmKind === "" && nameMode === ""
    readonly property int selectionCount: selectedEntries().length
    readonly property bool hasDownload: selectedEntries().some(item => item.type !== "dir")
    readonly property bool validName: !!nameInput.text.trim() && nameInput.text.trim() !== "." && nameInput.text.trim() !== ".." && nameInput.text.indexOf("/") === -1
    property int cursor: 0
    property var selected: ({})
    property string confirmKind: ""
    property string confirmPath: ""
    property string confirmHost: ""
    property var confirmHosts: []
    property string nameMode: ""
    property string nameSeed: ""
    property string dropHint: ""
    property string seenError: ""

    function selectedPaths() {
        const out = []
        for (let i = 0; i < entries.length; i++) {
            const name = entries[i].name
            if (root.selected[name]) out.push(root.join(files.path, name))
        }
        if (!out.length && entries[cursor]) out.push(root.join(files.path, entries[cursor].name))
        return out
    }
    function selectedEntries() {
        const paths = selectedPaths()
        const map = {}
        for (let i = 0; i < entries.length; i++) map[root.join(files.path, entries[i].name)] = entries[i]
        return paths.map(path => map[path]).filter(item => !!item)
    }
    function join(dir, name) {
        if (!dir || dir === "/") return "/" + name
        return dir + "/" + name
    }
    function basename(path) {
        const parts = String(path).split("/")
        return parts[parts.length - 1] || path
    }
    function localPath(url) {
        let text = String(url)
        if (text.startsWith("file://")) text = decodeURIComponent(text.slice(7))
        return text
    }
    function sizeLabel(bytes) {
        const size = Number(bytes) || 0
        if (size < 1024) return size + " B"
        if (size < 1048576) return (size / 1024).toFixed(1) + " KB"
        return (size / 1048576).toFixed(1) + " MB"
    }
    function toggle(index, multi) {
        if (!actionable || !entries[index]) return
        cursor = index
        const name = entries[index].name
        if (!multi) {
            const only = ({})
            only[name] = true
            selected = only
            return
        }
        const next = Object.assign({}, selected)
        if (next[name]) delete next[name]
        else next[name] = true
        selected = next
    }
    function openItem(index) {
        const item = entries[index]
        if (!actionable || !item) return
        cursor = index
        const path = root.join(files.path, item.name)
        if (item.type === "dir") {
            selected = {}
            service.filesList(path)
        } else service.filesPreview(path)
    }
    function goParent() {
        if (!actionable || !files.parent) return
        selected = {}
        service.filesList(files.parent)
    }
    function requestDownload() {
        if (!actionable) return
        const items = selectedEntries().filter(item => item.type !== "dir")
        if (!items.length) return
        confirmKind = "download"
        confirmHosts = items.map(item => root.join(files.path, item.name))
    }
    function requestDelete() {
        if (!actionable) return
        const items = selectedEntries()
        if (!items.length) return
        confirmKind = "delete"
        confirmHosts = items.map(item => root.join(files.path, item.name))
    }
    function requestRename() {
        if (!actionable) return
        const items = selectedEntries()
        if (items.length !== 1) return
        nameMode = "rename"
        nameSeed = items[0].name
        nameInput.text = items[0].name
        nameInput.forceActiveFocus()
        nameInput.selectAll()
    }
    function requestMkdir() {
        if (!actionable) return
        nameMode = "mkdir"
        nameSeed = ""
        nameInput.text = ""
        nameInput.forceActiveFocus()
    }
    function submitName() {
        if (!available || busy || !validName || confirmKind !== "") return
        const name = nameInput.text.trim()
        if (nameMode === "mkdir") service.filesMkdir(root.join(files.path, name))
        else if (nameMode === "rename") {
            const items = selectedEntries()
            if (items.length === 1) service.filesRename(root.join(files.path, items[0].name), root.join(files.path, name))
        }
        nameMode = ""
        nameInput.focus = false
    }
    function runConfirm() {
        if (!available || busy) return
        if (confirmKind === "delete") {
            for (const path of confirmHosts) {
                const item = selectedEntries().find(entry => root.join(files.path, entry.name) === path)
                service.filesDelete(path, !!(item && item.type === "dir"))
            }
        } else if (confirmKind === "download") {
            for (const path of confirmHosts) service.filesDownload(path, false)
        } else if (confirmKind === "upload") {
            for (const host of confirmHosts) service.filesUpload(root.join(files.path, basename(host)), host, false)
        } else if (confirmKind === "overwrite") {
            if (files.lastOp === "upload") service.filesUpload(files.target, files.hostPath, true)
            else service.filesDownload(files.target, true)
        }
        confirmKind = ""
        confirmHosts = []
        confirmPath = ""
        confirmHost = ""
    }
    function offerDrop(urls) {
        if (!actionable) return
        const hosts = []
        for (let i = 0; i < urls.length; i++) {
            const path = localPath(urls[i])
            if (path) hosts.push(path)
        }
        if (!hosts.length) return
        confirmKind = "upload"
        confirmHosts = hosts
        dropHint = ""
    }

    onServiceChanged: if (!service) {
        confirmKind = ""
        confirmHosts = []
        confirmPath = ""
        confirmHost = ""
        nameMode = ""
        dropHint = ""
        seenError = ""
        selected = ({})
    }
    onEntriesChanged: {
        cursor = Math.min(cursor, Math.max(0, entries.length - 1))
        const live = {}
        for (let i = 0; i < entries.length; i++) if (selected[entries[i].name]) live[entries[i].name] = true
        selected = live
    }
    onCursorChanged: list.positionViewAtIndex(cursor, ListView.Contain)
    onNameModeChanged: if (nameMode === "") nameInput.focus = false
    Connections {
        target: root.service
        function onDevicesChanged() {
            const code = root.files.errorCode
            const key = (code || "") + (root.files.target || "")
            if (code === "exists" && key !== root.seenError) {
                root.seenError = key
                root.confirmKind = "overwrite"
            }
        }
    }

    Row {
        width: parent.width
        spacing: Style.space(8)
        Text {
            width: Math.max(0, parent.width - folderStatus.implicitWidth - parent.spacing)
            text: root.files.path || "/ext"
            textFormat: Text.PlainText
            color: root.foreground
            font.family: Style.font.family
            font.pixelSize: Style.font.body
            font.bold: true
            elide: Text.ElideMiddle
        }
        Text {
            id: folderStatus
            text: root.busy ? "Working…" : root.entries.length + " items"
            color: root.busy ? root.accentColor : Qt.alpha(root.foreground, 0.65)
            font.family: Style.font.family
            font.pixelSize: Style.font.caption
        }
    }
    Rectangle {
        width: parent.width
        height: errorLabel.implicitHeight + Style.space(20)
        visible: !!root.files.error
        radius: Style.cornerRadius
        color: Qt.alpha(Color.urgent, 0.09)
        border.color: Qt.alpha(Color.urgent, 0.35)
        Text {
            id: errorLabel
            anchors.fill: parent
            anchors.margins: Style.space(10)
            text: "File operation failed\n" + (root.files.error || "")
            textFormat: Text.PlainText
            wrapMode: Text.WrapAnywhere
            color: root.foreground
            font.family: Style.font.family
            font.pixelSize: Style.font.caption
        }
    }
    Text {
        width: parent.width
        visible: !!(root.files.transfer && root.files.transfer.path)
        text: {
            const t = root.files.transfer || {}
            if (!t.path) return ""
            return (t.direction === "upload" ? "Uploading " : "Downloading ") + t.path + "\n" + root.sizeLabel(t.bytes) + " / " + root.sizeLabel(t.total) + (t.done ? " · Complete" : "")
        }
        textFormat: Text.PlainText
        wrapMode: Text.WrapAnywhere
        color: Qt.alpha(root.foreground, 0.75)
        font.family: Style.font.family
        font.pixelSize: Style.font.caption
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
            onEntered: root.dropHint = "Drop to upload into " + (root.files.path || "/ext")
            onExited: root.dropHint = ""
            onDropped: event => root.offerDrop(event.urls)
        }
        ListView {
            id: list
            anchors.fill: parent
            anchors.margins: Style.space(6)
            clip: true
            spacing: Style.space(3)
            visible: root.confirmKind === "" && root.nameMode === ""
            enabled: root.actionable
            boundsBehavior: Flickable.StopAtBounds
            model: root.entries
            delegate: Rectangle {
                id: fileRow
                required property var modelData
                required property int index
                width: list.width
                height: Style.space(40)
                radius: Style.cornerRadius
                color: root.cursor === index || root.selected[modelData.name] ? Qt.alpha(root.accentColor, 0.14) : Qt.alpha(root.foreground, hover.containsMouse ? 0.07 : 0)
                border.width: root.cursor === index ? 1 : 0
                border.color: Qt.alpha(root.accentColor, 0.4)
                Text {
                    id: kindLabel
                    anchors.left: parent.left
                    anchors.leftMargin: Style.space(10)
                    anchors.verticalCenter: parent.verticalCenter
                    width: Style.space(24)
                    text: fileRow.modelData.type === "dir" ? "▸" : "·"
                    color: fileRow.modelData.type === "dir" ? root.accentColor : Qt.alpha(root.foreground, 0.6)
                    font.family: Style.font.family
                    font.pixelSize: Style.font.body
                }
                Text {
                    anchors.left: kindLabel.right
                    anchors.right: sizeText.left
                    anchors.rightMargin: Style.space(8)
                    anchors.verticalCenter: parent.verticalCenter
                    text: fileRow.modelData.name
                    textFormat: Text.PlainText
                    elide: Text.ElideMiddle
                    color: root.foreground
                    font.family: Style.font.family
                    font.pixelSize: Style.font.body
                }
                Text {
                    id: sizeText
                    anchors.right: parent.right
                    anchors.rightMargin: Style.space(10)
                    anchors.verticalCenter: parent.verticalCenter
                    width: Style.space(72)
                    text: fileRow.modelData.type === "dir" ? "Folder" : root.sizeLabel(fileRow.modelData.size)
                    color: Qt.alpha(root.foreground, 0.65)
                    font.family: Style.font.family
                    font.pixelSize: Style.font.caption
                    horizontalAlignment: Text.AlignRight
                }
                MouseArea {
                    id: hover
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton
                    onClicked: mouse => root.toggle(fileRow.index, !!(mouse.modifiers & Qt.ControlModifier))
                    onDoubleClicked: root.openItem(fileRow.index)
                }
            }
        }
        Text {
            anchors.centerIn: parent
            width: parent.width - Style.space(40)
            visible: root.confirmKind === "" && root.nameMode === "" && root.entries.length === 0
            text: !root.available ? "File session unavailable.\nOpen Files on a connected device." : root.busy ? "Reading the Flipper…" : root.files.error ? "Could not read this folder.\nUse Refresh to retry." : "This folder is empty.\nDrop a file here or create a folder."
            color: Qt.alpha(root.foreground, 0.7)
            font.family: Style.font.family
            font.pixelSize: Style.font.body
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
                text: root.confirmKind === "delete" ? "Delete selected items?" : root.confirmKind === "overwrite" ? "Replace existing file?" : root.confirmKind === "upload" ? "Upload to device?" : "Download to computer?"
                color: root.foreground
                font.bold: true
                font.family: Style.font.family
                font.pixelSize: Style.font.body
                wrapMode: Text.Wrap
            }
            Text {
                width: parent.width
                wrapMode: Text.WrapAnywhere
                textFormat: Text.PlainText
                color: Qt.alpha(root.foreground, 0.8)
                font.family: Style.font.family
                font.pixelSize: Style.font.caption
                text: {
                    if (root.confirmKind === "delete") return root.confirmHosts.length + " item(s) will be removed permanently. Folders include all their contents."
                    if (root.confirmKind === "download") return root.confirmHosts.length + " file(s) → Downloads/OmaFlip"
                    if (root.confirmKind === "upload") return root.confirmHosts.length + " file(s) → " + (root.files.path || "/ext")
                    return root.files.error || "The existing file will be overwritten."
                }
            }
            Flow {
                width: parent.width
                spacing: Style.space(8)
                ToolButton {
                    text: root.confirmKind === "delete" ? "Delete" : root.confirmKind === "overwrite" ? "Replace" : "Confirm"
                    foreground: root.foreground
                    destructive: root.confirmKind === "delete" || root.confirmKind === "overwrite"
                    primary: !destructive
                    enabled: root.available && !root.busy
                    onTriggered: root.runConfirm()
                }
                ToolButton {
                    text: "Cancel"
                    foreground: root.foreground
                    onTriggered: { root.confirmKind = ""; root.confirmHosts = [] }
                }
            }
        }
        Column {
            anchors.centerIn: parent
            width: parent.width - Style.space(32)
            spacing: Style.space(10)
            visible: root.nameMode !== "" && root.confirmKind === ""
            Text {
                text: root.nameMode === "mkdir" ? "Create a folder" : "Rename item"
                color: root.foreground
                font.bold: true
                font.family: Style.font.family
                font.pixelSize: Style.font.body
            }
            InputField {
                id: nameInput
                width: parent.width
                foreground: root.foreground
                placeholderText: root.nameMode === "mkdir" ? "Folder name" : "New name"
                enabled: root.available && !root.busy
                onAccepted: root.submitName()
            }
            Text {
                width: parent.width
                text: "Use a single name, without /, . or .."
                color: Qt.alpha(root.foreground, 0.65)
                font.family: Style.font.family
                font.pixelSize: Style.font.caption
            }
            Flow {
                width: parent.width
                spacing: Style.space(8)
                ToolButton {
                    text: root.nameMode === "mkdir" ? "Create folder" : "Save name"
                    foreground: root.foreground
                    primary: true
                    enabled: root.available && !root.busy && root.validName
                    onTriggered: root.submitName()
                }
                ToolButton {
                    text: "Cancel"
                    foreground: root.foreground
                    onTriggered: root.nameMode = ""
                }
            }
        }
    }
    Text {
        width: parent.width
        visible: root.confirmKind === "" && root.nameMode === ""
        text: drop.containsDrag ? root.dropHint : root.selectionCount + " selected · Double-click to open · Ctrl-click to select more"
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        color: Qt.alpha(root.foreground, 0.65)
        font.family: Style.font.family
        font.pixelSize: Style.font.caption
    }
    Flow {
        width: parent.width
        spacing: Style.space(6)
        visible: root.confirmKind === "" && root.nameMode === ""
        Repeater {
            model: [
                {label: "Up", op: "up"}, {label: "Open", op: "open"},
                {label: "Download", op: "download"}, {label: "Rename", op: "rename"},
                {label: "New folder", op: "mkdir"}, {label: "Refresh", op: "refresh"},
                {label: "Delete", op: "delete"}
            ]
            ToolButton {
                required property var modelData
                text: modelData.label
                foreground: root.foreground
                primary: modelData.op === "open"
                destructive: modelData.op === "delete"
                enabled: root.actionable && (modelData.op === "up" ? !!root.files.parent : modelData.op === "open" ? !!root.entries[root.cursor] : modelData.op === "download" ? root.hasDownload : modelData.op === "rename" ? root.selectionCount === 1 : modelData.op === "delete" ? root.selectionCount > 0 : true)
                onTriggered: {
                    if (modelData.op === "up") root.goParent()
                    else if (modelData.op === "open") root.openItem(root.cursor)
                    else if (modelData.op === "download") root.requestDownload()
                    else if (modelData.op === "delete") root.requestDelete()
                    else if (modelData.op === "rename") root.requestRename()
                    else if (modelData.op === "mkdir") root.requestMkdir()
                    else if (modelData.op === "refresh") root.service.filesList(root.files.path || "/ext")
                }
            }
        }
    }
    Rectangle {
        width: parent.width
        height: previewLabel.implicitHeight + Style.space(24)
        visible: !!(root.files.preview && root.files.preview.path)
        radius: Style.cornerRadius
        color: Qt.alpha(root.foreground, 0.04)
        Text {
            id: previewLabel
            anchors.fill: parent
            anchors.margins: Style.space(12)
            text: {
                const preview = root.files.preview || {}
                return "Preview · " + (preview.path || "") + (preview.kind === "text" ? "" : " · " + (preview.kind || "file")) + "\n" + (preview.text || "")
            }
            textFormat: Text.PlainText
            wrapMode: Text.WrapAnywhere
            maximumLineCount: 12
            elide: Text.ElideRight
            color: Qt.alpha(root.foreground, 0.8)
            font.family: Style.font.family
            font.pixelSize: Style.font.caption
        }
    }
}
