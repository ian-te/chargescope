import QtQuick 2.6
import Sailfish.Silica 1.0
import "../js/format.js" as Format

// Charging watts over the last ten minutes; gaps where the app wasn't polling.
Item {
    id: spark

    property var points: []
    property real peak: 0
    property color tint: Theme.highlightColor
    readonly property int span: 10 * 60 * 1000

    implicitHeight: Theme.itemSizeHuge
    onPointsChanged: canvas.requestPaint()
    onTintChanged: canvas.requestPaint()

    Canvas {
        id: canvas
        anchors {
            fill: parent
            topMargin: peakLabel.height
            bottomMargin: axis.height + Theme.paddingSmall
        }
        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            var pts = spark.points
            var w = width, h = height
            var line = Math.max(2, Math.round(Theme.pixelRatio * 2))

            ctx.strokeStyle = Theme.rgba(Theme.primaryColor, 0.15)
            ctx.lineWidth = Math.max(1, Theme.pixelRatio)
            ctx.beginPath()
            ctx.moveTo(0, h - 0.5)
            ctx.lineTo(w, h - 0.5)
            ctx.stroke()

            if (!pts || pts.length === 0)
                return
            var now = pts[pts.length - 1].t
            var top = Math.max(spark.peak, 1) * 1.1
            function x(p) { return (p.t - (now - spark.span)) / spark.span * w }
            function y(p) { return h - Math.min(p.w / top, 1) * (h - line) - line / 2 }

            // One run per stretch of samples no more than a few polls apart.
            var runs = [], run = []
            for (var i = 0; i < pts.length; ++i) {
                if (run.length > 0 && pts[i].t - run[run.length - 1].t > 6000) {
                    runs.push(run)
                    run = []
                }
                run.push(pts[i])
            }
            runs.push(run)

            for (var r = 0; r < runs.length; ++r) {
                var seg = runs[r]
                ctx.fillStyle = Theme.rgba(spark.tint, 0.18)
                ctx.beginPath()
                ctx.moveTo(x(seg[0]), h)
                for (var j = 0; j < seg.length; ++j)
                    ctx.lineTo(x(seg[j]), y(seg[j]))
                ctx.lineTo(x(seg[seg.length - 1]), h)
                ctx.closePath()
                ctx.fill()

                ctx.strokeStyle = spark.tint
                ctx.lineWidth = line
                ctx.lineJoin = "round"
                ctx.beginPath()
                ctx.moveTo(x(seg[0]), y(seg[0]))
                for (var k = 1; k < seg.length; ++k)
                    ctx.lineTo(x(seg[k]), y(seg[k]))
                if (seg.length === 1)
                    ctx.lineTo(x(seg[0]) + line, y(seg[0]))
                ctx.stroke()
            }
        }
    }

    Label {
        id: peakLabel
        anchors.right: parent.right
        text: qsTr("peak %1 W").arg(Format.watts(spark.peak))
        font.pixelSize: Theme.fontSizeExtraSmall
        color: Theme.secondaryColor
    }

    Item {
        id: axis
        anchors.bottom: parent.bottom
        width: parent.width
        height: start.height

        Label {
            id: start
            text: qsTr("10 min ago")
            font.pixelSize: Theme.fontSizeExtraSmall
            color: Theme.secondaryColor
        }
        Label {
            anchors.right: parent.right
            text: qsTr("now")
            font.pixelSize: Theme.fontSizeExtraSmall
            color: Theme.secondaryColor
        }
    }
}
