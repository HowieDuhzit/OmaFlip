import QtQuick
import qs.Commons

Row {
    id: root
    property string label: ""
    property var value: null
    property color foreground: Color.foreground
    spacing: Style.space(12)
    Text {
        width: Math.round(root.width * 0.38)
        text: root.label
        textFormat: Text.PlainText
        color: Qt.alpha(root.foreground, 0.65)
        font.family: Style.font.family
        font.pixelSize: Style.font.body
        wrapMode: Text.Wrap
    }
    Text {
        width: root.width - x
        text: {
            const value = root.value
            if (value === null || value === undefined || value === "") return "Unavailable"
            if (typeof value === "object") return value.summary || value.storage || "Unavailable"
            return String(value)
        }
        textFormat: Text.PlainText
        color: root.foreground
        font.family: Style.font.family
        font.pixelSize: Style.font.body
        wrapMode: Text.WrapAnywhere
    }
}
