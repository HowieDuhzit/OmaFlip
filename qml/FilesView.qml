import QtQuick
import qs.Commons

Column {
    id: root
    property var service: null
    property color foreground: Color.foreground
    spacing: Style.space(8)

    readonly property var files: {
        const device = service && service.selectedDevice
        return device && device.files && typeof device.files === "object" ? device.files : ({open: false})
    }
    readonly property var entries: files.entries || []
    readonly property bool busy: !!files.busy
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
    function toggle(index, multi) {
        if (!entries[index]) return
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
        if (!item || !service) return
        cursor = index
        const path = root.join(files.path, item.name)
        if (item.type === "dir") {
            selected = {}
            service.filesList(path)
        } else service.filesPreview(path)
    }
    function goParent() {
        if (!service || !files.parent) return
        selected = {}
        service.filesList(files.parent)
    }
    function requestDownload() {
        const items = selectedEntries().filter(item => item.type !== "dir")
        if (!items.length) return
        confirmKind = "download"
        confirmHosts = items.map(item => root.join(files.path, item.name))
    }
    function requestDelete() {
        const items = selectedEntries()
        if (!items.length) return
        confirmKind = "delete"
        confirmHosts = items.map(item => root.join(files.path, item.name))
    }
    function requestRename() {
        const items = selectedEntries()
        if (items.length !== 1) return
        nameMode = "rename"
        nameSeed = items[0].name
        nameInput.text = items[0].name
        nameInput.forceActiveFocus()
    }
    function requestMkdir() {
        nameMode = "mkdir"
        nameSeed = ""
        nameInput.text = ""
        nameInput.forceActiveFocus()
    }
    function submitName() {
        const name = nameInput.text.trim()
        if (!service || !name) { nameMode = ""; return }
        if (nameMode === "mkdir") service.filesMkdir(root.join(files.path, name))
        else if (nameMode === "rename") {
            const items = selectedEntries()
            if (items.length === 1) service.filesRename(root.join(files.path, items[0].name), root.join(files.path, name))
        }
        nameMode = ""
    }
    function runConfirm() {
        if (!service) return
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

    onEntriesChanged: {
        cursor = Math.min(cursor, Math.max(0, entries.length - 1))
        const live = {}
        for (let i = 0; i < entries.length; i++) if (selected[entries[i].name]) live[entries[i].name] = true
        selected = live
    }

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

    Text {
        width: parent.width
        text: files.path || "/ext"
        textFormat: Text.PlainText
        color: root.foreground
        font.family: Style.font.family
        font.pixelSize: Style.font.body
        font.bold: true
        elide: Text.ElideMiddle
    }
    Text {
        width: parent.width
        visible: !!files.error
        text: files.error || ""
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        color: root.foreground
        font.family: Style.font.family
        font.pixelSize: Style.font.caption
    }
    Text {
        width: parent.width
        visible: !!(files.transfer && files.transfer.path)
        text: {
            const t = files.transfer || {}
            if (!t.path) return ""
            const label = t.direction === "upload" ? "Uploading" : "Downloading"
            const done = t.done ? " done" : ""
            return label + " " + t.path + " · " + (t.bytes || 0) + " / " + (t.total || 0) + done
        }
        textFormat: Text.PlainText
        wrapMode: Text.WrapAnywhere
        color: Qt.alpha(root.foreground, 0.7)
        font.family: Style.font.family
        font.pixelSize: Style.font.caption
    }

    Rectangle {
        width: parent.width
        height: 220
        radius: Style.cornerRadius
        color: Qt.alpha(root.foreground, drop.containsDrag ? 0.1 : 0.04)
        border.width: 1
        border.color: Qt.alpha(root.foreground, drop.containsDrag || confirmKind ? 0.45 : 0.18)

        DropArea {
            id: drop
            anchors.fill: parent
            onEntered: root.dropHint = "Drop to upload into " + (root.files.path || "/ext")
            onExited: root.dropHint = ""
            onDropped: drop => root.offerDrop(drop.urls)
        }

        ListView {
            id: list
            anchors.fill: parent
            anchors.margins: 4
            clip: true
            visible: confirmKind === "" && nameMode === ""
            boundsBehavior: Flickable.StopAtBounds
            model: root.entries
            delegate: Rectangle {
                required property var modelData
                required property int index
                width: list.width
                height: Style.space(26)
                radius: Style.cornerRadius
                color: Qt.alpha(root.foreground, root.cursor === index || root.selected[modelData.name] ? 0.12 : (hover.containsMouse ? 0.07 : 0))
                Row {
                    anchors.fill: parent
                    anchors.leftMargin: Style.space(8)
                    spacing: Style.space(8)
                    Text {
                        width: 18
                        text: modelData.type === "dir" ? "▸" : "·"
                        color: Qt.alpha(root.foreground, 0.55)
                        font.family: Style.font.family
                        font.pixelSize: Style.font.body
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        width: parent.width - 90
                        text: modelData.name
                        textFormat: Text.PlainText
                        elide: Text.ElideMiddle
                        color: root.foreground
                        font.family: Style.font.family
                        font.pixelSize: Style.font.body
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        width: 64
                        text: modelData.type === "dir" ? "dir" : String(modelData.size || 0)
                        color: Qt.alpha(root.foreground, 0.55)
                        font.family: Style.font.family
                        font.pixelSize: Style.font.caption
                        horizontalAlignment: Text.AlignRight
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                MouseArea {
                    id: hover
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton
                    onClicked: mouse => root.toggle(index, !!(mouse.modifiers & Qt.ControlModifier))
                    onDoubleClicked: root.openItem(index)
                }
            }
        }
        Text {
            anchors.centerIn: parent
            visible: confirmKind === "" && nameMode === "" && entries.length === 0 && !busy
            text: dropHint || "This folder is empty. Drop a file to upload."
            color: Qt.alpha(root.foreground, 0.55)
            font.family: Style.font.family
            font.pixelSize: Style.font.caption
        }
        Text {
            anchors.centerIn: parent
            visible: busy && entries.length === 0 && confirmKind === ""
            text: "Reading the Flipper…"
            color: Qt.alpha(root.foreground, 0.55)
            font.family: Style.font.family
            font.pixelSize: Style.font.caption
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
                    if (confirmKind === "delete") return "Delete " + confirmHosts.length + " item(s)? Folders are removed with their contents."
                    if (confirmKind === "download") return "Download " + confirmHosts.length + " file(s) into Downloads/OmaFlip?"
                    if (confirmKind === "upload") return "Upload " + confirmHosts.length + " file(s) into " + (files.path || "/ext") + "?"
                    if (confirmKind === "overwrite") return files.error || "Replace the existing file?"
                    return ""
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
                        color: mouse.pressed ? Qt.alpha(root.foreground, 0.2) : Qt.alpha(root.foreground, mouse.containsMouse ? 0.12 : 0.05)
                        border.width: 1
                        border.color: Qt.alpha(root.foreground, 0.25)
                        Text { anchors.centerIn: parent; text: modelData.label; color: root.foreground; font.family: Style.font.family; font.pixelSize: Style.font.body }
                        MouseArea {
                            id: mouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (modelData.op === "ok") root.runConfirm()
                                else { root.confirmKind = ""; root.confirmHosts = [] }
                            }
                        }
                    }
                }
            }
        }
        Column {
            anchors.centerIn: parent
            width: parent.width - Style.space(24)
            spacing: Style.space(8)
            visible: nameMode !== ""
            Text {
                text: nameMode === "mkdir" ? "New folder name" : "Rename to"
                color: Qt.alpha(root.foreground, 0.7)
                font.family: Style.font.family
                font.pixelSize: Style.font.caption
            }
            TextInput {
                id: nameInput
                width: parent.width
                color: root.foreground
                font.family: Style.font.family
                font.pixelSize: Style.font.body
                onAccepted: root.submitName()
            }
            Row {
                spacing: Style.space(8)
                Repeater {
                    model: [{label: "Save", op: "ok"}, {label: "Cancel", op: "no"}]
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
                            onClicked: modelData.op === "ok" ? root.submitName() : (root.nameMode = "")
                        }
                    }
                }
            }
        }
    }

    Flow {
        width: parent.width
        spacing: Style.space(6)
        Repeater {
            model: [
                {label: "Up", op: "up"},
                {label: "Open", op: "open"},
                {label: "Download", op: "download"},
                {label: "Delete", op: "delete"},
                {label: "Rename", op: "rename"},
                {label: "New folder", op: "mkdir"}
            ]
            Rectangle {
                required property var modelData
                width: implicitWidth
                implicitWidth: label.implicitWidth + Style.space(16)
                height: Style.space(28)
                radius: Style.cornerRadius
                color: mouse.pressed ? Qt.alpha(root.foreground, 0.2) : (mouse.containsMouse ? Qt.alpha(root.foreground, 0.12) : Qt.alpha(root.foreground, 0.05))
                border.width: 1
                border.color: Qt.alpha(root.foreground, 0.25)
                Text {
                    id: label
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
                        if (modelData.op === "up") root.goParent()
                        else if (modelData.op === "open") root.openItem(root.cursor)
                        else if (modelData.op === "download") root.requestDownload()
                        else if (modelData.op === "delete") root.requestDelete()
                        else if (modelData.op === "rename") root.requestRename()
                        else if (modelData.op === "mkdir") root.requestMkdir()
                    }
                }
            }
        }
    }

    Text {
        width: parent.width
        visible: files.preview && files.preview.path
        text: {
            const preview = files.preview || {}
            if (!preview.path) return ""
            const title = preview.path + (preview.kind === "text" ? "" : " · " + (preview.kind || "file"))
            const body = preview.text || ""
            return title + "\n" + body
        }
        textFormat: Text.PlainText
        wrapMode: Text.WrapAnywhere
        maximumLineCount: 12
        elide: Text.ElideRight
        color: Qt.alpha(root.foreground, 0.75)
        font.family: Style.font.family
        font.pixelSize: Style.font.caption
    }
}
