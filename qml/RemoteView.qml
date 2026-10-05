pragma ComponentBehavior: Bound
import QtQuick
import qs.Commons

Column {
    id: root
    property var service: null
    property color foreground: Color.foreground
    property color accentColor: Color.accent
    spacing: Style.space(12)

    readonly property bool vertical: service && (service.frameOrientation === 2 || service.frameOrientation === 3)
    readonly property int screenColumns: vertical ? 64 : 128
    readonly property int screenRows: vertical ? 128 : 64
    // Whole LCD pixels whenever space permits; subpixel fit for very narrow panels.
    readonly property real availableScale: Math.max(0, Math.min(4, width / screenColumns, Style.space(384) / screenRows))
    readonly property real pixel: availableScale >= 1 ? Math.floor(availableScale) : availableScale
    readonly property string frameData: service ? (service.frameData || "") : ""

    function visualFromDelta(dx, dy) {
        if (Math.abs(dx) > Math.abs(dy)) return dx > 0 ? "right" : "left"
        return dy > 0 ? "down" : "up"
    }
    function sendVisual(dir) {
        if (root.service) root.service.input("visual-" + dir)
    }
    function decodeB64(text) {
        const table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
        const out = []
        let buf = 0, bits = 0
        for (let i = 0; i < text.length; i++) {
            const ch = text.charAt(i)
            if (ch === "=") break
            const v = table.indexOf(ch)
            if (v < 0) continue
            buf = (buf << 6) | v
            bits += 6
            if (bits >= 8) {
                bits -= 8
                out.push((buf >> bits) & 255)
            }
        }
        return out
    }

    Canvas {
        id: screen
        objectName: "remoteScreen"
        anchors.horizontalCenter: parent.horizontalCenter
        width: root.screenColumns * root.pixel
        height: root.screenRows * root.pixel
        renderStrategy: Canvas.Immediate
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        Component.onCompleted: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.fillStyle = "#0c0c0c"
            ctx.fillRect(0, 0, width, height)
            const bytes = root.decodeB64(root.frameData)
            if (!root.frameData || bytes.length < 1024) return
            const scale = root.pixel
            const w = 128, h = 64
            ctx.fillStyle = "#dcdcdc"
            const orient = root.service ? root.service.frameOrientation : 0
            for (let y = 0; y < h; y++) {
                for (let x = 0; x < w; x++) {
                    if ((bytes[(y >> 3) * w + x] & (1 << (y & 7))) === 0) continue
                    let dx = x, dy = y
                    if (orient === 1) { dx = w - 1 - x; dy = h - 1 - y }
                    else if (orient === 2) { dx = y; dy = w - 1 - x }
                    else if (orient === 3) { dx = h - 1 - y; dy = x }
                    ctx.fillRect(dx * scale, dy * scale, scale, scale)
                }
            }
        }
        Rectangle {
            anchors.fill: parent
            color: "transparent"
            border.color: Qt.alpha(root.foreground, 0.25)
            border.width: 1
        }
        Text {
            anchors.centerIn: parent
            width: Math.max(0, parent.width - Style.space(16))
            visible: root.frameData.length === 0
            text: "Waiting for screen…"
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            color: "#dcdcdc"
            font.family: Style.font.family
            font.pixelSize: Style.font.caption
        }
        MouseArea {
            id: pointer
            objectName: "screenGestures"
            anchors.fill: parent
            enabled: !!root.service
            hoverEnabled: true
            preventStealing: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
            cursorShape: Qt.PointingHandCursor
            property real originX: 0
            property real originY: 0
            property bool dragged: false
            Accessible.role: Accessible.Button
            Accessible.name: "Flipper screen: click OK, right-click Back, drag to navigate"
            onPressed: mouse => {
                originX = mouse.x
                originY = mouse.y
                dragged = false
            }
            onPositionChanged: mouse => {
                if (!pressed) return
                if (Math.hypot(mouse.x - originX, mouse.y - originY) > Math.max(16, root.pixel * 3))
                    dragged = true
            }
            onReleased: mouse => {
                if (!root.service) return
                if (dragged) {
                    root.sendVisual(root.visualFromDelta(mouse.x - originX, mouse.y - originY))
                    return
                }
                if (mouse.button === Qt.RightButton) root.service.input("back")
                else root.service.input("ok")
            }
            onCanceled: dragged = false
            onWheel: wheel => {
                const dx = wheel.angleDelta.x
                const dy = wheel.angleDelta.y
                if (dy !== 0 || dx !== 0) {
                    root.sendVisual(root.visualFromDelta(dx, -dy))
                    wheel.accepted = true
                }
            }
        }
    }
    Connections {
        target: root.service
        function onFrameDataChanged() { screen.requestPaint() }
        function onFrameOrientationChanged() { screen.requestPaint() }
    }

    // These are physical Flipper buttons, not orientation-relative gestures.
    Item {
        id: pad
        objectName: "directionPad"
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(root.width, Style.space(252))
        height: Style.space(114)
        readonly property real gap: Math.min(Style.space(6), width / 12)
        readonly property real cell: Math.max(0, (width - 2 * gap) / 3)
        Repeater {
            model: [
                {label: "↑", button: "up", col: 1, row: 0},
                {label: "←", button: "left", col: 0, row: 1},
                {label: "OK", button: "ok", col: 1, row: 1},
                {label: "→", button: "right", col: 2, row: 1},
                {label: "↓", button: "down", col: 1, row: 2}
            ]
            ToolButton {
                required property var modelData
                objectName: "remote-" + modelData.button
                x: modelData.col * (pad.cell + pad.gap)
                y: modelData.row * Style.space(40)
                width: pad.cell
                text: modelData.label
                foreground: root.foreground
                accentColor: root.accentColor
                primary: modelData.button === "ok"
                enabled: !!root.service
                Accessible.name: modelData.button === "ok" ? "OK" : "Physical " + modelData.button
                onTriggered: if (root.service) root.service.input(modelData.button)
            }
        }
    }
    ToolButton {
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(implicitWidth, root.width)
        objectName: "remote-back"
        text: "Back"
        foreground: root.foreground
        accentColor: root.accentColor
        enabled: !!root.service
        onTriggered: if (root.service) root.service.input("back")
    }
    Text {
        width: parent.width
        wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignHCenter
        textFormat: Text.PlainText
        color: Qt.alpha(root.foreground, 0.65)
        font.family: Style.font.family
        font.pixelSize: Style.font.caption
        text: "Click: OK · right-click: Back\nDrag or scroll the screen to navigate · arrows: device buttons"
    }
    Text {
        width: parent.width
        visible: !!(root.service && root.service.selectedDevice && root.service.selectedDevice.screenshots && root.service.selectedDevice.screenshots.length)
        text: {
            const shots = root.service && root.service.selectedDevice ? root.service.selectedDevice.screenshots : null
            return shots && shots.length ? "Last PNG: " + shots[0] : ""
        }
        textFormat: Text.PlainText
        wrapMode: Text.WrapAnywhere
        horizontalAlignment: Text.AlignHCenter
        color: Qt.alpha(root.foreground, 0.65)
        font.family: Style.font.family
        font.pixelSize: Style.font.caption
    }
}
