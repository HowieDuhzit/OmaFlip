import QtQuick
import qs.Commons

Rectangle {
    id: root
    property string text: ""
    property bool selected: false
    property color foreground: Color.foreground
    signal triggered()
    implicitHeight: Style.space(32)
    radius: Style.cornerRadius
    color: selected || mouse.containsMouse ? Qt.alpha(foreground, 0.1) : "transparent"
    border.width: selected ? 1 : 0
    border.color: Qt.alpha(foreground, 0.5)
    Accessible.role: Accessible.Button
    Accessible.name: text
    Accessible.onPressAction: root.triggered()
    Behavior on color { ColorAnimation { duration: 80 } }
    Text {
        anchors.fill: parent
        leftPadding: Style.space(8)
        verticalAlignment: Text.AlignVCenter
        text: root.text
        textFormat: Text.PlainText
        color: root.foreground
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
