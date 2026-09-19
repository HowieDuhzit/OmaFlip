import QtQuick
import qs.Commons

Rectangle {
    id: root
    property string text: ""
    property bool selected: false
    property color foreground: Color.foreground
    property color accentColor: "#3B82F6"
    signal triggered()
    implicitHeight: Style.space(32)
    radius: 8
    color: selected ? Qt.alpha(root.accentColor, 0.15) : (mouse.containsMouse ? Qt.alpha(root.foreground, 0.08) : "transparent")
    border.width: selected ? 1 : 0
    border.color: selected ? Qt.alpha(root.accentColor, 0.4) : "transparent"
    Accessible.role: Accessible.Button
    Accessible.name: text
    Accessible.onPressAction: root.triggered()
    Behavior on color { ColorAnimation { duration: 80 } }
    Text {
        anchors.fill: parent
        leftPadding: Style.space(10)
        verticalAlignment: Text.AlignVCenter
        text: root.text
        textFormat: Text.PlainText
        color: selected ? root.accentColor : root.foreground
        font.family: Style.font.family
        font.pixelSize: Style.font.body
        elide: Text.ElideRight
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.triggered()
    }
}
