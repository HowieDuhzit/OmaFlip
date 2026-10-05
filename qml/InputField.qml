import QtQuick
import qs.Commons

TextInput {
    id: root
    property string placeholderText: ""
    property color foreground: Color.foreground
    height: Math.max(Style.space(36), implicitHeight)
    leftPadding: Style.space(10)
    rightPadding: Style.space(10)
    topPadding: Style.space(8)
    bottomPadding: Style.space(8)
    color: foreground
    font.family: Style.font.family
    font.pixelSize: Style.font.body
    selectionColor: Qt.alpha(Color.accent, 0.35)
    selectedTextColor: foreground
    clip: true
    selectByMouse: true
    activeFocusOnTab: true
    Accessible.role: Accessible.EditableText
    Accessible.name: placeholderText
    Keys.onEscapePressed: event => { focus = false; event.accepted = false }
    Rectangle {
        anchors.fill: parent
        z: -1
        radius: Style.cornerRadius
        color: Qt.alpha(root.foreground, 0.04)
        border.width: 1
        border.color: Qt.alpha(root.activeFocus ? Color.accent : root.foreground, root.activeFocus ? 0.8 : 0.2)
    }
    Text {
        anchors.fill: parent
        leftPadding: root.leftPadding
        rightPadding: root.rightPadding
        verticalAlignment: Text.AlignVCenter
        visible: root.text.length === 0
        text: root.placeholderText
        textFormat: Text.PlainText
        elide: Text.ElideRight
        color: Qt.alpha(root.foreground, 0.55)
        font: root.font
    }
}
