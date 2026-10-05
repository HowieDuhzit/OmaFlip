import QtQuick
import qs.Commons

Rectangle {
    id: root
    property string text: ""
    property bool selected: false
    property color foreground: Color.foreground
    property color accentColor: Color.accent
    signal triggered()
    property bool keyboardSelected: false
    implicitHeight: Style.space(38)
    activeFocusOnTab: true
    opacity: enabled ? 1 : 0.4
    Keys.onReturnPressed: if (enabled) triggered()
    Keys.onEnterPressed: if (enabled) triggered()
    Keys.onSpacePressed: if (enabled) triggered()
    radius: Style.cornerRadius
    color: selected ? Qt.alpha(root.accentColor, 0.15) : (mouse.containsMouse ? Qt.alpha(root.foreground, 0.08) : "transparent")
    border.width: selected || keyboardSelected || activeFocus ? 1 : 0
    border.color: keyboardSelected || activeFocus ? root.accentColor : Qt.alpha(root.accentColor, 0.4)
    Accessible.role: Accessible.Button
    Accessible.name: text
    Accessible.onPressAction: if (root.enabled) root.triggered()
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
