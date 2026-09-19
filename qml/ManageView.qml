import QtQuick
import qs.Commons

Column {
    id: root
    property var service: null
    property color foreground: Color.foreground
    spacing: Style.space(8)

    readonly property var manage: {
        const device = service && service.selectedDevice
        return device && device.manage && typeof device.manage === "object" ? device.manage : ({open: false})
    }
    readonly property var firmware: service && service.firmware ? service.firmware : ({})
    readonly property var packsState: service && service.packs ? service.packs : ({})
    readonly property var info: service && service.selectedDevice ? service.selectedDevice.info : ({})
    readonly property string origin: (service && service.selectedDevice && service.selectedDevice.origin) || "Unknown"
    readonly property string primaryProvider: origin === "Momentum" ? "momentum" : "official"
    readonly property string primaryLabel: primaryProvider === "momentum" ? "Momentum" : "official"
    readonly property var backups: manage.backups || []
    readonly property var installed: manage.packs || []
    readonly property var catalog: packsState.catalog || []
    property string section: "backups"
    property int cursor: 0
    property int packCursor: 0
    property int catalogCursor: 0
    property string confirmKind: ""
    property string seenError: ""

    function current() { return backups[cursor] || null }
    function currentPack() { return installed[packCursor] || null }
    function currentCatalog() { return catalog[catalogCursor] || null }
    function requestRestore() {
        const item = current()
        if (!item || !item.archive) return
        if (item.compat === "target_mismatch") return
        confirmKind = item.compat === "origin_mismatch" ? "origin" : (item.compat === "version_mismatch" ? "version" : "restore")
    }
    function requestApply() {
        if (!firmware.path) return
        const provider = firmware.provider === "momentum" ? "momentum" : "official"
        const expected = provider === "momentum" ? "Momentum" : "Official"
        const fork = info.firmware_origin_fork || ""
        if (provider === "momentum")
            confirmKind = fork && fork !== expected ? "replaceMomentum" : "applyMomentum"
        else
            confirmKind = fork && fork !== expected ? "replace" : "apply"
    }
    function requestPackInstall() {
        if (!packsState.path) return
        confirmKind = "packInstall"
    }
    function requestPackRemove() {
        const item = currentPack()
        if (!item || !item.name) return
        confirmKind = "packRemove"
    }
    function runConfirm() {
        if (!service) return
        if (confirmKind === "restore" || confirmKind === "origin" || confirmKind === "version") {
            const item = current()
            if (item) service.backupRestore(item.archive, confirmKind === "origin", confirmKind === "version")
        } else if (confirmKind === "apply" || confirmKind === "replace" || confirmKind === "applyMomentum" || confirmKind === "replaceMomentum") {
            service.firmwareApply(confirmKind === "replace" || confirmKind === "replaceMomentum")
        } else if (confirmKind === "packInstall") {
            service.packsInstall()
        } else if (confirmKind === "packRemove") {
            const item = currentPack()
            if (item) service.packsRemove(item.name)
        }
        confirmKind = ""
    }

    Connections {
        target: root.service
        function onDevicesChanged() {
            const code = root.manage.errorCode
            if ((code === "origin_mismatch" || code === "version_mismatch") && code !== root.seenError && !root.confirmKind) {
                root.seenError = code
                root.confirmKind = code === "origin_mismatch" ? "origin" : "version"
            }
        }
    }

    Text {
        width: parent.width
        text: "Backup, firmware, packs"
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
        text: (info.firmware_version || "Unknown version") + " · " + (info.firmware_origin_fork || "Unknown origin")
    }
    Text {
        width: parent.width
        visible: !!manage.error
        wrapMode: Text.Wrap
        textFormat: Text.PlainText
        color: root.foreground
        font.family: Style.font.family
        font.pixelSize: Style.font.caption
        text: manage.error || ""
    }

    Row {
        spacing: Style.space(6)
        Repeater {
            model: [
                {label: "Backups", id: "backups"},
                {label: "Packs", id: "packs"}
            ]
            Rectangle {
                required property var modelData
                width: implicitWidth
                implicitWidth: tab.implicitWidth + Style.space(16)
                height: Style.space(26)
                radius: Style.cornerRadius
                color: Qt.alpha(root.foreground, root.section === modelData.id ? 0.12 : 0.04)
                border.width: 1
                border.color: Qt.alpha(root.foreground, root.section === modelData.id ? 0.45 : 0.18)
                Text {
                    id: tab
                    anchors.centerIn: parent
                    text: modelData.label
                    color: root.foreground
                    font.family: Style.font.family
                    font.pixelSize: Style.font.caption
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.section = modelData.id
                }
            }
        }
    }

    Rectangle {
        width: parent.width
        height: 140
        radius: Style.cornerRadius
        color: Qt.alpha(root.foreground, 0.04)
        border.width: 1
        border.color: Qt.alpha(root.foreground, confirmKind ? 0.45 : 0.18)
        ListView {
            anchors.fill: parent
            anchors.margins: 4
            clip: true
            visible: confirmKind === "" && root.section === "backups"
            model: root.backups
            delegate: Rectangle {
                required property var modelData
                required property int index
                width: ListView.view ? ListView.view.width : 200
                height: Style.space(26)
                radius: Style.cornerRadius
                color: Qt.alpha(root.foreground, root.cursor === index ? 0.12 : 0)
                Text {
                    anchors.fill: parent
                    anchors.leftMargin: Style.space(8)
                    verticalAlignment: Text.AlignVCenter
                    text: (modelData.name || modelData.archive || "backup") + (modelData.firmware_version ? " · " + modelData.firmware_version : "") + (modelData.compat && modelData.compat !== "ok" ? " · " + modelData.compat : "")
                    textFormat: Text.PlainText
                    elide: Text.ElideMiddle
                    color: root.foreground
                    font.family: Style.font.family
                    font.pixelSize: Style.font.caption
                }
                MouseArea { anchors.fill: parent; onClicked: root.cursor = index }
            }
        }
        ListView {
            anchors.fill: parent
            anchors.margins: 4
            clip: true
            visible: confirmKind === "" && root.section === "packs"
            model: root.installed
            delegate: Rectangle {
                required property var modelData
                required property int index
                width: ListView.view ? ListView.view.width : 200
                height: Style.space(26)
                radius: Style.cornerRadius
                color: Qt.alpha(root.foreground, root.packCursor === index ? 0.12 : 0)
                Text {
                    anchors.fill: parent
                    anchors.leftMargin: Style.space(8)
                    verticalAlignment: Text.AlignVCenter
                    text: modelData.name || "pack"
                    textFormat: Text.PlainText
                    elide: Text.ElideMiddle
                    color: root.foreground
                    font.family: Style.font.family
                    font.pixelSize: Style.font.caption
                }
                MouseArea { anchors.fill: parent; onClicked: root.packCursor = index }
            }
        }
        Text {
            anchors.centerIn: parent
            visible: confirmKind === "" && root.section === "backups" && backups.length === 0
            text: "No backups yet. Create one before changing firmware."
            color: Qt.alpha(root.foreground, 0.55)
            font.family: Style.font.family
            font.pixelSize: Style.font.caption
        }
        Text {
            anchors.centerIn: parent
            visible: confirmKind === "" && root.section === "packs" && installed.length === 0
            text: origin === "Momentum" ? "No packs on /ext/asset_packs yet." : "Asset packs need Momentum firmware."
            color: Qt.alpha(root.foreground, 0.55)
            font.family: Style.font.family
            font.pixelSize: Style.font.caption
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
                text: {
                    if (confirmKind === "restore") return "Restore internal storage from this backup?"
                    if (confirmKind === "version") return "This backup is from another firmware version. Restore anyway?"
                    if (confirmKind === "origin") return "This backup is from another firmware origin. Restore anyway?"
                    if (confirmKind === "replace") return "Install official firmware and replace " + (info.firmware_origin_fork || "the current firmware") + "? Create a backup first."
                    if (confirmKind === "replaceMomentum") return "Install Momentum firmware and replace " + (info.firmware_origin_fork || "the current firmware") + "? Create a backup first."
                    if (confirmKind === "applyMomentum") return "Install the verified Momentum update? The Flipper will reboot into the updater."
                    if (confirmKind === "packInstall") return "Extract this pack into /ext/asset_packs? Select it later in Momentum Settings on the Flipper."
                    if (confirmKind === "packRemove") return "Delete " + ((currentPack() && currentPack().name) || "this pack") + " from the SD card?"
                    return "Install the verified official update package? The Flipper will reboot into the updater."
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
        visible: confirmKind === "" && root.section === "backups"
        Repeater {
            model: [
                {label: "Create backup", op: "create"},
                {label: "Restore", op: "restore"},
                {label: "Check " + root.primaryLabel, op: "checkPrimary"},
                {label: firmware.path && firmware.provider === root.primaryProvider ? ("Apply " + root.primaryLabel) : ("Download " + root.primaryLabel), op: firmware.path && firmware.provider === root.primaryProvider ? "apply" : "download"},
                {label: origin === "Momentum" ? "Check official" : "Check Momentum", op: "checkOther"}
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
                        if (modelData.op === "create") root.service.backupCreate()
                        else if (modelData.op === "restore") root.requestRestore()
                        else if (modelData.op === "checkPrimary") root.service.firmwareCheck(root.primaryProvider)
                        else if (modelData.op === "checkOther") root.service.firmwareCheck(root.origin === "Momentum" ? "official" : "momentum")
                        else if (modelData.op === "download") root.service.firmwareDownload()
                        else root.requestApply()
                    }
                }
            }
        }
    }

    Flow {
        width: parent.width
        spacing: Style.space(6)
        visible: confirmKind === "" && root.section === "packs"
        Repeater {
            model: [
                {label: "Check packs", op: "check"},
                {label: packsState.path ? "Install pack" : "Download pack", op: packsState.path ? "install" : "download"},
                {label: "Remove pack", op: "remove"}
            ]
            Rectangle {
                required property var modelData
                width: implicitWidth
                implicitWidth: pbtn.implicitWidth + Style.space(16)
                height: Style.space(28)
                radius: Style.cornerRadius
                color: Qt.alpha(root.foreground, pmouse.containsMouse ? 0.12 : 0.05)
                border.width: 1
                border.color: Qt.alpha(root.foreground, 0.25)
                Text {
                    id: pbtn
                    anchors.centerIn: parent
                    text: modelData.label
                    color: root.foreground
                    font.family: Style.font.family
                    font.pixelSize: Style.font.body
                }
                MouseArea {
                    id: pmouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (!root.service) return
                        if (modelData.op === "check") root.service.packsCheck()
                        else if (modelData.op === "download") {
                            const item = root.currentCatalog()
                            if (item && item.id) root.service.packsDownload(item.id)
                        } else if (modelData.op === "install") root.requestPackInstall()
                        else root.requestPackRemove()
                    }
                }
            }
        }
    }

    Rectangle {
        width: parent.width
        height: 120
        radius: Style.cornerRadius
        visible: root.section === "packs" && confirmKind === "" && catalog.length > 0
        color: Qt.alpha(root.foreground, 0.04)
        border.width: 1
        border.color: Qt.alpha(root.foreground, 0.18)
        ListView {
            anchors.fill: parent
            anchors.margins: 4
            clip: true
            model: root.catalog
            delegate: Rectangle {
                required property var modelData
                required property int index
                width: ListView.view ? ListView.view.width : 200
                height: Style.space(26)
                radius: Style.cornerRadius
                color: Qt.alpha(root.foreground, root.catalogCursor === index ? 0.12 : 0)
                Text {
                    anchors.fill: parent
                    anchors.leftMargin: Style.space(8)
                    verticalAlignment: Text.AlignVCenter
                    text: (modelData.name || modelData.id || "pack") + (modelData.author ? " · " + modelData.author : "")
                    textFormat: Text.PlainText
                    elide: Text.ElideMiddle
                    color: root.foreground
                    font.family: Style.font.family
                    font.pixelSize: Style.font.caption
                }
                MouseArea { anchors.fill: parent; onClicked: root.catalogCursor = index }
            }
        }
    }

    Text {
        width: parent.width
        visible: root.section === "packs"
        wrapMode: Text.Wrap
        textFormat: Text.PlainText
        color: Qt.alpha(root.foreground, 0.7)
        font.family: Style.font.family
        font.pixelSize: Style.font.caption
        text: {
            if (packsState.downloading) return "Downloading pack…"
            if (packsState.path) return "Verified " + (packsState.name || packsState.id || "pack") + ". Install writes /ext/asset_packs; activate it in Momentum Settings on the Flipper."
            return "Install copies packs into /ext/asset_packs. OmaFlip does not write Momentum settings."
        }
    }

    Text {
        width: parent.width
        visible: !!(firmware.version || firmware.path || firmware.downloading)
        wrapMode: Text.WrapAnywhere
        textFormat: Text.PlainText
        color: Qt.alpha(root.foreground, 0.7)
        font.family: Style.font.family
        font.pixelSize: Style.font.caption
        text: {
            const label = firmware.provider === "momentum" ? "Momentum" : "official"
            if (firmware.downloading) return "Downloading " + label + " " + (firmware.version || "") + "…"
            if (firmware.path) return "Verified " + label + " " + firmware.version + " · " + firmware.path
            if (firmware.version) return label + " " + firmware.channel + " " + firmware.version + " for " + (firmware.target || "f7")
            return ""
        }
    }
}
