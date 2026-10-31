import QtQuick 2.6
import Sailfish.Silica 1.0
import "../js/format.js" as Format

// The charging mode as a coloured pill. `tint` is shared with the gauge.
Rectangle {
    id: badge

    property string mode: "none"
    property int fontSize: Theme.fontSizeSmall
    readonly property bool light: Theme.colorScheme === Theme.DarkOnLight
    readonly property color tint: {
        switch (mode) {
        case "pps": return light ? "#16803f" : "#62e295"
        case "pd": return Theme.highlightColor
        case "basic": return light ? "#9a5800" : "#f4b24c"
        default: return Theme.secondaryColor
        }
    }

    implicitWidth: label.implicitWidth + 2 * Theme.paddingLarge
    implicitHeight: label.implicitHeight + Theme.paddingSmall * 2
    radius: height / 2
    color: Theme.rgba(tint, 0.16)
    border.width: Math.max(1, Theme.pixelRatio)
    border.color: Theme.rgba(tint, 0.6)

    Label {
        id: label
        anchors.centerIn: parent
        width: Math.min(implicitWidth, badge.width - 2 * Theme.paddingMedium)
        truncationMode: TruncationMode.Fade
        text: Format.modeName(badge.mode)
        color: badge.tint
        font.pixelSize: badge.fontSize
        font.bold: badge.mode !== "none"
    }
}
