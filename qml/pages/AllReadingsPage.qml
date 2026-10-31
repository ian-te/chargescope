import QtQuick 2.6
import Sailfish.Silica 1.0

// Every attribute of every power supply and Type-C port, as the kernel gives it.
Page {
    id: page

    property var groups: chargescope.allReadings()

    allowedOrientations: Orientation.All

    Timer {
        interval: 2000
        repeat: true
        running: page.status === PageStatus.Active && Qt.application.active
        onTriggered: page.groups = chargescope.allReadings()
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        PullDownMenu {
            MenuItem {
                text: qsTr("Copy all")
                onClicked: {
                    Clipboard.text = chargescope.deviceReport()
                    app.showNotice(qsTr("Device report copied"))
                }
            }
        }

        Column {
            id: column
            width: parent.width

            PageHeader { title: qsTr("All readings") }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                bottomPadding: Theme.paddingMedium
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("Raw values, unconverted: voltages in µV, currents in µA, charge in µAh, temperatures in tenths of a degree.")
            }

            Repeater {
                model: page.groups.length

                Column {
                    id: groupColumn

                    readonly property var group: page.groups[index]

                    width: column.width

                    SectionHeader { text: groupColumn.group.title }

                    Label {
                        x: Theme.horizontalPageMargin
                        width: parent.width - 2 * x
                        bottomPadding: Theme.paddingSmall
                        horizontalAlignment: Text.AlignRight
                        truncationMode: TruncationMode.Fade
                        font.pixelSize: Theme.fontSizeTiny
                        color: Theme.secondaryColor
                        text: groupColumn.group.path
                    }

                    Repeater {
                        model: groupColumn.group.items.length

                        Item {
                            readonly property var reading: groupColumn.group.items[index]

                            width: column.width
                            height: Math.max(name.height, value.height) + Theme.paddingSmall

                            Label {
                                id: name
                                x: Theme.horizontalPageMargin
                                width: (parent.width - 2 * Theme.horizontalPageMargin) * 0.6 - Theme.paddingSmall
                                wrapMode: Text.WrapAnywhere
                                font.pixelSize: Theme.fontSizeSmall
                                color: Theme.secondaryColor
                                text: reading.name
                            }
                            Label {
                                id: value
                                anchors {
                                    right: parent.right
                                    rightMargin: Theme.horizontalPageMargin
                                }
                                width: (parent.width - 2 * Theme.horizontalPageMargin) * 0.4
                                horizontalAlignment: Text.AlignRight
                                wrapMode: Text.Wrap
                                font.pixelSize: Theme.fontSizeSmall
                                color: Theme.highlightColor
                                text: reading.value
                            }
                        }
                    }
                }
            }
        }

        VerticalScrollDecorator {}
    }
}
