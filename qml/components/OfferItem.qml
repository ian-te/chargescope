import QtQuick 2.6
import Sailfish.Silica 1.0

// One source PDO: the voltage large with the power on the right, and "Fixed, up to 3.00 A"
// under it. Highlighted while in use.
Item {
    id: item

    property var offer

    readonly property bool inUse: offer.inUse

    // 9000 → "9", 3300 → "3.3"
    function compactVolts(mv) {
        return String(Math.round(mv / 100) / 10)
    }

    width: parent.width
    height: Math.max(Theme.itemSizeSmall, content.height + 2 * Theme.paddingSmall)

    Rectangle {
        anchors.fill: parent
        visible: item.inUse
        color: Theme.rgba(Theme.highlightBackgroundColor, Theme.highlightBackgroundOpacity)
    }

    Label {
        id: number
        x: Theme.horizontalPageMargin
        width: Theme.itemSizeExtraSmall * 0.7
        // On the voltage's baseline, which is inside the column, so it can't be anchored to.
        y: content.y + voltage.baselineOffset - baselineOffset
        text: "#" + item.offer.index
        color: Theme.secondaryColor
        font.pixelSize: Theme.fontSizeSmall
    }

    Column {
        id: content
        anchors {
            left: number.right
            right: power.left
            rightMargin: Theme.paddingMedium
            verticalCenter: parent.verticalCenter
        }

        Label {
            id: voltage
            width: parent.width
            wrapMode: Text.Wrap
            text: item.offer.pps
                  ? qsTr("%1–%2 V").arg(compactVolts(item.offer.minMv)).arg(compactVolts(item.offer.maxMv))
                  : qsTr("%1 V").arg(compactVolts(item.offer.maxMv))
            color: item.inUse ? Theme.highlightColor : Theme.primaryColor
        }
        Label {
            width: parent.width
            wrapMode: Text.Wrap
            text: {
                var amps = (item.offer.ma / 1000).toFixed(2)
                var kind = item.offer.pps ? qsTr("PPS, up to %1 A").arg(amps) : qsTr("Fixed, up to %1 A").arg(amps)
                if (!item.inUse)
                    return kind
                return item.offer.pps ? qsTr("%1 · in use, %2 now").arg(kind).arg((chargescope.vbusMv / 1000).toFixed(2) + " V")
                                      : qsTr("%1 · in use").arg(kind)
            }
            font.pixelSize: Theme.fontSizeExtraSmall
            color: item.inUse ? Theme.secondaryHighlightColor : Theme.secondaryColor
        }
    }

    Label {
        id: power
        anchors {
            right: parent.right
            rightMargin: Theme.horizontalPageMargin
        }
        y: content.y + voltage.baselineOffset - baselineOffset
        // 19.98 → "20", 22.5 → "22.5"
        text: String(Math.round(item.offer.watts * 10) / 10) + " W"
        color: item.inUse ? Theme.highlightColor : Theme.secondaryColor
    }
}
