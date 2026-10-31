import QtQuick 2.6
import Sailfish.Silica 1.0
import "../js/format.js" as Format

// One source PDO: "#2  Fixed 9.00 V    2.22 A · 20.0 W", highlighted while in use.
Item {
    id: item

    property var offer

    readonly property bool inUse: offer.inUse
    readonly property color textColor: inUse ? Theme.highlightColor : Theme.primaryColor

    width: parent.width
    height: Math.max(Theme.itemSizeSmall, content.height + 2 * Theme.paddingSmall)

    Rectangle {
        anchors.fill: parent
        visible: item.inUse
        color: Theme.rgba(Theme.highlightBackgroundColor, Theme.highlightBackgroundOpacity)
    }

    Column {
        id: content
        x: Theme.horizontalPageMargin
        width: parent.width - 2 * x
        anchors.verticalCenter: parent.verticalCenter

        Item {
            width: parent.width
            height: kind.height

            Label {
                id: number
                width: Theme.itemSizeExtraSmall * 0.7
                text: "#" + item.offer.index
                color: Theme.secondaryColor
                font.pixelSize: Theme.fontSizeSmall
                anchors.baseline: kind.baseline
            }
            Label {
                id: kind
                anchors.left: number.right
                anchors.right: power.left
                anchors.rightMargin: Theme.paddingMedium
                truncationMode: TruncationMode.Fade
                text: item.offer.pps
                      ? qsTr("PPS %1–%2").arg(Format.volts(item.offer.minMv).replace(" V", "")).arg(Format.volts(item.offer.maxMv))
                      : qsTr("Fixed %1").arg(Format.volts(item.offer.maxMv))
                color: item.textColor
            }
            Label {
                id: power
                anchors.right: parent.right
                anchors.baseline: kind.baseline
                text: Format.amps(item.offer.ma) + " · " + item.offer.watts.toFixed(1) + " W"
                color: item.inUse ? Theme.highlightColor : Theme.secondaryColor
                font.pixelSize: Theme.fontSizeSmall
            }
        }
        Label {
            visible: item.inUse
            x: number.width
            text: item.offer.pps ? qsTr("In use, %1 now").arg(Format.volts(chargescope.vbusMv)) : qsTr("In use")
            font.pixelSize: Theme.fontSizeExtraSmall
            color: Theme.secondaryHighlightColor
        }
    }
}
