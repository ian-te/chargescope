import QtQuick 2.6
import Sailfish.Silica 1.0
import "../js/format.js" as Format

// A 270° arc from 0 to `maximum` watts with the reading in the middle.
Item {
    id: gauge

    property real value
    property real maximum: 10
    property color tint: Theme.highlightColor
    property string caption

    property real shown: Math.min(Math.abs(value), maximum)
    Behavior on shown { NumberAnimation { duration: 400; easing.type: Easing.OutCubic } }

    readonly property real thickness: Math.round(width * 0.065)

    onShownChanged: canvas.requestPaint()
    onTintChanged: canvas.requestPaint()
    onMaximumChanged: canvas.requestPaint()

    Canvas {
        id: canvas
        anchors.fill: parent
        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            var cx = width / 2, cy = height / 2
            var r = Math.min(width, height) / 2 - gauge.thickness / 2
            var start = 0.75 * Math.PI, sweep = 1.5 * Math.PI
            ctx.lineCap = "round"
            ctx.lineWidth = gauge.thickness

            ctx.strokeStyle = Theme.rgba(Theme.primaryColor, 0.12)
            ctx.beginPath()
            ctx.arc(cx, cy, r, start, start + sweep, false)
            ctx.stroke()

            if (gauge.shown > 0) {
                ctx.strokeStyle = gauge.tint
                ctx.beginPath()
                ctx.arc(cx, cy, r, start, start + sweep * Math.max(0.005, gauge.shown / gauge.maximum), false)
                ctx.stroke()
            }
        }
    }

    Column {
        anchors.centerIn: parent
        width: parent.width - 4 * gauge.thickness

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: Theme.paddingSmall

            Label {
                id: reading
                text: Format.watts(gauge.value)
                font.pixelSize: Theme.fontSizeHuge * 1.4
                font.family: Theme.fontFamilyHeading
                color: gauge.value > 0 ? Theme.primaryColor : Theme.secondaryColor
            }
            Label {
                anchors.baseline: reading.baseline
                text: "W"
                font.pixelSize: Theme.fontSizeLarge
                color: Theme.secondaryColor
            }
        }
        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            truncationMode: TruncationMode.Fade
            text: gauge.caption
            font.pixelSize: Theme.fontSizeSmall
            color: Theme.secondaryHighlightColor
        }
    }

    Label {
        anchors {
            horizontalCenter: parent.horizontalCenter
            bottom: parent.bottom
            bottomMargin: gauge.thickness
        }
        text: qsTr("of %1 W").arg(gauge.maximum)
        font.pixelSize: Theme.fontSizeExtraSmall
        color: Theme.secondaryColor
    }
}
