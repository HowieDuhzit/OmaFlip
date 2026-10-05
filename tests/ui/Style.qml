pragma Singleton
import QtQuick
// Deterministic theme only for Qt6 qmltestrunner, not the hosted smoke.
QtObject {
    property int cornerRadius: 6
    property var font: ({family: "sans-serif", body: 12, caption: 11, subtitle: 16})
    function space(value) { return value; }
}
