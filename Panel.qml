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
    readonly property bool canUseTools: !!(service && service.ready && device && device.identityConfirmed)
    readonly property string currentPage: remoteOpen ? "remote" : filesOpen ? "files" : cliOpen ? "cli"
        : appsOpen ? "apps" : manageOpen ? "manage" : devOpen ? "dev" : companionOpen ? "companion"
        : settingsOpen ? "settings" : showDiagnostics ? "diagnostics" : "overview"
    readonly property var actions: [
        {label: "Overview", op: "overview", icon: "󰓻"},
        {label: "Remote", op: "remote", icon: "󰖟"},
        {label: "Files", op: "files", icon: "󰉋"},
        {label: "CLI", op: "cli", icon: "󰆍"},
        {label: "Apps", op: "apps", icon: "󰀻"},
        {label: "Manage", op: "manage", icon: "󰁿"},
        {label: "Developer", op: "dev", icon: "󰅂"},
        {label: "Companion", op: "companion", icon: "󰟶"},
        {label: "Settings", op: "settings", icon: "⚙"},
        {label: "Diagnostics", op: "diagnostics", icon: "󰋼"}
    ]
    function pageEnabled(op) {
        return op === "overview" || op === "settings" || op === "diagnostics" || canUseTools
    }
    function navigate(op) {
        if (!pageEnabled(op) || currentPage === op) return
        settingsOpen = false
        showDiagnostics = false
        if (op === "overview" || op === "settings" || op === "diagnostics") {
            closeExclusive("")
            settingsOpen = op === "settings"
            showDiagnostics = op === "diagnostics"
        } else toggleSession(op)
        cursor = Math.max(0, actions.findIndex(item => item.op === op))
        scroll.contentY = 0
        Qt.callLater(() => keys.forceActiveFocus())
    }
    function activate(index) {
        const action = actions[index]
        if (action) navigate(action.op)
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
    function performAction(op) {
        if (op === "refresh" && service && !sessionOpen) service.refresh()
        else if (op === "screenshot" && service && remoteOpen) service.screenshot()
        else if (op === "copyShot") {
            const shots = root.device && root.device.screenshots
            if (shots && shots.length && shots[0]) {
                // wl-copy accepts PNG on stdin; the sole shell value is quoted.
                Quickshell.execDetached(["bash", "-c", "wl-copy -t image/png < " + Util.shellQuote(String(shots[0]))])
            }
        } else if (op === "copy") Quickshell.clipboardText = errorText
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
        cursor = Math.max(0, actions.findIndex(item => item.op === currentPage))
        root.controller.show()
    }
    onOpenedChanged: if (!opened) {
        closeExclusive("")
    }
    Connections {
        target: service
        function onSelectedKeyChanged() { root.closeExclusive("") }
        function onDevicesChanged() {
            if (!root.device || !root.device.identityConfirmed) root.closeExclusive("")
        }
    }

    readonly property color accentColor: Color.accent
    readonly property color cardBg: Qt.alpha(root.barForeground, 0.04)
    readonly property color cardBorder: Qt.alpha(root.barForeground, 0.14)
    readonly property color subtleText: Qt.alpha(root.barForeground, 0.65)
    readonly property int cardR: Style.cornerRadius

    Ui.KeyboardPanel {
        id: popup
        anchorItem: root.anchorItem
        owner: root.hostWidget || root
        bar: root.bar
        open: root.opened
        focusTarget: keys
        contentWidth: popup.fittedContentWidth(Style.space(760))
        contentHeight: popup.fittedContentHeight(Style.space(650))

        Ui.PanelKeyCatcher {
            id: keys
            anchors.fill: parent
            blocked: (root.filesOpen && fileView.inputActive) || (root.appsOpen && appsView.inputActive)
                || (root.cliOpen && cliView.inputActive) || (root.devOpen && devView.inputActive)
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
                if (root.settingsOpen || root.showDiagnostics) { root.navigate("overview"); return }
                root.close()
            }
            onMoveRequested: (dx, dy) => {
                const focused = root.focusedChild(keys)
                if (focused && focused.objectName.indexOf("omaflip-navigation-") === 0) {
                    root.cursor = actions.findIndex(item => focused.objectName === "omaflip-navigation-" + item.op)
                    root.moveCursor(dy || dx)
                    const next = compactNav.visible ? compactNavRepeater.itemAt(root.cursor) : navRepeater.itemAt(root.cursor)
                    if (next) next.forceActiveFocus(Qt.TabFocusReason)
                    return
                }
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
                    } else if (manageView.section === "backups") {
                        if (dy < 0 || dx < 0) manageView.cursor = Math.max(0, manageView.cursor - 1)
                        else if (dy > 0 || dx > 0) manageView.cursor = Math.min(Math.max(0, manageView.backups.length - 1), manageView.cursor + 1)
                    }
                    return
                }
                root.moveCursor(dy || dx)
            }
            onTabRequested: direction => {
                const focused = root.focusedChild(keys) || keys
                const next = focused.nextItemInFocusChain(direction > 0)
                if (next) next.forceActiveFocus(Qt.TabFocusReason)
            }
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
            Column {
                anchors.fill: parent
                spacing: Style.space(14)
                Column {
                    id: header
                    width: parent.width
                    spacing: Style.space(8)
                    Row {
                        width: parent.width
                        spacing: Style.space(12)
                        Text { text: "󰓻"; color: root.accentColor; font.family: Style.font.family; font.pixelSize: Style.space(30) }
                        Column {
                            width: parent.width - Style.space(48) - closeButton.width - parent.spacing * 2
                            spacing: Style.space(4)
                            Text {
                                width: parent.width
                                text: root.info.hardware_name || "OmaFlip"
                                textFormat: Text.PlainText
                                color: root.barForeground
                                font.family: Style.font.family
                                font.pixelSize: Style.font.subtitle
                                font.bold: true
                                elide: Text.ElideRight
                            }
                            Text {
                                width: parent.width
                                text: root.status
                                textFormat: Text.PlainText
                                color: root.errorText ? Color.urgent : root.subtleText
                                font.family: Style.font.family
                                font.pixelSize: Style.font.caption
                                elide: Text.ElideRight
                            }
                        }
                        Oma.ToolButton { id: closeButton; text: "Close"; foreground: root.barForeground; onTriggered: root.close() }
                    }
                    Flow {
                        width: parent.width
                        spacing: Style.space(6)
                        visible: !!(root.service && root.service.devices.length > 1)
                        Repeater {
                            model: root.service ? root.service.devices : []
                            Oma.ToolButton {
                                required property var modelData
                                text: (modelData.info && modelData.info.hardware_name) || modelData.usbSerial || modelData.key
                                foreground: root.barForeground
                                primary: root.service && modelData.key === root.service.selectedKey
                                onTriggered: {
                                    root.closeExclusive("")
                                    root.service.selectDevice(modelData.key)
                                    if (root.hostWidget) root.hostWidget.persist("preferredDevice", modelData.id)
                                }
                            }
                        }
                    }
                }
                Rectangle { width: parent.width; height: 1; color: root.cardBorder }
                Flow {
                    id: compactNav
                    width: parent.width
                    visible: keys.width < Style.space(560)
                    spacing: Style.space(4)
                    Repeater {
                        id: compactNavRepeater
                        model: root.actions
                        Oma.ToolButton {
                            required property var modelData
                            required property int index
                            objectName: "omaflip-navigation-" + modelData.op
                            text: modelData.label
                            foreground: root.barForeground
                            enabled: root.pageEnabled(modelData.op)
                            primary: root.currentPage === modelData.op
                            border.color: keys.activeFocus && root.cursor === index ? root.accentColor : Qt.alpha(root.barForeground, 0.2)
                            onTriggered: root.activate(index)
                        }
                    }
                }
                Row {
                    id: workspace
                    width: parent.width
                    height: Math.max(Style.space(80), keys.height - y - footer.implicitHeight - Style.space(14))
                    spacing: sidebar.visible ? Style.space(16) : 0
                    Rectangle {
                        id: sidebar
                        width: Style.space(156)
                        height: parent.height
                        visible: !compactNav.visible
                        radius: root.cardR
                        color: root.cardBg
                        Flickable {
                            anchors.fill: parent
                            anchors.margins: Style.space(8)
                            clip: true
                            contentHeight: navigation.implicitHeight
                            boundsBehavior: Flickable.StopAtBounds
                            Column {
                                id: navigation
                                width: parent.width
                                spacing: Style.space(4)
                                Repeater {
                                    id: navRepeater
                                    model: root.actions
                                    Oma.Action {
                                        required property var modelData
                                        required property int index
                                        width: navigation.width
                                        objectName: "omaflip-navigation-" + modelData.op
                                        text: modelData.icon + "  " + modelData.label
                                        selected: root.currentPage === modelData.op
                                        keyboardSelected: keys.activeFocus && root.cursor === index
                                        enabled: root.pageEnabled(modelData.op)
                                        foreground: root.barForeground
                                        onTriggered: root.activate(index)
                                    }
                                }
                            }
                        }
                    }
                    Flickable {
                        id: scroll
                        width: workspace.width - (sidebar.visible ? sidebar.width + workspace.spacing : 0)
                        height: workspace.height
                        contentHeight: content.implicitHeight
                        contentWidth: width
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        Column {
                            id: content
                            width: scroll.width
                            spacing: Style.space(14)
                            // Errors stay visible on every tool, with a usable recovery path.
                            Rectangle {
                                visible: !!(root.errorText || (root.device && root.device.warning))
                                width: parent.width
                                height: errorContent.implicitHeight + Style.space(24)
                                radius: root.cardR
                                color: Qt.alpha(Color.urgent, 0.08)
                                border.width: 1
                                border.color: Qt.alpha(Color.urgent, 0.3)
                                Column {
                                    id: errorContent
                                    x: Style.space(12); y: Style.space(12)
                                    width: parent.width - Style.space(24)
                                    spacing: Style.space(8)
                                    Text {
                                        width: parent.width
                                        text: root.errorText || (root.device && root.device.warning) || ""
                                        textFormat: Text.PlainText
                                        wrapMode: Text.WrapAnywhere
                                        color: root.barForeground
                                        font.family: Style.font.family
                                        font.pixelSize: Style.font.caption
                                    }
                                    Flow {
                                        width: parent.width
                                        spacing: Style.space(6)
                                        Oma.ToolButton { text: "Copy error"; foreground: root.barForeground; visible: !!root.errorText; onTriggered: root.performAction("copy") }
                                        Oma.ToolButton { text: "Diagnostics"; foreground: root.barForeground; onTriggered: root.navigate("diagnostics") }
                                    }
                                }
                            }
                            Column {
                                width: parent.width
                                visible: root.currentPage === "overview"
                                spacing: Style.space(14)
                                Text { text: "Device overview"; color: root.barForeground; font.family: Style.font.family; font.pixelSize: Style.font.subtitle; font.bold: true }
                                Text {
                                    width: parent.width
                                    text: !root.service || !root.service.ready ? "The backend is unavailable. Open Settings to build or restart it."
                                        : !root.device ? (root.service.devices.length ? "Choose a device using the selector above." : "Connect your Flipper Zero with a USB data cable. It will appear here automatically.")
                                        : !root.device.identityConfirmed ? "STM32 DFU identity is unconfirmed. Device tools and flashing are unavailable."
                                        : "Choose a tool from the navigation. Only one device session is active at a time."
                                    textFormat: Text.PlainText
                                    wrapMode: Text.Wrap
                                    color: root.subtleText
                                    font.family: Style.font.family
                                    font.pixelSize: Style.font.body
                                }
                                Flow {
                                    width: parent.width
                                    spacing: Style.space(8)
                                    visible: !!root.device
                                    Repeater {
                                        model: [
                                            {label: "Battery", value: root.power.charge_level !== undefined ? root.power.charge_level + "%" : "Unavailable", note: root.power.charge_state || "Last read"},
                                            {label: "SD storage", value: root.storageText || "Unavailable", note: "Last read"},
                                            {label: "Firmware", value: root.info.firmware_version || "Unavailable", note: root.info.firmware_origin_fork || "Unknown origin"}
                                        ]
                                        Rectangle {
                                            required property var modelData
                                            width: parent.width < Style.space(360) ? parent.width : (parent.width - Style.space(16)) / 3
                                            height: tile.implicitHeight + Style.space(24)
                                            radius: root.cardR
                                            color: root.cardBg
                                            border.width: 1
                                            border.color: root.cardBorder
                                            Column {
                                                id: tile
                                                x: Style.space(12); y: Style.space(12)
                                                width: parent.width - Style.space(24)
                                                spacing: Style.space(6)
                                                Text { width: parent.width; text: modelData.label; color: root.subtleText; font.family: Style.font.family; font.pixelSize: Style.font.caption }
                                                Text { width: parent.width; text: modelData.value; textFormat: Text.PlainText; wrapMode: Text.WrapAnywhere; color: root.barForeground; font.family: Style.font.family; font.pixelSize: Style.font.body; font.bold: true }
                                                Text { width: parent.width; text: modelData.note; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: root.subtleText; font.family: Style.font.family; font.pixelSize: Style.font.caption }
                                            }
                                        }
                                    }
                                }
                                Flow {
                                    width: parent.width
                                    spacing: Style.space(8)
                                    Oma.ToolButton { text: "Open remote"; primary: true; enabled: root.canUseTools; foreground: root.barForeground; onTriggered: root.navigate("remote") }
                                    Oma.ToolButton { text: "Browse files"; enabled: root.canUseTools; foreground: root.barForeground; onTriggered: root.navigate("files") }
                                    Oma.ToolButton { text: "Refresh / Retry"; enabled: !!(root.service && root.service.ready); foreground: root.barForeground; onTriggered: root.performAction("refresh") }
                                    Oma.ToolButton { text: root.details ? "Hide details" : "Device details"; visible: !!root.device; foreground: root.barForeground; onTriggered: root.details = !root.details }
                                }
                                Column {
                                    width: parent.width
                                    visible: root.details && !!root.device
                                    spacing: Style.space(8)
                                    Oma.InfoRow { width: parent.width; label: "USB serial"; value: root.device ? root.device.usbSerial : null; foreground: root.barForeground }
                                    Oma.InfoRow { width: parent.width; label: "Hardware"; value: root.info.hardware_ver; foreground: root.barForeground }
                                    Oma.InfoRow { width: parent.width; label: "Region"; value: root.regionText; foreground: root.barForeground }
                                    Oma.InfoRow { width: parent.width; label: "Battery health"; value: root.power.battery_health !== undefined ? root.power.battery_health + "%" : null; foreground: root.barForeground }
                                    Oma.InfoRow { width: parent.width; label: "Serial port"; value: root.device ? root.device.port : null; foreground: root.barForeground }
                                    Oma.InfoRow { width: parent.width; label: "RPC"; value: root.rpcText; foreground: root.barForeground }
                                    Oma.InfoRow { width: parent.width; label: "Ping"; value: root.rpc.pingMs !== undefined ? root.rpc.pingMs + " ms" : null; foreground: root.barForeground }
                                    Oma.InfoRow { width: parent.width; label: "Backend"; value: root.service ? root.service.backendVersion : null; foreground: root.barForeground }
                                }
                            }
                            Column {
                                width: parent.width; spacing: Style.space(10); visible: root.remoteOpen && !!root.device
                                Text { text: "Remote control"; color: root.barForeground; font.family: Style.font.family; font.pixelSize: Style.font.subtitle; font.bold: true }
                                Oma.RemoteView { width: parent.width; service: root.remoteOpen ? root.service : null; foreground: root.barForeground }
                                Flow {
                                    width: parent.width; spacing: Style.space(8)
                                    Oma.ToolButton { text: "Save screenshot"; foreground: root.barForeground; enabled: !!(root.service && root.service.frameData); onTriggered: root.performAction("screenshot") }
                                    Oma.ToolButton { text: "Copy last screenshot"; foreground: root.barForeground; enabled: !!(root.device && root.device.screenshots && root.device.screenshots.length); onTriggered: root.performAction("copyShot") }
                                }
                            }
                            Oma.FilesView { id: fileView; width: parent.width; visible: root.filesOpen && !!root.device; service: root.filesOpen ? root.service : null; foreground: root.barForeground }
                            Oma.CliView { id: cliView; width: parent.width; visible: root.cliOpen && !!root.device; service: root.cliOpen ? root.service : null; foreground: root.barForeground }
                            Oma.AppsView { id: appsView; width: parent.width; visible: root.appsOpen && !!root.device; service: root.appsOpen ? root.service : null; foreground: root.barForeground }
                            Oma.ManageView { id: manageView; width: parent.width; visible: root.manageOpen && !!root.device; service: root.manageOpen ? root.service : null; foreground: root.barForeground }
                            Oma.DevView { id: devView; width: parent.width; visible: root.devOpen && !!root.device; service: root.devOpen ? root.service : null; hostWidget: root.hostWidget; foreground: root.barForeground }
                    // ── Companion Card ──
                    Rectangle {
                        width: parent.width
                        visible: root.companionOpen && root.device
                        radius: root.cardR
                        color: root.cardBg
                        border.width: 1
                        border.color: root.cardBorder
                        height: companionCol.implicitHeight + Style.space(24)
                        Column {
                            id: companionCol
                            x: Style.space(12)
                            y: Style.space(12)
                            width: parent.width - Style.space(24)
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

                    // ── Diagnostics Card ──
                    Rectangle {
                        visible: root.showDiagnostics
                        width: parent.width
                        radius: root.cardR
                        color: root.cardBg
                        border.width: 1
                        border.color: root.cardBorder
                        height: diagCol.implicitHeight + Style.space(24)
                        Column {
                            id: diagCol
                            x: Style.space(12)
                            y: Style.space(12)
                            width: parent.width - Style.space(24)
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

                            Column {
                                width: parent.width
                                visible: root.settingsOpen
                                spacing: Style.space(12)
                                Text { text: "Settings"; color: root.barForeground; font.family: Style.font.family; font.pixelSize: Style.font.subtitle; font.bold: true }
                                Text { width: parent.width; text: "Saved in this widget's Omarchy settings. Device access setup asks for authorization in a terminal."; wrapMode: Text.Wrap; color: root.subtleText; font.family: Style.font.family; font.pixelSize: Style.font.caption }
                                Repeater {
                                    id: settingsRepeater
                                    model: root.settingsActions
                                    Oma.Action {
                                        required property var modelData
                                        required property int index
                                        width: content.width
                                        text: modelData.label
                                        foreground: root.barForeground
                                        onTriggered: root.activateSettings(index)
                                    }
                                }
                            }
                        }
                        // Visible scroll affordance without replacing the hosted popup chrome.
                        Rectangle {
                            anchors.right: parent.right
                            width: Style.space(3)
                            height: Math.max(Style.space(20), scroll.height * scroll.height / Math.max(1, scroll.contentHeight))
                            y: scroll.visibleArea.yPosition * scroll.height
                            radius: width / 2
                            color: Qt.alpha(root.barForeground, 0.3)
                            visible: scroll.contentHeight > scroll.height
                        }
                    }
                }
                Text {
                    id: footer
                    width: parent.width
                    text: root.sessionOpen ? "One active session · switch tools to release the port · Esc returns to overview" : "Tab moves focus · arrows select navigation · Enter opens · Esc closes"
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    color: root.subtleText
                    font.family: Style.font.family
                    font.pixelSize: Style.font.caption
                }
            }
        }
    }
    function focusedChild(item) {
        if (!item) return null
        for (const child of item.children || []) {
            const focused = focusedChild(child)
            if (focused) return focused
        }
        return item.activeFocus ? item : null
    }
    function moveCursor(direction) {
        const count = actions.length
        if (!count) return
        for (let i = 0; i < count; i++) {
            cursor = (cursor + direction + count) % count
            if (pageEnabled(actions[cursor].op)) break
        }
    }
}
