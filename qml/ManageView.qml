pragma ComponentBehavior: Bound
import QtQuick
import qs.Commons

Column {
    id: root
    property var service: null
    property color foreground: Color.foreground
    property color accentColor: Color.accent
    spacing: Style.space(12)

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

    readonly property bool busy: !!manage.busy || !!firmware.downloading || !!packsState.downloading
    readonly property bool canManage: !!service && !!manage.ready && !busy
    property string pendingArchive: ""
    property string pendingPack: ""
    property string restoreNote: ""
    function current() { return backups[cursor] || null }
    function currentPack() { return installed[packCursor] || null }
    function currentCatalog() { return catalog[catalogCursor] || null }
    function requestRestore() {
        const item = current()
        if (!canManage || !item || !item.archive) return
        if (item.compat === "target_mismatch") {
            restoreNote = "This backup targets a different hardware revision and cannot be restored here."
            return
        }
        restoreNote = ""
        pendingArchive = item.archive
        confirmKind = item.compat === "origin_mismatch" ? "origin" : (item.compat === "version_mismatch" ? "version" : "restore")
    }
    function requestApply() {
        if (!canManage || !firmware.path) return
        const provider = firmware.provider === "momentum" ? "momentum" : "official"
        const expected = provider === "momentum" ? "Momentum" : "Official"
        const fork = info.firmware_origin_fork || ""
        if (provider === "momentum")
            confirmKind = fork && fork !== expected ? "replaceMomentum" : "applyMomentum"
        else
            confirmKind = fork && fork !== expected ? "replace" : "apply"
    }
    function requestPackInstall() {
        if (!canManage || !packsState.path || origin !== "Momentum") return
        confirmKind = "packInstall"
    }
    function requestPackRemove() {
        const item = currentPack()
        if (!canManage || !item || !item.name || origin !== "Momentum") return
        pendingPack = item.name
        confirmKind = "packRemove"
    }
    function runConfirm() {
        if (!canManage) return
        if (confirmKind === "restore" || confirmKind === "origin" || confirmKind === "version") {
            const item = current()
            if (pendingArchive || item) service.backupRestore(pendingArchive || item.archive, confirmKind === "origin", confirmKind === "version")
        } else if (confirmKind === "apply" || confirmKind === "replace" || confirmKind === "applyMomentum" || confirmKind === "replaceMomentum") {
            service.firmwareApply(confirmKind === "replace" || confirmKind === "replaceMomentum")
        } else if (confirmKind === "packInstall") {
            service.packsInstall()
        } else if (confirmKind === "packRemove") {
            const item = currentPack()
            if (pendingPack || item) service.packsRemove(pendingPack || item.name)
        }
        confirmKind = ""
        pendingArchive = ""
        pendingPack = ""
    }

    Connections {
        target: root.service
        function onDevicesChanged() {
            const code = root.manage.errorCode
            if (!code) root.seenError = ""
            if ((code === "origin_mismatch" || code === "version_mismatch") && code !== root.seenError && !root.confirmKind) {
                root.seenError = code
                root.confirmKind = code === "origin_mismatch" ? "origin" : "version"
            }
        }
    }

    onServiceChanged: if (!service) { confirmKind = ""; seenError = ""; restoreNote = "" }
    onConfirmKindChanged: if (!confirmKind) {
        pendingArchive = ""
        pendingPack = ""
    }
    onBackupsChanged: cursor = Math.max(0, Math.min(cursor, backups.length - 1))
    onInstalledChanged: packCursor = Math.max(0, Math.min(packCursor, installed.length - 1))
    onCatalogChanged: catalogCursor = Math.max(0, Math.min(catalogCursor, catalog.length - 1))

    // Shared local typography and list treatment, using the host theme.
    component Caption: Text {
        width: parent.width
        wrapMode: Text.WrapAnywhere
        textFormat: Text.PlainText
        color: Qt.alpha(root.foreground, 0.7)
        font.family: Style.font.family
        font.pixelSize: Style.font.caption
    }
    component Heading: Caption {
        color: root.foreground
        font.pixelSize: Style.font.body
        font.bold: true
    }
    component Records: Rectangle {
        id: records
        property var entries: []
        property int selected: 0
        property string emptyText: ""
        signal picked(int row)
        width: parent.width
        height: Style.space(144)
        radius: Style.cornerRadius
        color: Qt.alpha(root.foreground, 0.04)
        border.width: 1
        border.color: Qt.alpha(root.foreground, 0.16)
        ListView {
            id: list
            anchors.fill: parent
            anchors.margins: Style.space(4)
            clip: true
            model: records.entries
            currentIndex: records.selected
            onCurrentIndexChanged: if (currentIndex >= 0) positionViewAtIndex(currentIndex, ListView.Contain)
            delegate: Rectangle {
                id: entryRow
                required property var modelData
                required property int index
                width: ListView.view ? ListView.view.width : 0
                height: entry.implicitHeight + Style.space(16)
                radius: Style.cornerRadius
                color: Qt.alpha(root.accentColor, records.selected === index ? 0.14 : 0)
                border.width: records.selected === index ? 1 : 0
                border.color: Qt.alpha(root.accentColor, 0.4)
                Caption {
                    id: entry
                    x: Style.space(10)
                    y: Style.space(8)
                    width: Math.max(0, parent.width - Style.space(20))
                    color: root.foreground
                    text: (entryRow.modelData.name || entryRow.modelData.archive || entryRow.modelData.id || "Item")
                        + (entryRow.modelData.firmware_version ? " · " + entryRow.modelData.firmware_version : "")
                        + (entryRow.modelData.author ? " · " + entryRow.modelData.author : "")
                        + (entryRow.modelData.compat && entryRow.modelData.compat !== "ok" ? "\n" + entryRow.modelData.compat.replace(/_/g, " ") : "")
                }
                MouseArea {
                    anchors.fill: parent
                    enabled: !root.confirmKind
                    cursorShape: Qt.PointingHandCursor
                    onClicked: records.picked(entryRow.index)
                }
            }
        }
        Caption {
            anchors.centerIn: parent
            width: Math.max(0, parent.width - Style.space(24))
            horizontalAlignment: Text.AlignHCenter
            visible: records.entries.length === 0
            text: records.emptyText
        }
    }

    Caption {
        text: (root.info.firmware_version || "Unknown version") + " · " + (root.info.firmware_origin_fork || "Unknown origin")
    }
    Flow {
        width: parent.width
        spacing: Style.space(6)
        Repeater {
            model: [
                {label: "Backups", id: "backups"},
                {label: "Firmware", id: "firmware"},
                {label: "Packs", id: "packs"}
            ]
            ToolButton {
                required property var modelData
                objectName: "manage-tab-" + modelData.id
                width: Math.min(implicitWidth, root.width)
                text: modelData.label
                foreground: root.foreground
                accentColor: root.accentColor
                primary: root.section === modelData.id
                enabled: !root.confirmKind
                onTriggered: root.section = modelData.id
            }
        }
    }
    Rectangle {
        width: parent.width
        height: feedback.implicitHeight + Style.space(20)
        visible: !!root.manage.error || !!root.restoreNote || root.busy || (!!root.service && !root.manage.ready)
        radius: Style.cornerRadius
        color: Qt.alpha(root.manage.error || root.restoreNote ? Color.urgent : root.accentColor, 0.08)
        border.width: 1
        border.color: Qt.alpha(root.manage.error || root.restoreNote ? Color.urgent : root.accentColor, 0.3)
        Caption {
            id: feedback
            x: Style.space(10)
            y: Style.space(10)
            width: Math.max(0, parent.width - Style.space(20))
            color: root.foreground
            text: root.manage.error || root.restoreNote || (root.firmware.downloading ? "Downloading firmware…" :
                (root.packsState.downloading ? "Downloading asset pack…" : (root.manage.busy ? "Working on the device. Please wait…" : "Opening management session…")))
        }
    }

    Rectangle {
        width: parent.width
        height: confirmation.implicitHeight + Style.space(24)
        visible: root.confirmKind !== ""
        radius: Style.cornerRadius
        color: Qt.alpha(Color.urgent, 0.06)
        border.width: 1
        border.color: Qt.alpha(Color.urgent, 0.35)
        Column {
            id: confirmation
            x: Style.space(12)
            y: Style.space(12)
            width: Math.max(0, parent.width - Style.space(24))
            spacing: Style.space(10)
            Heading { text: "Confirm device change" }
            Caption {
                color: root.foreground
                text: {
                    if (root.confirmKind === "restore") return "Restore internal storage from this backup?"
                    if (root.confirmKind === "version") return "This backup is from another firmware version. Restore anyway?"
                    if (root.confirmKind === "origin") return "This backup is from another firmware origin. Restore anyway?"
                    if (root.confirmKind === "replace") return "Install official firmware and replace " + (root.info.firmware_origin_fork || "the current firmware") + "? Create a backup first."
                    if (root.confirmKind === "replaceMomentum") return "Install Momentum firmware and replace " + (root.info.firmware_origin_fork || "the current firmware") + "? Create a backup first."
                    if (root.confirmKind === "applyMomentum") return "Install the verified Momentum update? The Flipper will reboot into the updater."
                    if (root.confirmKind === "packInstall") return "Extract this pack into /ext/asset_packs? Select it later in Momentum Settings on the Flipper."
                    if (root.confirmKind === "packRemove") return "Delete " + (root.pendingPack || ((root.currentPack() && root.currentPack().name) || "this pack")) + " from the SD card?"
                    return "Install the verified official update package? The Flipper will reboot into the updater."
                }
            }
            Flow {
                width: parent.width
                spacing: Style.space(8)
                ToolButton {
                    objectName: "manage-confirm"
                    width: Math.min(implicitWidth, parent.width)
                    text: "Confirm"
                    foreground: root.foreground
                    accentColor: root.accentColor
                    destructive: true
                    enabled: root.canManage
                    onTriggered: root.runConfirm()
                }
                ToolButton {
                    width: Math.min(implicitWidth, parent.width)
                    text: "Cancel"
                    foreground: root.foreground
                    accentColor: root.accentColor
                    onTriggered: root.confirmKind = ""
                }
            }
        }
    }

    Column {
        width: parent.width
        spacing: Style.space(10)
        visible: root.section === "backups" && !root.confirmKind
        Heading { text: "Internal storage backups" }
        Caption { text: "Create a backup before changing firmware. Select an archive to restore; compatibility is checked before writing." }
        Records {
            objectName: "backupList"
            entries: root.backups
            selected: root.cursor
            emptyText: "No backups yet. Create one before changing firmware."
            onPicked: row => { root.cursor = row; root.restoreNote = "" }
        }
        Flow {
            width: parent.width
            spacing: Style.space(6)
            ToolButton {
                width: Math.min(implicitWidth, parent.width)
                text: "Create backup"
                foreground: root.foreground
                accentColor: root.accentColor
                primary: true
                enabled: root.canManage
                onTriggered: root.service.backupCreate()
            }
            ToolButton {
                width: Math.min(implicitWidth, parent.width)
                text: "Restore selected"
                foreground: root.foreground
                accentColor: root.accentColor
                enabled: root.canManage && !!root.current()
                onTriggered: root.requestRestore()
            }
            ToolButton {
                width: Math.min(implicitWidth, parent.width)
                text: "Refresh backups"
                foreground: root.foreground
                accentColor: root.accentColor
                enabled: root.canManage
                onTriggered: root.service.backupRefresh()
            }
        }
    }
    Column {
        width: parent.width
        spacing: Style.space(10)
        visible: root.section === "firmware" && !root.confirmKind
        Heading { text: "Firmware updates" }
        Caption { text: "1. Check a provider · 2. Download and verify · 3. Apply\nBack up internal storage first. Applying an update reboots the device." }
        Flow {
            width: parent.width
            spacing: Style.space(6)
            Repeater {
                model: [{label: "Check " + root.primaryLabel, provider: root.primaryProvider},
                    {label: root.origin === "Momentum" ? "Check official" : "Check Momentum", provider: root.origin === "Momentum" ? "official" : "momentum"}]
                ToolButton {
                    required property var modelData
                    width: Math.min(implicitWidth, root.width)
                    text: modelData.label
                    foreground: root.foreground
                    accentColor: root.accentColor
                    enabled: !!root.service && !root.busy
                    onTriggered: root.service.firmwareCheck(modelData.provider)
                }
            }
        }
        Caption {
            text: {
                const fw = root.firmware
                const label = fw.provider === "momentum" ? "Momentum" : "Official"
                if (fw.downloading) return "Downloading " + label + " " + (fw.version || "") + "…"
                if (fw.path) return "Verified " + label + " " + (fw.version || "") + "\n" + fw.path
                if (fw.version) return label + " " + (fw.channel || "release") + " " + fw.version + " for " + (fw.target || "f7")
                return "No update checked yet. Choose a provider above."
            }
        }
        ToolButton {
            width: Math.min(implicitWidth, parent.width)
            text: root.firmware.path ? "Apply verified update" : "Download update"
            foreground: root.foreground
            accentColor: root.accentColor
            primary: true
            enabled: root.firmware.path ? root.canManage : (!!root.service && !root.busy && !!root.firmware.url)
            onTriggered: root.firmware.path ? root.requestApply() : root.service.firmwareDownload()
        }
    }
    Column {
        width: parent.width
        spacing: Style.space(10)
        visible: root.section === "packs" && !root.confirmKind
        Heading { text: "Installed asset packs" }
        Caption { text: root.origin === "Momentum" ? "Manage packs on the SD card. Activate installed packs in Momentum Settings on the Flipper." : "Installing asset packs requires Momentum firmware. You can still browse and download the catalog." }
        Records {
            objectName: "installedPackList"
            entries: root.installed
            selected: root.packCursor
            emptyText: root.origin === "Momentum" ? "No packs on /ext/asset_packs yet." : "Asset packs need Momentum firmware."
            onPicked: row => root.packCursor = row
        }
        ToolButton {
            width: Math.min(implicitWidth, parent.width)
            text: "Remove selected pack"
            foreground: root.foreground
            accentColor: root.accentColor
            destructive: true
            enabled: root.canManage && root.origin === "Momentum" && !!root.currentPack()
            onTriggered: root.requestPackRemove()
        }
        Heading { text: "Pack catalog" }
        Records {
            objectName: "packCatalogList"
            entries: root.catalog
            selected: root.catalogCursor
            emptyText: "No catalog loaded. Check packs to browse available downloads."
            onPicked: row => root.catalogCursor = row
        }
        Flow {
            width: parent.width
            spacing: Style.space(6)
            ToolButton {
                width: Math.min(implicitWidth, parent.width)
                text: "Check packs"
                foreground: root.foreground
                accentColor: root.accentColor
                enabled: !!root.service && !root.busy
                onTriggered: root.service.packsCheck()
            }
            ToolButton {
                width: Math.min(implicitWidth, parent.width)
                text: "Download selected"
                foreground: root.foreground
                accentColor: root.accentColor
                enabled: !!root.service && !root.busy && !!root.currentCatalog()
                onTriggered: {
                    const item = root.currentCatalog()
                    if (item && item.id) root.service.packsDownload(item.id)
                }
            }
            ToolButton {
                width: Math.min(implicitWidth, parent.width)
                text: "Install verified pack"
                foreground: root.foreground
                accentColor: root.accentColor
                primary: true
                enabled: root.canManage && root.origin === "Momentum" && !!root.packsState.path
                onTriggered: root.requestPackInstall()
            }
        }
        Caption {
            text: root.packsState.downloading ? "Downloading pack…" : (root.packsState.path ? "Verified " + (root.packsState.name || root.packsState.id || "pack") + ". Ready to install into /ext/asset_packs." : "Downloads are verified before installation. OmaFlip does not write Momentum settings.")
        }
    }
}
