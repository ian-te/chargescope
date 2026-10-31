import QtQuick 2.6
import Sailfish.Silica 1.0
import "pages"

ApplicationWindow {
    id: app

    initialPage: Component { MainPage {} }
    cover: Qt.resolvedUrl("cover/CoverPage.qml")
    allowedOrientations: defaultAllowedOrientations

    // Polls sysfs only while the app is in front; the cover has a refresh action instead.
    Binding {
        target: chargescope
        property: "active"
        value: Qt.application.active
    }

    Notice {
        id: notice
        duration: 3000
    }

    function showNotice(text) {
        notice.text = text
        notice.show()
    }
}
