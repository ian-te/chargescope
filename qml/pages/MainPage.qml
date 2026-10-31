import QtQuick 2.6
import Sailfish.Silica 1.0
import "../components"
import "../js/format.js" as Format

Page {
    id: page

    readonly property var offers: chargescope.offers
    readonly property real maxOffer: {
        var m = 0
        for (var i = 0; i < offers.length; ++i)
            m = Math.max(m, offers[i].watts)
        return m
    }

    function chipState(online) {
        return online === 1 ? qsTr("Online") : online === 0 ? qsTr("Offline") : qsTr("Unknown")
    }

    function gaugeCaption() {
        if (chargescope.watts > 0.005)
            return qsTr("into the battery")
        if (chargescope.watts < -0.005)
            return qsTr("from the battery")
        return chargescope.plugged ? qsTr("idle") : qsTr("unplugged")
    }

    allowedOrientations: Orientation.All

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        PullDownMenu {
            MenuItem {
                text: qsTr("Copy report")
                onClicked: {
                    Clipboard.text = chargescope.report()
                    app.showNotice(qsTr("Report copied"))
                }
            }
        }

        Column {
            id: column
            width: parent.width

            PageHeader { title: "ChargeScope" }

            Gauge {
                anchors.horizontalCenter: parent.horizontalCenter
                width: Math.min(page.width, page.height) * 0.62
                height: width
                value: chargescope.watts
                maximum: Format.scale(Math.max(page.maxOffer, chargescope.historyPeak, Math.abs(chargescope.watts)))
                tint: chargescope.watts > 0 ? badge.tint : Theme.secondaryColor
                caption: gaugeCaption()
            }

            Item { width: 1; height: Theme.paddingMedium }

            ModeBadge {
                id: badge
                anchors.horizontalCenter: parent.horizontalCenter
                mode: chargescope.mode
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                topPadding: Theme.paddingMedium
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.secondaryColor
                text: {
                    var parts = []
                    if (chargescope.capacity >= 0)
                        parts.push(chargescope.capacity + " %")
                    if (chargescope.status)
                        parts.push(chargescope.status)
                    if (chargescope.pdRevision && chargescope.mode !== "basic" && chargescope.plugged)
                        parts.push(qsTr("PD %1").arg(chargescope.pdRevision))
                    return parts.join(" · ")
                }
            }

            SectionHeader { text: qsTr("Charger offers") }

            Repeater {
                model: page.offers
                OfferItem { offer: modelData }
            }

            Label {
                visible: page.offers.length === 0
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                color: Theme.secondaryHighlightColor
                text: !chargescope.plugged ? qsTr("Plug in a charger to see what it offers.")
                    : chargescope.mode === "basic" ? qsTr("No PD offers: this charger only does basic USB power.")
                    : qsTr("No offers found yet. They show up once the phone has talked to the charger.")
            }

            Label {
                visible: chargescope.logError.length > 0 || chargescope.offersSource.length > 0
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                topPadding: Theme.paddingSmall
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
                text: {
                    var source = chargescope.offersSource === "dmesg" ? qsTr("From the kernel log.")
                               : chargescope.offersSource === "typec" ? qsTr("From the USB Type-C port.")
                               : ""
                    if (chargescope.logError)
                        source = (source ? source + " " : "") + chargescope.logError + "."
                    return source
                }
            }

            SectionHeader { text: qsTr("Phone") }

            DetailItem { label: qsTr("VBUS"); value: Format.volts(chargescope.vbusMv) }
            DetailItem { label: qsTr("Input current limit"); value: Format.milliamps(chargescope.inputLimitMa) }
            DetailItem { label: qsTr("Battery current"); value: chargescope.batteryMa + " mA" }
            DetailItem { label: qsTr("Battery voltage"); value: Format.volts(chargescope.batteryMv) }
            DetailItem { label: qsTr("Battery temperature"); value: Format.celsius(chargescope.temperature) }
            DetailItem {
                label: qsTr("Charge level")
                value: chargescope.capacity >= 0 ? chargescope.capacity + " %" : "—"
            }
            DetailItem {
                label: qsTr("PD revision")
                value: chargescope.pdRevision || "—"
            }
            DetailItem {
                visible: chargescope.usbType.length > 0
                label: qsTr("USB type")
                value: chargescope.usbType
            }

            SectionHeader {
                visible: chargescope.chips.length > 0
                text: qsTr("Charger chips")
            }

            Repeater {
                model: chargescope.chips
                DetailItem {
                    label: modelData.name
                    value: chipState(modelData.online)
                    // DetailItem has no colour property for the value, so dim the whole row instead.
                    opacity: modelData.online === 1 ? 1.0 : Theme.opacityHigh
                }
            }

            SectionHeader { text: qsTr("Last 10 minutes") }

            Sparkline {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                height: Theme.itemSizeHuge * 1.3
                points: chargescope.history
                peak: chargescope.historyPeak
                tint: badge.mode !== "none" ? badge.tint : Theme.highlightColor
            }
        }

        VerticalScrollDecorator {}
    }
}
