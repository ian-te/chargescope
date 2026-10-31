import QtQuick 2.6
import Sailfish.Silica 1.0

// A reading with its name and, in small print under it, the sysfs attribute it comes from; the
// value on the right, with an optional detail under it. Without a name the attribute stands in
// for it. Nothing is cut off: long names and values wrap.
Item {
    id: item

    property string label
    property string param
    property string value
    property string detail

    width: parent.width
    height: Math.max(Theme.itemSizeSmall, Math.max(labels.height, values.height) + 2 * Theme.paddingSmall)

    Column {
        id: labels
        anchors {
            left: parent.left
            leftMargin: Theme.horizontalPageMargin
            right: values.left
            rightMargin: Theme.paddingMedium
            verticalCenter: parent.verticalCenter
        }

        Label {
            width: parent.width
            text: item.label || item.param
            wrapMode: Text.Wrap
            color: Theme.primaryColor
        }
        Label {
            visible: item.label.length > 0 && item.param.length > 0
            width: parent.width
            text: item.param
            wrapMode: Text.WrapAnywhere
            font.pixelSize: Theme.fontSizeTiny
            color: Theme.secondaryColor
        }
    }

    Column {
        id: values
        anchors {
            right: parent.right
            rightMargin: Theme.horizontalPageMargin
            verticalCenter: parent.verticalCenter
        }
        width: Math.min(Math.max(valueLabel.implicitWidth, detailLabel.implicitWidth),
                        (item.width - 2 * Theme.horizontalPageMargin) * 0.45)

        Label {
            id: valueLabel
            width: parent.width
            horizontalAlignment: Text.AlignRight
            wrapMode: Text.Wrap
            text: item.value
            color: Theme.highlightColor
        }
        Label {
            id: detailLabel
            visible: item.detail.length > 0
            width: parent.width
            horizontalAlignment: Text.AlignRight
            wrapMode: Text.Wrap
            text: item.detail
            font.pixelSize: Theme.fontSizeTiny
            color: Theme.secondaryHighlightColor
        }
    }
}
