.pragma library

function watts(w) {
    var a = Math.abs(w)
    return a < 10 ? a.toFixed(2) : a.toFixed(1)
}

function volts(mv) {
    return mv < 0 ? "—" : (mv / 1000).toFixed(2) + " V"
}

function amps(ma) {
    return (ma / 1000).toFixed(2) + " A"
}

function milliamps(ma) {
    return ma < 0 ? "—" : ma + " mA"
}

function celsius(t) {
    return isNaN(t) ? "—" : t.toFixed(1) + " °C"
}

function modeName(mode) {
    switch (mode) {
    case "pps": return qsTr("Direct charging (PPS)")
    case "pd": return qsTr("USB PD")
    case "basic": return qsTr("Basic USB")
    case "wireless": return qsTr("Wireless")
    default: return qsTr("Not charging")
    }
}

// The gauge's full scale: a round number above everything worth showing.
function scale(w) {
    var steps = [10, 15, 20, 30, 45, 60, 100, 150, 240]
    for (var i = 0; i < steps.length; ++i) {
        if (w <= steps[i])
            return steps[i]
    }
    return Math.ceil(w / 50) * 50
}

function time(date) {
    return Qt.formatTime(date, "hh:mm:ss")
}
