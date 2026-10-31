import QtQuick 2.6
import Sailfish.Silica 1.0
import "../components"
import "../js/format.js" as Format

CoverBackground {
    Column {
        anchors.centerIn: parent
        width: parent.width - 2 * Theme.paddingLarge
        spacing: Theme.paddingMedium

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: Theme.paddingSmall

            Label {
                id: reading
                text: (chargescope.watts < -0.005 ? "−" : "") + Format.watts(chargescope.watts)
                font.pixelSize: Theme.fontSizeHuge
                font.family: Theme.fontFamilyHeading
                color: chargescope.watts > 0 ? Theme.primaryColor : Theme.secondaryColor
            }
            Label {
                anchors.baseline: reading.baseline
                text: "W"
                font.pixelSize: Theme.fontSizeMedium
                color: Theme.secondaryColor
            }
        }

        ModeBadge {
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.min(implicitWidth, parent.width)
            mode: chargescope.mode
            fontSize: Theme.fontSizeExtraSmall
        }

        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: Theme.fontSizeExtraSmall
            color: Theme.secondaryColor
            truncationMode: TruncationMode.Fade
            // Polling stops in the background, so say how old the reading is.
            text: (chargescope.capacity >= 0 ? chargescope.capacity + " % · " : "")
                  + Format.time(chargescope.updated)
        }
    }

    CoverActionList {
        CoverAction {
            iconSource: "image://theme/icon-cover-refresh"
            onTriggered: chargescope.refresh()
        }
    }
}
