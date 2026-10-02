import QtQuick
import Quickshell
import qs.Commons
import qs.Ui as Ui
import "qml" as Oma

Ui.Panel {
    id: root
    moduleName: "io.github.howieduhzit.omaflip"
    ipcTarget: "io.github.howieduhzit.omaflip"
    property var anchorItem: null
    property var hostWidget: null
    readonly property var service: hostWidget ? hostWidget.service : null
    readonly property var device: service ? service.selectedDevice : null
    readonly property var info: device && device.info && typeof device.info === "object" ? device.info : ({})
    readonly property var power: device && device.power && typeof device.power === "object" ? device.power : ({})
    readonly property var rpc: device && device.rpc && typeof device.rpc === "object" ? device.rpc : ({})
    readonly property string rpcText: rpc.summary || ""
    readonly property string storageText: rpc.storage || ""
    readonly property string regionText: info.hardware_region_provisioned || ""
    property bool details: false
    property bool showDiagnostics: false
    property bool settingsOpen: false
    property bool remoteOpen: false
    property bool filesOpen: false
    property bool cliOpen: false
    property bool appsOpen: false
    property bool manageOpen: false
    property bool devOpen: false
    property bool companionOpen: false
    property int cursor: 0
    readonly property bool sessionOpen: remoteOpen || filesOpen || cliOpen || appsOpen || manageOpen || devOpen || companionOpen
    readonly property string status: !service ? "Service unavailable" : (!service.ready ? "Service unavailable" :
        (device ? (device.remote ? "Remote" : (device.files && device.files.open ? "Files" : (device.cli && device.cli.open ? "CLI" : (device.apps && device.apps.open ? "Apps" : (device.manage && device.manage.open ? "Backup" : (device.dev && device.dev.open ? "Dev" : (device.companion && device.companion.open ? "Companion" : (device.identityConfirmed ? device.state : "STM32 DFU · identity unconfirmed")))))))) :
        (service.devices.length ? "Select a device" : "No Flipper Zero detected")))
    readonly property string errorText: {
        if (!service) return "Enable the OmaFlip service alongside the bar widget."
        if (service.serviceError) return service.serviceError
        if (device && device.error && device.error.reason) return device.error.operation + "\n" + device.error.path + "\n" + device.error.reason + "\n" + device.error.suggestion
        return ""
    }
    readonly property var actions: {
        let rows = []
        if (service) for (const item of service.devices) rows.push({label: (item.key === service.selectedKey ? "● " : "○ ") + (item.info.hardware_name || (item.identityConfirmed ? "Flipper " : "STM32 DFU ") + (item.usbSerial || item.key)), op: "select", key: item.key, id: item.id})
        if (device && device.identityConfirmed) {
            rows.push({label: remoteOpen ? "Close remote" : "Remote", op: "remote"})
            rows.push({label: filesOpen ? "Close files" : "Files", op: "files"})
            rows.push({label: cliOpen ? "Close CLI" : "CLI", op: "cli"})
            rows.push({label: appsOpen ? "Close apps" : "Apps", op: "apps"})
            rows.push({label: manageOpen ? "Close backup" : "Backup", op: "manage"})
            rows.push({label: devOpen ? "Close dev" : "Dev", op: "dev"})
            rows.push({label: companionOpen ? "Stop companion" : "Companion", op: "companion"})
            if (!sessionOpen) {
                rows.push({label: "Refresh / Retry", op: "refresh"})
                rows.push({label: details ? "Hide device details" : "Device details", op: "details"})
            } else if (remoteOpen) {
                rows.push({label: "Screenshot", op: "screenshot"})
                rows.push({label: "Copy screenshot", op: "copyShot"})
            }
        } else if (!sessionOpen && service) {
            rows.push({label: "Refresh / Rescan", op: "refresh"})
            if (device) rows.push({label: details ? "Hide device details" : "Device details", op: "details"})
        }
        if (errorText) rows.push({label: "Copy error", op: "copy"})
        rows.push({label: showDiagnostics ? "Hide diagnostics" : "Open diagnostics", op: "diagnostics"})
        return rows
    }
    readonly property var settingsActions: {
        let rows = []
        rows.push({label: "Restart service", op: "restart"})
        rows.push({label: "Auto-connect: " + (hostWidget && hostWidget.setting("autoConnect", true) ? "On" : "Off"), op: "auto"})
        rows.push({label: "Bar label: " + (hostWidget && hostWidget.setting("compact", false) ? "Icon" : "Device name"), op: "compact"})
        rows.push({label: "Notify connect: " + (hostWidget && hostWidget.setting("notifyConnect", true) ? "On" : "Off"), op: "notifyConnect"})
        rows.push({label: "Notify errors: " + (hostWidget && hostWidget.setting("notifyError", true) ? "On" : "Off"), op: "notifyError"})
        if (!service || !service.ready) rows.push({label: "Build / repair backend", op: "build"})
        rows.push({label: "Setup Device Access", op: "setup"})
        rows.push({label: "Open documentation", op: "docs"})
        return rows
    }
    function closeExclusive(except) {
        if (except !== "remote" && remoteOpen) { remoteOpen = false; if (service) service.remoteStop() }
        if (except !== "files" && filesOpen) { filesOpen = false; if (service) service.filesStop() }
        if (except !== "cli" && cliOpen) { cliOpen = false; if (service) service.cliStop() }
        if (except !== "apps" && appsOpen) { appsOpen = false; if (service) service.appsStop() }
        if (except !== "manage" && manageOpen) { manageOpen = false; if (service) service.manageStop() }
        if (except !== "dev" && devOpen) { devOpen = false; if (service) service.devStop() }
        if (except !== "companion" && companionOpen) { companionOpen = false; if (service) service.companionStop() }
    }
    function toggleSession(name) {
        closeExclusive(name)
        if (name === "remote") {
            remoteOpen = !remoteOpen
            if (remoteOpen && service) service.remoteStart()
            else if (service) service.remoteStop()
        } else if (name === "files") {
            filesOpen = !filesOpen
            if (filesOpen && service) service.filesStart()
            else if (service) service.filesStop()
        } else if (name === "cli") {
            cliOpen = !cliOpen
            if (cliOpen && service) service.cliStart()
            else if (service) service.cliStop()
        } else if (name === "apps") {
            appsOpen = !appsOpen
            if (appsOpen && service) service.appsStart()
            else if (service) service.appsStop()
        } else if (name === "manage") {
            manageOpen = !manageOpen
            if (manageOpen && service) service.manageStart()
            else if (service) service.manageStop()
        } else if (name === "dev") {
            devOpen = !devOpen
            if (devOpen && service) service.devStart()
            else if (service) service.devStop()
        } else if (name === "companion") {
            companionOpen = !companionOpen
            if (companionOpen && service) service.companionStart()
            else if (service) service.companionStop()
        }
    }
    function activate(index) {
        const action = actions[index]
        if (!action) return
        if (action.op === "select" && service) {
            service.selectDevice(action.key)
            if (hostWidget) hostWidget.persist("preferredDevice", action.id)
        }
        else if (action.op === "refresh" && service) service.refresh()
        else if (action.op === "remote" || action.op === "files" || action.op === "cli"
            || action.op === "apps" || action.op === "manage" || action.op === "dev" || action.op === "companion") {
            root.toggleSession(action.op)
        }
        else if (action.op === "screenshot" && service) service.screenshot()
        else if (action.op === "copyShot") {
            const shots = root.device && root.device.screenshots
            if (shots && shots.length && shots[0]) {
                Quickshell.execDetached(["bash", "-c", "wl-copy -t image/png < " + Util.shellQuote(String(shots[0]))])
            }
        }
        else if (action.op === "details") details = !details
        else if (action.op === "diagnostics") showDiagnostics = !showDiagnostics
        else if (action.op === "copy") Quickshell.clipboardText = errorText
        else if (action.op === "docs") Qt.openUrlExternally("https://github.com/HowieDuhzit/OmaFlip#readme")
        else if (action.op === "build") Quickshell.execDetached(["omarchy-launch-terminal", "--hold", "bash", decodeURIComponent(Qt.resolvedUrl("scripts/build").toString().replace(/^file:\/\//, ""))])
        else if (action.op === "setup") Quickshell.execDetached(["omarchy-launch-terminal", "bash", decodeURIComponent(Qt.resolvedUrl("scripts/setup-access").toString().replace(/^file:\/\//, ""))])
        cursor = Math.min(cursor, actions.length - 1)
    }
    function activateSettings(index) {
        const action = settingsActions[index]
        if (!action) return
        if (action.op === "restart" && service) service.restart()
        else if (action.op === "auto" && hostWidget) hostWidget.persist("autoConnect", !hostWidget.setting("autoConnect", true))
        else if (action.op === "compact" && hostWidget) hostWidget.persist("compact", !hostWidget.setting("compact", false))
        else if (action.op === "notifyConnect" && hostWidget) hostWidget.persist("notifyConnect", !hostWidget.setting("notifyConnect", true))
        else if (action.op === "notifyError" && hostWidget) hostWidget.persist("notifyError", !hostWidget.setting("notifyError", true))
        else if (action.op === "build") Quickshell.execDetached(["omarchy-launch-terminal", "--hold", "bash", decodeURIComponent(Qt.resolvedUrl("scripts/build").toString().replace(/^file:\/\//, ""))])
        else if (action.op === "setup") Quickshell.execDetached(["omarchy-launch-terminal", "bash", decodeURIComponent(Qt.resolvedUrl("scripts/setup-access").toString().replace(/^file:\/\//, ""))])
        else if (action.op === "docs") Qt.openUrlExternally("https://github.com/HowieDuhzit/OmaFlip#readme")
    }
    function open() {
        cursor = 0
        root.controller.show()
    }
    onOpenedChanged: if (!opened) {
        closeExclusive("")
    }
    Connections {
        target: service
        function onSelectedKeyChanged() {
            if (!root.service) return
            root.closeExclusive("")
        }
    }

    readonly property color accentColor: Color.accent
    readonly property color flipperOrange: Color.accent
    readonly property color statusGreen: Color.accent
    readonly property color cardBg: Qt.alpha(root.barForeground, 0.06)
    readonly property color cardBorder: Qt.alpha(root.barForeground, 0.08)
    readonly property color subtleText: Qt.alpha(root.barForeground, 0.40)
    readonly property int cardR: 6

    Ui.KeyboardPanel {
        id: popup
        anchorItem: root.anchorItem
        owner: root.hostWidget || root
        bar: root.bar
        open: root.opened
        focusTarget: keys
        contentWidth: popup.fittedContentWidth(Style.space(root.sessionOpen ? 520 : (root.details ? 460 : 400)))
        contentHeight: popup.fittedContentHeight(content.implicitHeight)

        Ui.PanelKeyCatcher {
            id: keys
            anchors.fill: parent
            onCloseRequested: {
                if (root.remoteOpen) { root.remoteOpen = false; if (root.service) root.service.remoteStop(); return }
                if (root.filesOpen) {
                    if (fileView && fileView.confirmKind) { fileView.confirmKind = ""; fileView.confirmHosts = []; return }
                    if (fileView && fileView.nameMode) { fileView.nameMode = ""; return }
                    root.filesOpen = false; if (root.service) root.service.filesStop(); return
                }
                if (root.cliOpen) {
                    if (cliView && cliView.confirmText) { cliView.confirmText = ""; return }
                    if (cliView && root.device && root.device.cli && root.device.cli.streaming) {
                        if (root.service) root.service.cliInterrupt(); return
                    }
                    root.cliOpen = false; if (root.service) root.service.cliStop(); return
                }
                if (root.appsOpen) {
                    if (appsView && appsView.confirmKind) { appsView.confirmKind = ""; return }
                    root.appsOpen = false; if (root.service) root.service.appsStop(); return
                }
                if (root.manageOpen) {
                    if (manageView && manageView.confirmKind) { manageView.confirmKind = ""; return }
                    root.manageOpen = false; if (root.service) root.service.manageStop(); return
                }
                if (root.devOpen) {
                    if (devView && devView.confirmKind) { devView.confirmKind = ""; return }
                    root.devOpen = false; if (root.service) root.service.devStop(); return
                }
                if (root.companionOpen) {
                    root.companionOpen = false; if (root.service) root.service.companionStop(); return
                }
                root.close()
            }
            onMoveRequested: (dx, dy) => {
                if (root.remoteOpen && root.service) {
                    if (dy < 0) root.service.input("up")
                    else if (dy > 0) root.service.input("down")
                    else if (dx < 0) root.service.input("left")
                    else if (dx > 0) root.service.input("right")
                    return
                }
                if (root.filesOpen) {
                    const view = fileView
                    if (view) {
                        if (dy < 0 || dx < 0) view.cursor = Math.max(0, view.cursor - 1)
                        else if (dy > 0 || dx > 0) view.cursor = Math.min(Math.max(0, view.entries.length - 1), view.cursor + 1)
                    }
                    return
                }
                if (root.cliOpen && cliView) {
                    if (dy < 0) cliView.historyPrev()
                    else if (dy > 0) cliView.historyNext()
                    return
                }
                if (root.appsOpen && appsView) {
                    if (dy < 0 || dx < 0) appsView.cursor = Math.max(0, appsView.cursor - 1)
                    else if (dy > 0 || dx > 0) appsView.cursor = Math.min(Math.max(0, appsView.apps.length - 1), appsView.cursor + 1)
                    return
                }
                if (root.manageOpen && manageView) {
                    if (manageView.section === "packs") {
                        if (manageView.catalog.length) {
                            if (dy < 0 || dx < 0) manageView.catalogCursor = Math.max(0, manageView.catalogCursor - 1)
                            else if (dy > 0 || dx > 0) manageView.catalogCursor = Math.min(Math.max(0, manageView.catalog.length - 1), manageView.catalogCursor + 1)
                        } else {
                            if (dy < 0 || dx < 0) manageView.packCursor = Math.max(0, manageView.packCursor - 1)
                            else if (dy > 0 || dx > 0) manageView.packCursor = Math.min(Math.max(0, manageView.installed.length - 1), manageView.packCursor + 1)
                        }
                    } else {
                        if (dy < 0 || dx < 0) manageView.cursor = Math.max(0, manageView.cursor - 1)
                        else if (dy > 0 || dx > 0) manageView.cursor = Math.min(Math.max(0, manageView.backups.length - 1), manageView.cursor + 1)
                    }
                    return
                }
                root.moveCursor(dy || dx)
            }
            onTabRequested: direction => { if (!root.sessionOpen) root.moveCursor(direction) }
            onActivateRequested: {
                if (root.remoteOpen && root.service) root.service.input("ok")
                else if (root.filesOpen && fileView) {
                    if (fileView.confirmKind) fileView.runConfirm()
                    else if (fileView.nameMode) fileView.submitName()
                    else fileView.openItem(fileView.cursor)
                }
                else if (root.cliOpen && cliView) cliView.send()
                else if (root.appsOpen && appsView) {
                    if (appsView.confirmKind) appsView.runConfirm()
                    else appsView.launch()
                }
                else if (root.manageOpen && manageView) {
                    if (manageView.confirmKind) manageView.runConfirm()
                }
                else if (root.devOpen && devView) {
                    if (devView.confirmKind) devView.runConfirm()
                }
                else root.activate(root.cursor)
            }
            Flickable {
                id: scroll
                anchors.fill: parent
                contentHeight: content.implicitHeight
                clip: true
                interactive: true
                boundsBehavior: Flickable.StopAtBounds
                Column {
                    id: content
                    width: scroll.width
                    spacing: Style.space(8)

                    // ── Header ──
                    Row {
                        width: parent.width
                        spacing: 8
                        Text { text: "󰓻"; color: root.flipperOrange; font.family: Style.font.family; font.pixelSize: Style.font.subtitle }
                        Column {
                            width: parent.width - 32
                            Text {
                                text: root.info.hardware_name || "OmaFlip"
                                textFormat: Text.PlainText
                                color: root.barForeground
                                font.family: Style.font.family
                                font.pixelSize: Style.font.subtitle
                                font.bold: true
                            }
                            Row {
                                spacing: 6
                                Rectangle {
                                    width: 8
                                    height: 8
                                    radius: 4
                                    color: root.device ? root.statusGreen : Qt.alpha(root.barForeground, 0.25)
                                }
                                Text {
                                    text: root.status
                                    textFormat: Text.PlainText
                                    color: root.subtleText
                                    font.family: Style.font.family
                                    font.pixelSize: Style.font.caption
                                }
                            }
                        }
                    }

                    // ── No device hint ──
                    Text {
                        width: parent.width
                        visible: !root.device
                        text: root.service && root.service.ready && root.service.devices.length === 0
                            ? "Connect your Flipper Zero using a USB data cable."
                            : "Choose a device above, or inspect the service diagnostics below."
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                        color: root.subtleText
                        font.family: Style.font.family
                        font.pixelSize: Style.font.caption
                    }

                    // ── Stat Tiles ──
                    Row {
                        visible: root.device !== null && !root.sessionOpen
                        width: parent.width
                        spacing: 6

                        // Battery tile
                        Rectangle {
                            width: (parent.width - 12) / 3
                            height: 52
                            radius: root.cardR
                            color: root.cardBg
                            border.width: 1
                            border.color: root.cardBorder
                            Column {
                                anchors.centerIn: parent
                                spacing: 2
                                Text { anchors.horizontalCenter: parent.horizontalCenter; text: "󰁹"; color: root.subtleText; font.family: Style.font.family; font.pixelSize: Style.font.caption }
                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: root.power.charge_level !== undefined ? (root.power.charge_level || 0) + "%" : "—"
                                    textFormat: Text.PlainText
                                    color: root.barForeground
                                    font.family: Style.font.family
                                    font.pixelSize: Style.font.body
                                    font.bold: true
                                }
                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    visible: root.power.charge_state !== undefined
                                    text: root.power.charge_state || ""
                                    textFormat: Text.PlainText
                                    color: root.subtleText
                                    font.family: Style.font.family
                                    font.pixelSize: 8
                                }
                            }
                        }

                        // Storage tile
                        Rectangle {
                            width: (parent.width - 12) / 3
                            height: 52
                            radius: root.cardR
                            color: root.cardBg
                            border.width: 1
                            border.color: root.cardBorder
                            Column {
                                anchors.centerIn: parent
                                spacing: 2
                                Text { anchors.horizontalCenter: parent.horizontalCenter; text: "󰉋"; color: root.subtleText; font.family: Style.font.family; font.pixelSize: Style.font.caption }
                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: root.storageText || "—"
                                    textFormat: Text.PlainText
                                    color: root.barForeground
                                    font.family: Style.font.family
                                    font.pixelSize: Style.font.body
                                    font.bold: true
                                }
                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: "free"
                                    textFormat: Text.PlainText
                                    color: root.subtleText
                                    font.family: Style.font.family
                                    font.pixelSize: 8
                                }
                            }
                        }

                        // Firmware tile
                        Rectangle {
                            width: (parent.width - 12) / 3
                            height: 52
                            radius: root.cardR
                            color: root.cardBg
                            border.width: 1
                            border.color: root.cardBorder
                            Column {
                                anchors.centerIn: parent
                                spacing: 2
                                Text { anchors.horizontalCenter: parent.horizontalCenter; text: "󰍹"; color: root.subtleText; font.family: Style.font.family; font.pixelSize: Style.font.caption }
                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: root.info.firmware_version || "—"
                                    textFormat: Text.PlainText
                                    color: root.barForeground
                                    font.family: Style.font.family
                                    font.pixelSize: Style.font.body
                                    font.bold: true
                                    elide: Text.ElideRight
                                    maximumLineCount: 1
                                }
                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: root.info.firmware_origin_fork || ""
                                    textFormat: Text.PlainText
                                    color: root.subtleText
                                    font.family: Style.font.family
                                    font.pixelSize: 8
                                    elide: Text.ElideRight
                                    maximumLineCount: 1
                                }
                            }
                        }
                    }

                    // ── Quick Actions ──
                    Rectangle {
                        visible: root.device !== null && !root.sessionOpen
                        width: parent.width
                        radius: root.cardR
                        color: root.cardBg
                        border.width: 1
                        border.color: root.cardBorder
                        height: qaGrid.implicitHeight + 16
                        Column {
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 6
                            Text { text: "Actions"; textFormat: Text.PlainText; color: root.subtleText; font.family: Style.font.family; font.pixelSize: Style.font.caption; font.bold: true }
                            Grid {
                                id: qaGrid
                                width: parent.width
                                columns: 3
                                spacing: 6
                                Repeater {
                                    model: [
                                        {label: "Remote", op: "remote", icon: "󰖟"},
                                        {label: "Files", op: "files", icon: "󰉋"},
                                        {label: "CLI", op: "cli", icon: "󰆍"},
                                        {label: "Apps", op: "apps", icon: "󰀻"},
                                        {label: "Backup", op: "manage", icon: "󰁿"},
                                        {label: "Dev", op: "dev", icon: "󰅂"},
                                        {label: "Companion", op: "companion", icon: "󰟶"}
                                    ]
                                    Rectangle {
                                        required property var modelData
                                        width: (qaGrid.width - 12) / 3
                                        height: 44
                                        radius: 6
                                        color: qaMouse.pressed ? Qt.alpha(root.accentColor, 0.25) : (qaMouse.containsMouse ? Qt.alpha(root.accentColor, 0.12) : "transparent")
                                        border.width: 1
                                        border.color: qaMouse.containsMouse ? Qt.alpha(root.accentColor, 0.4) : Qt.alpha(root.barForeground, 0.06)
                                        Row {
                                            anchors.centerIn: parent
                                            spacing: 4
                                            Text { text: modelData.icon; color: root.accentColor; font.family: Style.font.family; font.pixelSize: Style.font.caption; anchors.verticalCenter: parent.verticalCenter }
                                            Text { text: modelData.label; textFormat: Text.PlainText; color: root.barForeground; font.family: Style.font.family; font.pixelSize: Style.font.caption; anchors.verticalCenter: parent.verticalCenter }
                                        }
                                        MouseArea {
                                            id: qaMouse
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: root.toggleSession(modelData.op)
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // ── Remote Card ──
                    Rectangle {
                        width: parent.width
                        visible: root.remoteOpen && root.device
                        radius: root.cardR
                        color: root.cardBg
                        border.width: 1
                        border.color: root.cardBorder
                        height: remoteCol.implicitHeight + 16
                        Column {
                            id: remoteCol
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 6
                            Text { text: "Remote"; textFormat: Text.PlainText; color: root.barForeground; font.family: Style.font.family; font.pixelSize: Style.font.caption; font.bold: true }
                            Oma.RemoteView { width: parent.width; service: root.service; foreground: root.barForeground }
                        }
                    }

                    // ── Files Card ──
                    Rectangle {
                        width: parent.width
                        visible: root.filesOpen && root.device
                        radius: root.cardR
                        color: root.cardBg
                        border.width: 1
                        border.color: root.cardBorder
                        height: filesCol.implicitHeight + 16
                        Column {
                            id: filesCol
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 6
                            Text { text: "Files"; textFormat: Text.PlainText; color: root.barForeground; font.family: Style.font.family; font.pixelSize: Style.font.caption; font.bold: true }
                            Oma.FilesView { id: fileView; width: parent.width; service: root.service; foreground: root.barForeground }
                        }
                    }

                    // ── CLI Card ──
                    Rectangle {
                        width: parent.width
                        visible: root.cliOpen && root.device
                        radius: root.cardR
                        color: root.cardBg
                        border.width: 1
                        border.color: root.cardBorder
                        height: cliCol.implicitHeight + 16
                        Column {
                            id: cliCol
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 6
                            Text { text: "CLI"; textFormat: Text.PlainText; color: root.barForeground; font.family: Style.font.family; font.pixelSize: Style.font.caption; font.bold: true }
                            Oma.CliView { id: cliView; width: parent.width; service: root.service; foreground: root.barForeground }
                        }
                    }

                    // ── Apps Card ──
                    Rectangle {
                        width: parent.width
                        visible: root.appsOpen && root.device
                        radius: root.cardR
                        color: root.cardBg
                        border.width: 1
                        border.color: root.cardBorder
                        height: appsCol.implicitHeight + 16
                        Column {
                            id: appsCol
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 6
                            Text { text: "Apps"; textFormat: Text.PlainText; color: root.barForeground; font.family: Style.font.family; font.pixelSize: Style.font.caption; font.bold: true }
                            Oma.AppsView { id: appsView; width: parent.width; service: root.service; foreground: root.barForeground }
                        }
                    }

                    // ── Backup Card ──
                    Rectangle {
                        width: parent.width
                        visible: root.manageOpen && root.device
                        radius: root.cardR
                        color: root.cardBg
                        border.width: 1
                        border.color: root.cardBorder
                        height: manageCol.implicitHeight + 16
                        Column {
                            id: manageCol
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 6
                            Text { text: "Backup"; textFormat: Text.PlainText; color: root.barForeground; font.family: Style.font.family; font.pixelSize: Style.font.caption; font.bold: true }
                            Oma.ManageView { id: manageView; width: parent.width; service: root.service; foreground: root.barForeground }
                        }
                    }

                    // ── Dev Card ──
                    Rectangle {
                        width: parent.width
                        visible: root.devOpen && root.device
                        radius: root.cardR
                        color: root.cardBg
                        border.width: 1
                        border.color: root.cardBorder
                        height: devCol.implicitHeight + 16
                        Column {
                            id: devCol
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 6
                            Text { text: "Dev"; textFormat: Text.PlainText; color: root.barForeground; font.family: Style.font.family; font.pixelSize: Style.font.caption; font.bold: true }
                            Oma.DevView { id: devView; width: parent.width; service: root.service; hostWidget: root.hostWidget; foreground: root.barForeground }
                        }
                    }

                    // ── Companion Card ──
                    Rectangle {
                        width: parent.width
                        visible: root.companionOpen && root.device
                        radius: root.cardR
                        color: root.cardBg
                        border.width: 1
                        border.color: root.cardBorder
                        height: companionCol.implicitHeight + 16
                        Column {
                            id: companionCol
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 6
                            Text {
                                text: "Fliparchy Companion"
                                textFormat: Text.PlainText
                                color: root.barForeground
                                font.family: Style.font.family
                                font.pixelSize: Style.font.caption
                                font.bold: true
                            }
                            Text {
                                width: parent.width
                                text: root.device && root.device.companion
                                    ? (root.device.companion.status || "Starting…")
                                    : "Starting…"
                                textFormat: Text.PlainText
                                color: root.accentColor
                                font.family: Style.font.family
                                font.pixelSize: Style.font.body
                            }
                            Text {
                                width: parent.width
                                text: "Keep this session open. Use the Flipper buttons for volume, workspaces, media, and hold OK to lock."
                                textFormat: Text.PlainText
                                wrapMode: Text.Wrap
                                color: root.subtleText
                                font.family: Style.font.family
                                font.pixelSize: Style.font.caption
                            }
                            Text {
                                width: parent.width
                                visible: !!(root.device && root.device.companion && root.device.companion.lastAction)
                                text: (root.device && root.device.companion && root.device.companion.lastAction) ? "Last: " + root.device.companion.lastAction : ""
                                textFormat: Text.PlainText
                                elide: Text.ElideRight
                                color: root.subtleText
                                font.family: Style.font.family
                                font.pixelSize: Style.font.caption
                            }
                        }
                    }

                    // ── Device Details (collapsible) ──
                    Rectangle {
                        width: parent.width
                        visible: root.details && root.device !== null && !root.sessionOpen
                        radius: root.cardR
                        color: root.cardBg
                        border.width: 1
                        border.color: root.cardBorder
                        height: detGrid.implicitHeight + 16
                        Grid {
                            id: detGrid
                            anchors.fill: parent
                            anchors.margins: 10
                            columns: 2
                            columnSpacing: 10
                            rowSpacing: 2
                            width: parent.width - 20
                            Oma.InfoRow { width: detGrid.width / 2; label: "USB"; value: root.device ? root.device.usbSerial : null; foreground: root.barForeground }
                            Oma.InfoRow { width: detGrid.width / 2; label: "HW"; value: root.info.hardware_ver; foreground: root.barForeground }
                            Oma.InfoRow { width: detGrid.width / 2; label: "Region"; value: root.regionText || null; foreground: root.barForeground }
                            Oma.InfoRow { width: detGrid.width / 2; label: "Health"; value: root.power.battery_health !== undefined ? root.power.battery_health + "%" : null; foreground: root.barForeground }
                            Oma.InfoRow { width: detGrid.width / 2; label: "Port"; value: root.device ? root.device.port : null; foreground: root.barForeground }
                            Oma.InfoRow { width: detGrid.width / 2; label: "RPC"; value: root.rpcText || null; foreground: root.barForeground }
                            Oma.InfoRow { width: detGrid.width / 2; label: "Ping"; value: root.rpc.pingMs !== undefined ? root.rpc.pingMs + " ms" : null; foreground: root.barForeground }
                            Oma.InfoRow { width: detGrid.width / 2; label: "Build"; value: root.service && root.service.backendVersion ? root.service.backendVersion : null; foreground: root.barForeground }
                        }
                    }

                    // ── Error Card ──
                    Rectangle {
                        visible: root.errorText.length > 0 || (root.device && root.device.warning)
                        width: parent.width
                        radius: root.cardR
                        color: Qt.alpha(Color.urgent, 0.08)
                        border.width: 1
                        border.color: Qt.alpha(Color.urgent, 0.25)
                        height: errorCol.implicitHeight + 16
                        Column {
                            id: errorCol
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 4
                            Text {
                                width: parent.width
                                text: root.errorText || (root.device ? root.device.warning : "")
                                textFormat: Text.PlainText
                                wrapMode: Text.WrapAnywhere
                                color: root.barForeground
                                font.family: Style.font.family
                                font.pixelSize: Style.font.caption
                            }
                        }
                    }

                    // ── Diagnostics Card ──
                    Rectangle {
                        visible: root.showDiagnostics
                        width: parent.width
                        radius: root.cardR
                        color: root.cardBg
                        border.width: 1
                        border.color: root.cardBorder
                        height: diagCol.implicitHeight + 16
                        Column {
                            id: diagCol
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 4
                            Text { text: "Diagnostics"; textFormat: Text.PlainText; color: root.subtleText; font.family: Style.font.family; font.pixelSize: Style.font.caption; font.bold: true }
                            Text {
                                width: parent.width
                                text: {
                                    if (!root.service) return "Service not loaded."
                                    const diag = root.service.diagnostics || "No backend errors recorded."
                                    let slim = {}
                                    if (root.device && typeof root.device === "object") {
                                        slim = {key: root.device.key, state: root.device.state, identityConfirmed: root.device.identityConfirmed, usbSerial: root.device.usbSerial, port: root.device.port, origin: root.device.origin}
                                        if (root.device.info && typeof root.device.info === "object") slim.info = {hardware_name: root.device.info.hardware_name, firmware_version: root.device.info.firmware_version, firmware_origin_fork: root.device.info.firmware_origin_fork}
                                        if (root.device.error && root.device.error.reason) slim.error = root.device.error
                                    }
                                    let dump = ""
                                    try { dump = JSON.stringify(slim, null, 2) } catch (e) { dump = String(e) }
                                    if (dump.length > 3000) dump = dump.slice(0, 3000) + "… (truncated)"
                                    const head = diag.length > 2000 ? "…" + diag.slice(-2000) : diag
                                    return head + "\n" + dump
                                }
                                textFormat: Text.PlainText
                                wrapMode: Text.WrapAnywhere
                                color: Qt.alpha(root.barForeground, 0.7)
                                font.family: Style.font.family
                                font.pixelSize: Style.font.caption
                            }
                        }
                    }

                    // ── Settings Toggle ──
                    Rectangle {
                        visible: !root.sessionOpen && !root.settingsOpen
                        width: parent.width
                        radius: root.cardR
                        color: root.cardBg
                        border.width: 1
                        border.color: root.cardBorder
                        height: 28
                        Text {
                            anchors.centerIn: parent
                            text: "⚙ Settings"
                            textFormat: Text.PlainText
                            color: root.subtleText
                            font.family: Style.font.family
                            font.pixelSize: Style.font.caption
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.settingsOpen = true
                        }
                    }

                    // ── Settings Card (expanded) ──
                    Rectangle {
                        visible: !root.sessionOpen && root.settingsOpen
                        width: parent.width
                        radius: root.cardR
                        color: root.cardBg
                        border.width: 1
                        border.color: root.cardBorder
                        height: settingsCol.implicitHeight + 16
                        Column {
                            id: settingsCol
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 4
                            Text { text: "Settings"; textFormat: Text.PlainText; color: root.subtleText; font.family: Style.font.family; font.pixelSize: Style.font.caption; font.bold: true }
                            Column {
                                id: settingsList
                                width: parent.width
                                spacing: Style.space(2)
                                Repeater {
                                    id: settingsRepeater
                                    model: root.settingsActions
                                    Oma.Action {
                                        required property var modelData
                                        required property int index
                                        width: settingsList.width
                                        text: modelData.label
                                        selected: false
                                        foreground: root.barForeground
                                        onTriggered: { root.activateSettings(index) }
                                    }
                                }
                            }
                            Rectangle {
                                width: parent.width
                                height: 24
                                radius: 3
                                color: "transparent"
                                Text {
                                    anchors.centerIn: parent
                                    text: "▾ Collapse"
                                    textFormat: Text.PlainText
                                    color: root.subtleText
                                    font.family: Style.font.family
                                    font.pixelSize: Style.font.caption
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.settingsOpen = false
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    function moveCursor(direction) {
        if (root.settingsOpen) {
            const count = settingsActions.length
            if (!count) return
            cursor = (cursor + direction + count) % count
            const item = settingsRepeater.itemAt(cursor)
            if (!item) return
            const point = item.mapToItem(content, 0, 0)
            if (point.y < scroll.contentY) scroll.contentY = point.y
            else if (point.y + item.height > scroll.contentY + scroll.height)
                scroll.contentY = point.y + item.height - scroll.height
            return
        }
        const total = actions.length
        if (!total) return
        cursor = (cursor + direction + total) % total
        cursor = Math.max(0, Math.min(cursor, total - 1))
        if (cursor === 0) scroll.contentY = 0
    }
    onActionsChanged: cursor = Math.max(0, Math.min(cursor, Math.max(0, actions.length - 1)))
}
