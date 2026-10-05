import QtQuick
import qs.Commons

Rectangle {
    id: root
    property string text: ""
    property color foreground: Color.foreground
    property color accentColor: Color.accent
    property bool primary: false
    property bool destructive: false
    readonly property color tone: destructive ? Color.urgent : accentColor
    signal triggered()

    implicitWidth: label.implicitWidth + Style.space(24)
    implicitHeight: Style.space(34)
    radius: Style.cornerRadius
    activeFocusOnTab: true
    opacity: enabled ? 1 : 0.4
    color: primary ? Qt.alpha(tone, mouse.pressed ? 0.28 : 0.16)
        : Qt.alpha(foreground, mouse.pressed ? 0.14 : (mouse.containsMouse ? 0.09 : 0.04))
    border.width: 1
    border.color: activeFocus ? tone : Qt.alpha(primary || destructive ? tone : foreground, 0.25)
    Accessible.role: Accessible.Button
    Accessible.name: text
    Accessible.onPressAction: if (root.enabled) root.triggered()
    Keys.onReturnPressed: if (enabled) triggered()
    Keys.onEnterPressed: if (enabled) triggered()
    Keys.onSpacePressed: if (enabled) triggered()
    Text {
        id: label
        anchors.fill: parent
        anchors.leftMargin: Style.space(12)
        anchors.rightMargin: Style.space(12)
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        text: root.text
        textFormat: Text.PlainText
        color: root.primary || root.destructive ? root.tone : root.foreground
        font.family: Style.font.family
        font.pixelSize: Style.font.body
        font.bold: root.primary
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: root.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: { root.forceActiveFocus(); root.triggered() }
    }
}
