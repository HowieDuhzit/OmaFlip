import QtQuick
import qs.Commons

Column {
    id: root
    property var service: null
    property color foreground: Color.foreground
    spacing: Style.space(8)

    readonly property bool vertical: service && (service.frameOrientation === 2 || service.frameOrientation === 3)
    readonly property int pixel: 4
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
        width: (root.vertical ? 64 : 128) * root.pixel
        height: (root.vertical ? 128 : 64) * root.pixel
        renderStrategy: Canvas.Immediate
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.fillStyle = "#0c0c0c"
            ctx.fillRect(0, 0, width, height)
            const bytes = root.decodeB64(root.frameData)
            if (bytes.length < 1024) return
            const scale = root.pixel
            const w = 128, h = 64
            ctx.fillStyle = "#dcdcdc"
            let orient = root.service ? root.service.frameOrientation : 0
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
            border.color: Qt.alpha(root.foreground, 0.2)
            border.width: 1
            radius: 4
        }
        Text {
            anchors.centerIn: parent
            visible: root.frameData.length === 0
            text: "Waiting for screen…"
            color: Qt.alpha(root.foreground, 0.55)
            font.family: Style.font.family
            font.pixelSize: Style.font.caption
        }
        MouseArea {
            id: pointer
            anchors.fill: parent
            hoverEnabled: true
            preventStealing: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
            cursorShape: Qt.PointingHandCursor
            property real originX: 0
            property real originY: 0
            property bool dragged: false
            Accessible.role: Accessible.Button
            Accessible.name: "Flipper screen"
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

    Flow {
        width: parent.width
        spacing: Style.space(6)
        Repeater {
            model: [
                {label: "Up", button: "up"}, {label: "Down", button: "down"},
                {label: "Left", button: "left"}, {label: "Right", button: "right"},
                {label: "OK", button: "ok"}, {label: "Back", button: "back"}
            ]
            Rectangle {
                required property var modelData
                width: 72
                height: Style.space(28)
                radius: Style.cornerRadius
                color: mouse.pressed ? Qt.alpha(root.foreground, 0.2) : (mouse.containsMouse ? Qt.alpha(root.foreground, 0.12) : Qt.alpha(root.foreground, 0.05))
                border.width: 1
                border.color: Qt.alpha(root.foreground, 0.25)
                Text {
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
                    preventStealing: true
                    acceptedButtons: Qt.LeftButton
                    cursorShape: Qt.PointingHandCursor
                    onClicked: if (root.service) root.service.input(modelData.button)
                }
            }
        }
    }
    Text {
        width: parent.width
        wrapMode: Text.Wrap
        textFormat: Text.PlainText
        color: Qt.alpha(root.foreground, 0.65)
        font.family: Style.font.family
        font.pixelSize: Style.font.caption
        text: "Click OK · right-click Back · drag D-pad · wheel scroll"
    }
    Text {
        width: parent.width
        visible: root.service && root.service.selectedDevice && root.service.selectedDevice.screenshots && root.service.selectedDevice.screenshots.length
        text: {
            const shots = root.service && root.service.selectedDevice ? root.service.selectedDevice.screenshots : null
            return shots && shots.length ? "Last PNG: " + shots[0] : ""
        }
        textFormat: Text.PlainText
        wrapMode: Text.WrapAnywhere
        color: Qt.alpha(root.foreground, 0.65)
        font.family: Style.font.family
        font.pixelSize: Style.font.caption
    }
}
