import QtQuick
import qs.Commons

Column {
    id: root
    property var service: null
    property var hostWidget: null
    property color foreground: Color.foreground
    spacing: Style.space(10)

    readonly property var dev: {
        const device = service && service.selectedDevice
        return device && device.dev && typeof device.dev === "object" ? device.dev : ({open: false})
    }
    readonly property var fam: dev.fam && typeof dev.fam === "object" ? dev.fam : ({})
    readonly property bool inputActive: projectInput.activeFocus || appIdInput.activeFocus
    readonly property bool idle: !!service && !!dev.ready && !dev.busy
    readonly property bool projectValid: projectInput.text.trim().indexOf("/") === 0 && projectInput.text.indexOf("..") < 0
    readonly property bool projectApplied: projectValid && projectInput.text.trim() === (dev.project || "")
    readonly property bool appIdValid: /^[a-z][a-z0-9_]{0,31}$/.test(appIdInput.text.trim())
    readonly property bool canDeploy: idle && projectApplied && !!dev.fap
    property string confirmKind: ""
    property string inspectKind: "ping"
    property string lastAppliedProject: ""
    property bool inspectorExpanded: false

    function applyProject() {
        const path = projectInput.text.trim()
        if (!idle || !projectValid || path === lastAppliedProject) return
        lastAppliedProject = path
        service.devProject(path)
        if (hostWidget && path) hostWidget.persist("devProject", path)
    }
    function fillProject() {
        if (projectInput.text.trim()) return
        const saved = hostWidget ? hostWidget.setting("devProject", "") : ""
        projectInput.text = saved || dev.defaultProject || ""
        if (projectInput.text) applyProject()
    }
    function runConfirm() {
        if (!canDeploy) return
        if (confirmKind === "deploy") service.devDeploy(true)
        confirmKind = ""
    }
    function syncProject() {
        if (!visible || !service || !dev.ready) return
        fillProject()
        if (!lastAppliedProject) applyProject()
    }
    onServiceChanged: {
        lastAppliedProject = ""
        confirmKind = ""
        Qt.callLater(root.syncProject)
    }
    onDevChanged: Qt.callLater(root.syncProject)
    onVisibleChanged: if (visible) Qt.callLater(root.syncProject)

    Row {
        width: parent.width
        spacing: Style.space(8)
        Rectangle {
            width: Style.space(8)
            height: width
            radius: width / 2
            anchors.verticalCenter: parent.verticalCenter
            color: dev.error ? Color.urgent : (dev.ready ? Color.accent : Qt.alpha(root.foreground, 0.35))
        }
        Text {
            width: parent.width - Style.space(16)
            text: !root.service ? "No device selected" : (dev.busy ? "Developer · working…" : (dev.ready ? "Developer · ready" : (dev.error ? "Developer · unavailable" : "Connecting developer session…")))
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            color: root.foreground
            font.family: Style.font.family
            font.pixelSize: Style.font.body
            font.bold: true
        }
    }
    Text {
        width: parent.width
        visible: !!dev.error
        wrapMode: Text.Wrap
        textFormat: Text.PlainText
        color: Color.urgent
        font.family: Style.font.family
        font.pixelSize: Style.font.caption
        text: "Developer: " + (dev.error || "")
    }

    Column {
        width: parent.width
        spacing: Style.space(6)
        Text {
            text: "PROJECT"
            color: Qt.alpha(root.foreground, 0.6)
            font.family: Style.font.family
            font.pixelSize: Style.font.caption
            font.bold: true
        }
        InputField {
            id: projectInput
            width: parent.width
            foreground: root.foreground
            placeholderText: "Absolute project folder, e.g. /home/user/my_app"
            enabled: !dev.busy && root.confirmKind === ""
            onAccepted: root.applyProject()
            onEditingFinished: root.applyProject()
            Component.onCompleted: root.fillProject()
        }
        Text {
            width: parent.width
            text: !root.projectValid ? "Choose an absolute folder. Press Enter to apply." : (!root.projectApplied ? "Press Enter to apply this folder." : (root.fam.appid ? root.fam.appid + (root.fam.name ? " · " + root.fam.name : "") : "No application.fam · create a template or choose an existing app."))
            textFormat: Text.PlainText
            wrapMode: Text.WrapAnywhere
            color: Qt.alpha(root.foreground, 0.65)
            font.family: Style.font.family
            font.pixelSize: Style.font.caption
        }
        Flow {
            width: parent.width
            spacing: Style.space(6)
            ToolButton {
                text: "Build"
                foreground: root.foreground
                primary: true
                enabled: root.idle && root.projectApplied && !!root.fam.appid && root.confirmKind === ""
                onTriggered: { root.applyProject(); root.service.devBuild() }
            }
            ToolButton {
                text: "Lint"
                foreground: root.foreground
                enabled: root.idle && root.projectApplied && !!root.fam.appid && root.confirmKind === ""
                onTriggered: { root.applyProject(); root.service.devLint() }
            }
            ToolButton {
                text: "Deploy…"
                foreground: root.foreground
                enabled: root.canDeploy && root.confirmKind === ""
                onTriggered: { root.applyProject(); root.confirmKind = "deploy" }
            }
        }
        Text {
            width: parent.width
            text: dev.fap ? "Built FAP · " + dev.fap : "Build first to enable deployment."
            textFormat: Text.PlainText
            wrapMode: Text.WrapAnywhere
            color: Qt.alpha(root.foreground, 0.55)
            font.family: Style.font.family
            font.pixelSize: Style.font.caption
        }
    }

    Rectangle {
        width: parent.width
        height: confirmation.implicitHeight + Style.space(24)
        visible: root.confirmKind !== ""
        radius: Style.cornerRadius
        color: Qt.alpha(Color.urgent, 0.07)
        border.width: 1
        border.color: Qt.alpha(Color.urgent, 0.4)
        Column {
            id: confirmation
            anchors.fill: parent
            anchors.margins: Style.space(12)
            spacing: Style.space(8)
            Text {
                width: parent.width
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
                color: root.foreground
                font.family: Style.font.family
                font.pixelSize: Style.font.body
                text: "Upload the built FAP and start it on the Flipper? This replaces an app with the same name."
            }
            Flow {
                width: parent.width
                spacing: Style.space(8)
                ToolButton {
                    text: "Confirm deploy"
                    foreground: root.foreground
                    destructive: true
                    enabled: root.canDeploy
                    onTriggered: root.runConfirm()
                }
                ToolButton {
                    text: "Cancel"
                    foreground: root.foreground
                    onTriggered: root.confirmKind = ""
                }
            }
        }
    }

    Text {
        text: "BUILD & DEVICE OUTPUT"
        color: Qt.alpha(root.foreground, 0.6)
        font.family: Style.font.family
        font.pixelSize: Style.font.caption
        font.bold: true
    }
    Rectangle {
        width: parent.width
        height: Style.space(156)
        radius: Style.cornerRadius
        color: Qt.alpha(root.foreground, 0.035)
        border.width: 1
        border.color: Qt.alpha(root.foreground, 0.16)
        Flickable {
            id: logScroll
            anchors.fill: parent
            anchors.margins: Style.space(10)
            clip: true
            contentWidth: width
            contentHeight: logText.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            Text {
                id: logText
                width: logScroll.width
                readonly property string output: (root.dev.log || "") + (root.dev.inspect ? "\n" + root.dev.inspect : "")
                text: output.length ? output : (root.dev.busy ? "Operation in progress…" : "Build, lint, and inspector output will appear here.")
                textFormat: Text.PlainText
                wrapMode: Text.WrapAnywhere
                color: Qt.alpha(root.foreground, output.length ? 0.9 : 0.55)
                font.family: "monospace"
                font.pixelSize: Style.font.caption
            }
            onContentHeightChanged: contentY = Math.max(0, contentHeight - height)
        }
    }

    Column {
        width: parent.width
        spacing: Style.space(6)
        visible: root.confirmKind === ""
        Text {
            text: "SETUP & NEW APPLICATION"
            color: Qt.alpha(root.foreground, 0.6)
            font.family: Style.font.family
            font.pixelSize: Style.font.caption
            font.bold: true
        }
        Text {
            width: parent.width
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
            color: Qt.alpha(root.foreground, 0.65)
            font.family: Style.font.family
            font.pixelSize: Style.font.caption
            text: dev.ufbt ? "ufbt available · update SDK for the connected firmware." : (dev.ufbtCommand ? "ufbt uses python3 -m ufbt. Install ufbt if the module is missing." : "ufbt unavailable · install before building.")
        }
        Flow {
            width: parent.width
            spacing: Style.space(6)
            ToolButton {
                text: "Install ufbt"
                foreground: root.foreground
                enabled: root.idle
                onTriggered: root.service.devInstallUfbt()
            }
            ToolButton {
                text: "Update SDK"
                foreground: root.foreground
                enabled: root.idle && root.projectApplied
                onTriggered: { root.applyProject(); root.service.devUpdateSdk() }
            }
        }
        InputField {
            id: appIdInput
            width: parent.width
            foreground: root.foreground
            font.family: "monospace"
            placeholderText: "New app ID, e.g. hello_app"
            text: "hello_app"
            enabled: !dev.busy
        }
        Text {
            width: parent.width
            text: "New app ID · lowercase letters, digits, underscores; start with a letter. Use an empty project folder."
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            color: Qt.alpha(root.foreground, 0.55)
            font.family: Style.font.family
            font.pixelSize: Style.font.caption
        }
        ToolButton {
            text: "Create template"
            foreground: root.foreground
            enabled: root.idle && root.projectApplied && root.appIdValid && !root.fam.appid
            onTriggered: { root.applyProject(); root.service.devCreate(appIdInput.text) }
        }
    }

    ToolButton {
        text: root.inspectorExpanded ? "Hide device inspector" : "Device inspector · read-only"
        foreground: root.foreground
        enabled: root.confirmKind === ""
        onTriggered: root.inspectorExpanded = !root.inspectorExpanded
    }
    Column {
        width: parent.width
        spacing: Style.space(6)
        visible: root.inspectorExpanded && root.confirmKind === ""
        Text {
            width: parent.width
            text: "Read-only queries. GPIO, reboot, factory reset, and DFU stay off."
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
            color: Qt.alpha(root.foreground, 0.55)
            font.family: Style.font.family
            font.pixelSize: Style.font.caption
        }
        Flow {
            width: parent.width
            spacing: Style.space(4)
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
                ToolButton {
                    required property var modelData
                    text: modelData.label
                    foreground: root.foreground
                    primary: root.inspectKind === modelData.kind
                    enabled: root.idle
                    onTriggered: {
                        root.inspectKind = modelData.kind
                        root.service.devInspect(modelData.kind, modelData.kind === "property" ? "firmware" : "")
                    }
                }
            }
        }
    }
}
