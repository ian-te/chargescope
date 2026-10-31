#include "chargemonitor.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <QSysInfo>
#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace {

const qint64 HistoryMs = 10 * 60 * 1000;
const int PollMs = 2000;
// After a plug-in the PD contract takes a moment, so look at the log again a few times.
const int RescanMs[] = {2000, 5000, 15000};
const int Rescans = sizeof(RescanMs) / sizeof(RescanMs[0]);
// While plugged in with no offers found, look again this often: the driver may log them later.
const qint64 RetryMs = 10000;

// The supplies VBUS and the input limit are read from, best first: MediaTek's charger framework,
// then the charger IC's own supply (primary_chg on the MT6720), then the generic names.
const QStringList InputSupplies = {QStringLiteral("mtk-master-charger"), QStringLiteral("primary_chg"),
                                   QStringLiteral("mtk_charger_type"), QStringLiteral("usb"), QStringLiteral("ac")};

QString readText(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    return QString::fromUtf8(file.readAll()).trimmed();
}

bool readInt(const QString &path, qint64 *value)
{
    bool ok = false;
    const qint64 v = readText(path).toLongLong(&ok);
    if (ok)
        *value = v;
    return ok;
}

// "5000mV" → 5000
int leadingInt(const QString &text)
{
    static const QRegularExpression re(QStringLiteral("^\\s*(\\d+)"));
    const QRegularExpressionMatch m = re.match(text);
    return m.hasMatch() ? m.captured(1).toInt() : 0;
}

// The first of InputSupplies, then any other online supply, that reports `attr` above zero.
// `source` gets "<supply>/<attr>", or just the attribute when none does.
qint64 readInput(const QString &powerSupply, const QString &attr, const QStringList &online, QString *source)
{
    QStringList order = InputSupplies;
    for (const QString &name : online) {
        if (!order.contains(name))
            order.append(name);
    }
    for (const QString &name : order) {
        qint64 v = 0;
        if (readInt(powerSupply + QLatin1Char('/') + name + QLatin1Char('/') + attr, &v) && v > 0) {
            *source = name + QLatin1Char('/') + attr;
            return v;
        }
    }
    *source = attr;
    return -1;
}

// The selected entry of a list like "Unknown SDP [DCP] CDP" or "source [sink]".
QString bracketed(const QString &text)
{
    const int open = text.indexOf(QLatin1Char('['));
    const int close = text.indexOf(QLatin1Char(']'), open);
    if (open < 0 || close < open)
        return text;
    return text.mid(open + 1, close - open - 1);
}

// A charge pump (divider) supply: mtk-mst-div-chg, mtk-slv-hvdiv-chg, primary_dvchg, ...
bool isPump(const QString &name)
{
    return name.contains(QLatin1String("div")) || name.contains(QLatin1String("dvchg"));
}

// The chip a supply belongs to, e.g. "MT6720": its model_name, else its device-tree
// compatible string, else its own name. `dir` is the supply's folder, ending in '/'.
QString chipModel(const QString &dir, const QString &name)
{
    const QString model = readText(dir + QStringLiteral("model_name"));
    if (!model.isEmpty())
        return model;
    static const QRegularExpression re(QStringLiteral("\\bmt(\\d{4})"), QRegularExpression::CaseInsensitiveOption);
    const QStringList uevent = readText(dir + QStringLiteral("uevent")).split(QLatin1Char('\n'));
    for (const QString &line : uevent) {
        if (line.startsWith(QLatin1String("OF_COMPATIBLE_0="))) {
            const QRegularExpressionMatch m = re.match(line.mid(16).section(QLatin1Char(','), -1));
            if (m.hasMatch())
                return QStringLiteral("MT") + m.captured(1);
        }
    }
    const QRegularExpressionMatch m = re.match(name);
    return m.hasMatch() ? QStringLiteral("MT") + m.captured(1) : QString();
}

// What a supply is, from its name, with the chip when known: "Main charge pump", "Charger IC · MT6720".
QString chipName(const QString &name, const QString &model)
{
    const QString id = name.toLower();
    const bool main = id.contains(QLatin1String("master")) || id.contains(QLatin1String("mst"));
    const bool second = id.contains(QLatin1String("slave")) || id.contains(QLatin1String("slv"));
    QString kind;
    if (id.contains(QLatin1String("gauge")))
        kind = ChargeMonitor::tr("Fuel gauge");
    else if (id.contains(QLatin1String("charger_type")))
        kind = ChargeMonitor::tr("Charger type detection");
    else if (id.contains(QLatin1String("hvdiv")))
        kind = main ? ChargeMonitor::tr("Main high-voltage charge pump")
             : second ? ChargeMonitor::tr("Second high-voltage charge pump")
             : ChargeMonitor::tr("High-voltage charge pump");
    else if (isPump(id))
        kind = main ? ChargeMonitor::tr("Main charge pump")
             : second ? ChargeMonitor::tr("Second charge pump")
             : model.isEmpty() ? ChargeMonitor::tr("Charge pump") : ChargeMonitor::tr("Charge pump IC");
    else if (id.contains(QLatin1String("chg")) || id.contains(QLatin1String("charger")))
        kind = main ? ChargeMonitor::tr("Main charger")
             : second ? ChargeMonitor::tr("Second charger")
             : model.isEmpty() ? ChargeMonitor::tr("Charger") : ChargeMonitor::tr("Charger IC");
    else
        return model;
    return model.isEmpty() ? kind : kind + QStringLiteral(" · ") + model;
}

// sysfs units: µA, µV, µAh, 0.1 °C, seconds.
QString formatMa(qint64 ua) { return QStringLiteral("%1 mA").arg(ua / 1000); }
QString formatV(qint64 uv) { return QString::number(uv / 1e6, 'f', 2) + QStringLiteral(" V"); }
QString formatMah(qint64 uah) { return QStringLiteral("%1 mAh").arg(uah / 1000); }
QString formatC(qint64 deci) { return QString::number(deci / 10.0, 'f', 1) + QStringLiteral(" °C"); }
QString formatCount(qint64 n) { return QString::number(n); }
QString formatDuration(qint64 s)
{
    const qint64 h = s / 3600;
    const qint64 m = (s % 3600) / 60;
    return h > 0 ? QStringLiteral("%1 h %2 min").arg(h).arg(m) : QStringLiteral("%1 min").arg(m);
}

QString volts(int mv)
{
    return QString::number(mv / 1000.0, 'f', 2) + QStringLiteral(" V");
}

QString amps(int ma)
{
    return QString::number(ma / 1000.0, 'f', 2) + QStringLiteral(" A");
}

}

ChargeMonitor::ChargeMonitor(const QString &sysfsRoot, const QString &logFile, QObject *parent)
    : QObject(parent)
    , m_powerSupply(sysfsRoot + QStringLiteral("/class/power_supply"))
    , m_typec(sysfsRoot + QStringLiteral("/class/typec"))
    , m_logFile(logFile)
    , m_temperature(std::numeric_limits<double>::quiet_NaN())
{
    m_poll.setInterval(PollMs);
    connect(&m_poll, &QTimer::timeout, this, &ChargeMonitor::sample);

    m_rescan.setSingleShot(true);
    connect(&m_rescan, &QTimer::timeout, this, [this]() {
        scanOffers();
        sample();
        if (--m_rescansLeft > 0)
            m_rescan.start(RescanMs[Rescans - m_rescansLeft]);
    });

    sample();
}

QString ChargeMonitor::attrPath(const QString &supply, const QString &attr) const
{
    return m_powerSupply + QLatin1Char('/') + supply + QLatin1Char('/') + attr;
}

void ChargeMonitor::setActive(bool active)
{
    if (active == m_active)
        return;
    m_active = active;
    if (active) {
        sample();
        m_poll.start();
    } else {
        m_poll.stop();
    }
    emit activeChanged();
}

void ChargeMonitor::refresh()
{
    sample();
}

void ChargeMonitor::sample()
{
    QString battery;
    QString usbTypeSource;
    QStringList supplies;
    QStringList online;
    bool plugged = false;
    bool divider = false;
    bool wireless = false;
    m_usbType.clear();

    const QStringList names = QDir(m_powerSupply).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &name : names) {
        const QString type = readText(attrPath(name, QStringLiteral("type")));
        if (type == QLatin1String("Battery")) {
            if (battery.isEmpty() || name == QLatin1String("battery"))
                battery = name;
            continue;
        }
        supplies.append(name);
        // 1 is online, 2 online with a programmable (adjustable) input, as primary_chg on PD.
        qint64 state = -1;
        readInt(attrPath(name, QStringLiteral("online")), &state);
        if (state >= 1) {
            plugged = true;
            online.append(name);
            if (isPump(name))
                divider = true;
            if (type == QLatin1String("Wireless"))
                wireless = true;
        }
        // "Unknown SDP DCP CDP [PD] PD_PPS": the one in brackets is what's connected.
        const QString usbType = readText(attrPath(name, QStringLiteral("usb_type")));
        if (usbType.contains(QLatin1Char('[')) && m_usbType.isEmpty() && state >= 1) {
            const QString selected = bracketed(usbType);
            if (selected != QLatin1String("Unknown")) {
                m_usbType = selected;
                usbTypeSource = name + QStringLiteral("/usb_type");
            }
        }
    }

    QString vbusSource = QStringLiteral("voltage_now");
    QString limitSource = QStringLiteral("input_current_limit");
    const qint64 vbus = plugged ? readInput(m_powerSupply, vbusSource, online, &vbusSource) : -1;
    // Microvolts as the ABI says, but a few drivers give millivolts.
    m_vbusMv = vbus < 0 ? -1 : int(vbus >= 100000 ? vbus / 1000 : vbus);
    const qint64 limit = readInput(m_powerSupply, limitSource, online, &limitSource);
    m_inputLimitMa = limit < 0 ? -1 : int(limit / 1000);

    m_status = battery.isEmpty() ? QString() : readText(attrPath(battery, QStringLiteral("status")));
    qint64 v = 0;
    m_batteryMv = readInt(attrPath(battery, QStringLiteral("voltage_now")), &v) ? int(v >= 100000 ? v / 1000 : v) : -1;
    m_capacity = readInt(attrPath(battery, QStringLiteral("capacity")), &v) ? int(v) : -1;
    m_temperature = readInt(attrPath(battery, QStringLiteral("temp")), &v)
            ? v / 10.0 : std::numeric_limits<double>::quiet_NaN();
    if (readInt(attrPath(battery, QStringLiteral("current_now")), &v)) {
        // Drivers disagree on the sign, so learn it from the status while the current is clear.
        if (std::abs(v) > 100000) {
            if (m_status == QLatin1String("Charging"))
                m_currentSign = v > 0 ? 1 : -1;
            else if (m_status == QLatin1String("Discharging"))
                m_currentSign = v < 0 ? 1 : -1;
        }
        m_batteryMa = int(v * (m_currentSign ? m_currentSign : 1) / 1000);
    } else {
        m_batteryMa = 0;
    }
    m_watts = m_batteryMv > 0 ? m_batteryMa * double(m_batteryMv) / 1e6 : 0;

    // port0 is the phone's own port; port0-partner exists while something is plugged in.
    QString port;
    QString partner;
    QString partnerRevision;
    const QStringList ports = QDir(m_typec).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &entry : ports) {
        if (port.isEmpty() && !entry.contains(QLatin1Char('-')))
            port = entry;
    }
    if (!port.isEmpty() && ports.contains(port + QStringLiteral("-partner"))) {
        partner = port + QStringLiteral("-partner");
        partnerRevision = readText(m_typec + QLatin1Char('/') + partner + QStringLiteral("/usb_power_delivery_revision"));
        if (partnerRevision == QLatin1String("0.0"))
            partnerRevision.clear();
    }
    m_pdRevision = partnerRevision;

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (m_first || plugged != m_plugged) {
        const bool plugIn = !m_first && plugged;
        m_first = false;
        m_plugged = plugged;
        scanOffers();
        if (plugIn) {
            m_rescansLeft = Rescans;
            m_rescan.start(RescanMs[0]);
        }
    } else if (m_plugged && m_pdos.isEmpty() && !m_rescan.isActive() && nowMs - m_lastScan >= RetryMs) {
        scanOffers();
    }

    m_direct = divider || m_usbType == QLatin1String("PD_PPS");
    const int inUse = inUseIndex();
    if (!m_plugged)
        m_mode = QStringLiteral("none");
    else if (wireless)
        m_mode = QStringLiteral("wireless");
    else if (m_direct || (inUse >= 0 && m_pdos.at(inUse).pps))
        m_mode = QStringLiteral("pps");
    else if (m_usbType.startsWith(QLatin1String("PD")) || !partnerRevision.isEmpty() || !m_pdos.isEmpty())
        m_mode = QStringLiteral("pd");
    else
        m_mode = QStringLiteral("basic");

    buildSections(supplies, battery, vbusSource, limitSource, usbTypeSource, port, partner);

    m_updated = QDateTime::currentDateTime();
    const qint64 now = m_updated.toMSecsSinceEpoch();
    m_history.append(qMakePair(now, std::max(0.0, m_watts)));
    while (!m_history.isEmpty() && m_history.first().first < now - HistoryMs)
        m_history.removeFirst();

    emit sampled();
    emit historyChanged();
}

void ChargeMonitor::buildSections(const QStringList &supplies, const QString &battery, const QString &vbusSource,
                                  const QString &limitSource, const QString &usbTypeSource, const QString &port,
                                  const QString &partner)
{
    m_sections.clear();
    QVariantList items;
    // `detail` goes in small print under the value.
    const auto addDetailed = [&items](const QString &label, const QString &param, const QString &value,
                                      const QString &detail, bool dim) {
        QVariantMap item;
        item.insert(QStringLiteral("label"), label);
        item.insert(QStringLiteral("param"), param);
        item.insert(QStringLiteral("value"), value);
        item.insert(QStringLiteral("detail"), detail);
        item.insert(QStringLiteral("dim"), dim);
        items.append(item);
    };
    const auto add = [&addDetailed](const QString &label, const QString &param, const QString &value, bool dim) {
        addDetailed(label, param, value, QString(), dim);
    };
    const auto section = [this, &items](const QString &title) {
        if (!items.isEmpty()) {
            QVariantMap map;
            map.insert(QStringLiteral("title"), title);
            map.insert(QStringLiteral("items"), items);
            m_sections.append(map);
        }
        items.clear();
    };
    // A number from a supply, shown only where the supply has it.
    const auto number = [this, &add](const QString &label, const QString &supply, const char *attr,
                               QString (*format)(qint64)) {
        qint64 value = 0;
        if (!supply.isEmpty() && readInt(attrPath(supply, QLatin1String(attr)), &value))
            add(label, supply + QLatin1Char('/') + QLatin1String(attr), format(value), false);
    };
    const auto text = [this, &add](const QString &label, const QString &supply, const char *attr) {
        const QString value = supply.isEmpty() ? QString() : readText(attrPath(supply, QLatin1String(attr)));
        if (!value.isEmpty())
            add(label, supply + QLatin1Char('/') + QLatin1String(attr), value, false);
    };
    // A setting with its ceiling under it: "3000 mA", "max 3150 mA".
    const auto withMax = [this, &addDetailed](const QString &label, const QString &supply, const char *attr, const char *maxAttr,
                                QString (*format)(qint64)) {
        qint64 value = 0;
        qint64 max = 0;
        if (supply.isEmpty() || !readInt(attrPath(supply, QLatin1String(attr)), &value))
            return;
        QString detail;
        if (readInt(attrPath(supply, QLatin1String(maxAttr)), &max) && max > 0)
            detail = tr("max %1").arg(format(max));
        addDetailed(label, supply + QLatin1Char('/') + QLatin1String(attr), format(value), detail, false);
    };

    // The charger IC is the supply with charge settings; the charge pump the divider with a current reading.
    QString charger;
    QString pump;
    for (const QString &name : supplies) {
        if (charger.isEmpty() && QFile::exists(attrPath(name, QStringLiteral("constant_charge_current"))))
            charger = name;
        if (pump.isEmpty() && isPump(name) && QFile::exists(attrPath(name, QStringLiteral("current_now"))))
            pump = name;
    }

    add(tr("VBUS"), vbusSource, m_vbusMv >= 0 ? volts(m_vbusMv) : QStringLiteral("—"), false);
    // The MT6720's charge pump measures the input bus whether or not it's pumping: its
    // voltage_now is VBUS, and its current_now the current drawn from the charger.
    qint64 ibus = 0;
    if (m_plugged && !pump.isEmpty() && readInt(attrPath(pump, QStringLiteral("current_now")), &ibus) && ibus > 0) {
        add(tr("Input current"), pump + QStringLiteral("/current_now"), formatMa(ibus), false);
        if (m_vbusMv > 0) {
            const double inputWatts = m_vbusMv * (ibus / 1000.0) / 1e6;
            QString detail;
            if (m_watts > 0)
                detail = tr("%1 % reaches the battery").arg(qRound(100 * m_watts / inputWatts));
            addDetailed(tr("Input power"), vbusSource + QStringLiteral(" × ") + pump + QStringLiteral("/current_now"),
                        QString::number(inputWatts, 'f', 1) + QStringLiteral(" W"), detail, false);
        }
    }
    add(tr("Input current limit"), limitSource,
        m_inputLimitMa >= 0 ? QStringLiteral("%1 mA").arg(m_inputLimitMa) : QStringLiteral("—"), false);
    number(tr("Input voltage limit"), charger, "input_voltage_limit", formatV);
    withMax(tr("Charge current"), charger, "constant_charge_current", "constant_charge_current_max", formatMa);
    withMax(tr("Charge voltage"), charger, "constant_charge_voltage", "constant_charge_voltage_max", formatV);
    number(tr("Pre-charge current"), charger, "precharge_current", formatMa);
    number(tr("Termination current"), charger, "charge_term_current", formatMa);
    text(tr("Charger status"), charger, "status");
    for (const QString &name : {QStringLiteral("mtk-master-charger"), charger}) {
        qint64 temp = 0;
        if (!name.isEmpty() && readInt(attrPath(name, QStringLiteral("temp")), &temp) && temp > 0) {
            add(tr("Charger temperature"), name + QStringLiteral("/temp"), formatC(temp), false);
            break;
        }
    }
    section(tr("Charging"));

    text(tr("Status"), pump, "status");
    number(tr("Temperature"), pump, "temp", formatC);
    number(tr("Voltage limit"), pump, "voltage_max", formatV);
    number(tr("Current limit"), pump, "current_max", formatMa);
    section(tr("Charge pump"));

    if (!battery.isEmpty()) {
        const int sign = m_currentSign ? m_currentSign : 1;
        add(tr("Current"), battery + QStringLiteral("/current_now"), QStringLiteral("%1 mA").arg(m_batteryMa), false);
        qint64 avg = 0;
        if (readInt(attrPath(battery, QStringLiteral("current_avg")), &avg))
            add(tr("Average current"), battery + QStringLiteral("/current_avg"), formatMa(avg * sign), false);
        number(tr("Voltage"), battery, "voltage_now", formatV);
        number(tr("Temperature"), battery, "temp", formatC);
        number(tr("Charge level"), battery, "capacity", [](qint64 n) { return QStringLiteral("%1 %").arg(n); });
        text(tr("Status"), battery, "status");
        text(tr("Health"), battery, "health");
        qint64 full = 0;
        qint64 design = 0;
        if (readInt(attrPath(battery, QStringLiteral("charge_full")), &full)) {
            QString detail;
            if (readInt(attrPath(battery, QStringLiteral("charge_full_design")), &design) && design > 0)
                detail = tr("%1 % of %2 new").arg(qRound(100.0 * full / design)).arg(formatMah(design));
            addDetailed(tr("Capacity"), battery + QStringLiteral("/charge_full"), formatMah(full), detail, false);
        }
        number(tr("Charge remaining"), battery, "charge_counter", formatMah);
        number(tr("Cycle count"), battery, "cycle_count", formatCount);
        // The driver keeps an estimate while discharging too, which means nothing then.
        qint64 toFull = 0;
        if (m_status == QLatin1String("Charging") && readInt(attrPath(battery, QStringLiteral("time_to_full_now")), &toFull)
                && toFull > 0)
            add(tr("Time to full"), battery + QStringLiteral("/time_to_full_now"), formatDuration(toFull), false);
        text(tr("Technology"), battery, "technology");
    }
    section(tr("Battery"));

    if (!port.isEmpty()) {
        const QString dir = m_typec + QLatin1Char('/') + port + QLatin1Char('/');
        add(tr("Charger PD revision"),
            (partner.isEmpty() ? port + QStringLiteral("-partner") : partner) + QStringLiteral("/usb_power_delivery_revision"),
            !m_pdRevision.isEmpty() ? m_pdRevision : partner.isEmpty() ? tr("Nothing plugged in") : tr("No PD"), false);
        const QString mode = readText(dir + QStringLiteral("power_operation_mode"));
        if (!mode.isEmpty()) {
            const QString shown = mode == QLatin1String("default") ? tr("Default USB")
                                : mode == QLatin1String("usb_power_delivery") ? tr("USB PD")
                                : QString(mode).replace(QLatin1String("A"), QLatin1String(" A"));
            add(tr("Type-C power mode"), port + QStringLiteral("/power_operation_mode"), shown, false);
        }
        const QString power = bracketed(readText(dir + QStringLiteral("power_role")));
        if (!power.isEmpty())
            add(tr("Power role"), port + QStringLiteral("/power_role"), power, false);
        const QString data = bracketed(readText(dir + QStringLiteral("data_role")));
        if (!data.isEmpty())
            add(tr("Data role"), port + QStringLiteral("/data_role"), data, false);
    }
    // SDP, DCP and CDP come from BC1.2's look at the data lines, which a PD charger can fail:
    // the Jolla Phone says SDP while on PD.
    static const QStringList Bc12 = {QStringLiteral("SDP"), QStringLiteral("DCP"), QStringLiteral("CDP")};
    if (!m_usbType.isEmpty())
        addDetailed(tr("USB type"), usbTypeSource, m_usbType,
                    Bc12.contains(m_usbType) ? tr("BC1.2 detection") : QString(), false);
    if (!port.isEmpty()) {
        const QString dir = m_typec + QLatin1Char('/') + port + QLatin1Char('/');
        const QString pd = readText(dir + QStringLiteral("usb_power_delivery_revision"));
        if (!pd.isEmpty())
            add(tr("Phone PD revision"), port + QStringLiteral("/usb_power_delivery_revision"), pd, false);
        const QString typec = readText(dir + QStringLiteral("usb_typec_revision"));
        if (!typec.isEmpty())
            add(tr("Type-C revision"), port + QStringLiteral("/usb_typec_revision"), typec, false);
    }
    section(tr("USB-C"));

    for (const QString &name : supplies) {
        qint64 state = -1;
        readInt(attrPath(name, QStringLiteral("online")), &state);
        addDetailed(chipName(name, chipModel(attrPath(name, QString()), name)), name + QStringLiteral("/online"),
                    state >= 1 ? tr("Online") : state == 0 ? tr("Offline") : QStringLiteral("—"),
                    state == 2 ? tr("programmable input") : QString(), state < 1);
    }
    section(tr("Charger chips"));
}

QVariantList ChargeMonitor::allReadings() const
{
    QVariantList groups;
    const auto group = [&groups](const QString &title, const QString &path, const QVariantList &items) {
        if (items.isEmpty())
            return;
        QVariantMap map;
        map.insert(QStringLiteral("title"), title);
        map.insert(QStringLiteral("path"), path);
        map.insert(QStringLiteral("items"), items);
        groups.append(map);
    };
    const auto item = [](const QString &name, const QString &value) {
        QVariantMap map;
        map.insert(QStringLiteral("name"), name);
        map.insert(QStringLiteral("value"), value);
        return QVariant(map);
    };

    // A supply's uevent holds every property it has, as POWER_SUPPLY_<NAME>=<value>.
    const QStringList supplies = QDir(m_powerSupply).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &supply : supplies) {
        QVariantList items;
        QSet<QString> seen;
        const QStringList lines = readText(attrPath(supply, QStringLiteral("uevent"))).split(QLatin1Char('\n'));
        for (const QString &line : lines) {
            const int eq = line.indexOf(QLatin1Char('='));
            if (eq <= 0)
                continue;
            QString key = line.left(eq);
            if (key.startsWith(QLatin1String("POWER_SUPPLY_")))
                key = key.mid(13).toLower();
            if (key == QLatin1String("name") || key == QLatin1String("DEVTYPE") || seen.contains(key))
                continue;
            seen.insert(key);
            items.append(item(key, line.mid(eq + 1)));
        }
        group(supply, m_powerSupply + QLatin1Char('/') + supply, items);
    }

    const QStringList ports = QDir(m_typec).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &port : ports) {
        const QString dir = m_typec + QLatin1Char('/') + port;
        QVariantList items;
        const QStringList files = QDir(dir).entryList(QDir::Files, QDir::Name);
        for (const QString &file : files) {
            if (file == QLatin1String("uevent"))
                continue;
            const QString value = readText(dir + QLatin1Char('/') + file);
            if (!value.isEmpty())
                items.append(item(file, value));
        }
        group(port, dir, items);
    }
    return groups;
}

void ChargeMonitor::scanOffers()
{
    m_lastScan = QDateTime::currentMSecsSinceEpoch();
    if (!m_plugged) {
        // Remember where the log ends, so the next charger isn't shown this one's offers.
        const KernelLog::Scan scan = KernelLog::scan(std::numeric_limits<double>::max(), m_logFile);
        if (scan.readable)
            m_logMark = scan.lastTime;
        m_logError = scan.error;
        m_pdos.clear();
        m_logSelected = -1;
        m_offersSource.clear();
        emit offersChanged();
        return;
    }

    const KernelLog::Scan scan = KernelLog::scan(m_logMark, m_logFile);
    m_logError = scan.error;
    if (!scan.pdos.isEmpty()) {
        m_pdos = scan.pdos;
        m_logSelected = scan.selected;
        m_offersSource = QStringLiteral("dmesg");
    } else if (m_offersSource == QLatin1String("dmesg") && !m_pdos.isEmpty()) {
        // Found earlier for this charger: other drivers' chatter may have pushed it out of the
        // ring buffer since, so keep it until the charger is unplugged.
    } else if (readTypecOffers()) {
        m_logSelected = -1;
        m_offersSource = QStringLiteral("typec");
    } else {
        m_pdos.clear();
        m_logSelected = -1;
        m_offersSource.clear();
    }
    emit offersChanged();
}

// The partner's source capabilities, where the Type-C class exposes them:
// /sys/class/typec/port0-partner/usb_power_delivery/source-capabilities/<n>:<kind>/
bool ChargeMonitor::readTypecOffers()
{
    QVector<KernelLog::Pdo> pdos;
    const QStringList ports = QDir(m_typec).entryList({QStringLiteral("*-partner")}, QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &port : ports) {
        const QString caps = m_typec + QLatin1Char('/') + port + QStringLiteral("/usb_power_delivery/source-capabilities");
        const QStringList entries = QDir(caps).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString &entry : entries) {
            const QString dir = caps + QLatin1Char('/') + entry + QLatin1Char('/');
            KernelLog::Pdo pdo;
            pdo.index = leadingInt(entry);
            pdo.ma = leadingInt(readText(dir + QStringLiteral("maximum_current")));
            if (entry.endsWith(QLatin1String("fixed_supply"))) {
                pdo.minMv = pdo.maxMv = leadingInt(readText(dir + QStringLiteral("voltage")));
            } else if (entry.endsWith(QLatin1String("programmable_supply"))) {
                pdo.minMv = leadingInt(readText(dir + QStringLiteral("minimum_voltage")));
                pdo.maxMv = leadingInt(readText(dir + QStringLiteral("maximum_voltage")));
                pdo.pps = true;
            } else {
                continue;   // battery and variable supplies don't come up on phone chargers
            }
            if (pdo.maxMv > 0)
                pdos.append(pdo);
        }
        if (!pdos.isEmpty())
            break;
    }
    std::sort(pdos.begin(), pdos.end(),
              [](const KernelLog::Pdo &a, const KernelLog::Pdo &b) { return a.index < b.index; });
    m_pdos = pdos;
    return !pdos.isEmpty();
}

// Which offer the phone is on: the driver's word if it logged it, else the one VBUS matches.
int ChargeMonitor::inUseIndex() const
{
    if (m_logSelected >= 0) {
        for (int i = 0; i < m_pdos.size(); ++i) {
            const KernelLog::Pdo &p = m_pdos.at(i);
            if (p.index != m_logSelected)
                continue;
            // The listing is from a few seconds ago; a fixed offer VBUS is far from has been left.
            if (p.pps || m_vbusMv <= 0 || std::abs(m_vbusMv - p.maxMv) <= std::max(1000, p.maxMv / 7))
                return i;
            break;
        }
    }
    if (!m_plugged || m_vbusMv <= 0)
        return -1;

    int fixed = -1;
    int fixedDiff = std::numeric_limits<int>::max();
    int pps = -1;
    for (int i = 0; i < m_pdos.size(); ++i) {
        const KernelLog::Pdo &p = m_pdos.at(i);
        if (p.pps) {
            if (pps < 0 && (m_direct || (m_vbusMv >= p.minMv - 300 && m_vbusMv <= p.maxMv + 300)))
                pps = i;
            continue;
        }
        const int diff = std::abs(m_vbusMv - p.maxMv);
        if (diff <= std::max(500, p.maxMv / 10) && diff < fixedDiff) {
            fixed = i;
            fixedDiff = diff;
        }
    }
    if (m_direct && pps >= 0)
        return pps;
    return fixed >= 0 ? fixed : pps;
}

QVariantList ChargeMonitor::offers() const
{
    QVariantList list;
    const int inUse = inUseIndex();
    // Shown as PDO positions, which count from 1; the log's cap[] counts from 0.
    const int base = !m_pdos.isEmpty() && m_pdos.first().index == 0 ? 1 : 0;
    for (int i = 0; i < m_pdos.size(); ++i) {
        const KernelLog::Pdo &p = m_pdos.at(i);
        QVariantMap offer;
        offer.insert(QStringLiteral("index"), p.index + base);
        offer.insert(QStringLiteral("pps"), p.pps);
        offer.insert(QStringLiteral("minMv"), p.minMv);
        offer.insert(QStringLiteral("maxMv"), p.maxMv);
        offer.insert(QStringLiteral("ma"), p.ma);
        offer.insert(QStringLiteral("watts"), p.maxMv * double(p.ma) / 1e6);
        offer.insert(QStringLiteral("inUse"), i == inUse);
        list.append(offer);
    }
    return list;
}

QVariantList ChargeMonitor::history() const
{
    QVariantList list;
    list.reserve(m_history.size());
    for (const auto &point : m_history) {
        QVariantMap map;
        map.insert(QStringLiteral("t"), point.first);
        map.insert(QStringLiteral("w"), point.second);
        list.append(map);
    }
    return list;
}

double ChargeMonitor::historyPeak() const
{
    double peak = 0;
    for (const auto &point : m_history)
        peak = std::max(peak, point.second);
    return peak;
}

QString ChargeMonitor::deviceReport() const
{
    QStringList out;
    out << QStringLiteral("ChargeScope %1 device report").arg(version());
    out << QStringLiteral("Device: %1").arg(deviceName());
    out << QStringLiteral("System: %1").arg(QSysInfo::prettyProductName());
    out << QStringLiteral("Kernel: %1").arg(QSysInfo::kernelVersion());
    out << QStringLiteral("Plugged in: %1, mode %2").arg(m_plugged ? QStringLiteral("yes") : QStringLiteral("no"), m_mode);

    const QVariantList groups = allReadings();
    for (const QVariant &entry : groups) {
        const QVariantMap group = entry.toMap();
        // The real path, whatever root this monitor reads, so reports from tests look the same.
        QString path = group.value(QStringLiteral("path")).toString();
        if (path.startsWith(m_powerSupply))
            path = QStringLiteral("/sys/class/power_supply") + path.mid(m_powerSupply.size());
        else if (path.startsWith(m_typec))
            path = QStringLiteral("/sys/class/typec") + path.mid(m_typec.size());
        out << QString() << path;
        const QVariantList items = group.value(QStringLiteral("items")).toList();
        for (const QVariant &i : items) {
            const QVariantMap item = i.toMap();
            out << QStringLiteral("  %1 = %2").arg(item.value(QStringLiteral("name")).toString(),
                                                   item.value(QStringLiteral("value")).toString());
        }
    }

    QString error;
    const QStringList lines = KernelLog::pdLines(200, &error, m_logFile);
    out << QString() << QStringLiteral("Kernel log, PD and charger lines:");
    if (!error.isEmpty())
        out << QStringLiteral("  (%1)").arg(error);
    else if (lines.isEmpty())
        out << QStringLiteral("  (none)");
    for (const QString &line : lines)
        out << QStringLiteral("  ") + line;
    return out.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

QString ChargeMonitor::deviceName() const
{
    const QStringList hw = readText(QStringLiteral("/etc/hw-release")).split(QLatin1Char('\n'));
    for (const QString &line : hw) {
        if (line.startsWith(QLatin1String("NAME=")))
            return line.mid(5).remove(QLatin1Char('"'));
    }
    return QStringLiteral("unknown");
}

QString ChargeMonitor::version() const
{
    return QStringLiteral(APP_VERSION);
}

QString ChargeMonitor::modeName() const
{
    if (m_mode == QLatin1String("pps"))
        return QStringLiteral("Direct charging (PPS)");
    if (m_mode == QLatin1String("pd"))
        return QStringLiteral("USB PD");
    if (m_mode == QLatin1String("basic"))
        return QStringLiteral("Basic USB");
    if (m_mode == QLatin1String("wireless"))
        return QStringLiteral("Wireless");
    return QStringLiteral("Not charging");
}

QString ChargeMonitor::report() const
{
    QStringList out;

    out << QStringLiteral("ChargeScope %1 report, %2").arg(version(), m_updated.toString(Qt::ISODate));
    QString mode = modeName();
    if (!m_pdRevision.isEmpty())
        mode += QStringLiteral(", PD %1").arg(m_pdRevision);
    if (!m_usbType.isEmpty())
        mode += QStringLiteral(", USB type %1").arg(m_usbType);
    out << QStringLiteral("Mode: %1").arg(mode);
    out << QStringLiteral("Battery power: %1 W %2")
                   .arg(QString::number(std::abs(m_watts), 'f', 2),
                        m_watts >= 0 ? QStringLiteral("in") : QStringLiteral("out"));

    out << QString() << QStringLiteral("Charger offers")
        + (m_offersSource.isEmpty() ? QString() : QStringLiteral(" (from %1)").arg(m_offersSource)) + QLatin1Char(':');
    const QVariantList list = offers();
    if (list.isEmpty())
        out << (m_plugged ? QStringLiteral("  none found") : QStringLiteral("  no charger"));
    for (const QVariant &item : list) {
        const QVariantMap o = item.toMap();
        const QString range = o.value(QStringLiteral("pps")).toBool()
                ? QStringLiteral("PPS   %1–%2").arg(volts(o.value(QStringLiteral("minMv")).toInt()),
                                                    volts(o.value(QStringLiteral("maxMv")).toInt()))
                : QStringLiteral("Fixed %1").arg(volts(o.value(QStringLiteral("maxMv")).toInt()));
        out << QStringLiteral("  #%1 %2 %3 (%4 W)%5")
                       .arg(o.value(QStringLiteral("index")).toInt())
                       .arg(range, amps(o.value(QStringLiteral("ma")).toInt()),
                            QString::number(o.value(QStringLiteral("watts")).toDouble(), 'f', 1),
                            o.value(QStringLiteral("inUse")).toBool() ? QStringLiteral("  <- in use") : QString());
    }
    if (!m_logError.isEmpty())
        out << QStringLiteral("  kernel log: %1").arg(m_logError);

    out << QString() << QStringLiteral("Readings (sysfs, under /sys/class/power_supply and /sys/class/typec):");
    for (const QVariant &entry : m_sections) {
        const QVariantMap section = entry.toMap();
        out << QString() << section.value(QStringLiteral("title")).toString() + QLatin1Char(':');
        const QVariantList items = section.value(QStringLiteral("items")).toList();
        for (const QVariant &i : items) {
            const QVariantMap item = i.toMap();
            const QString label = item.value(QStringLiteral("label")).toString();
            const QString param = item.value(QStringLiteral("param")).toString();
            QString value = item.value(QStringLiteral("value")).toString();
            const QString detail = item.value(QStringLiteral("detail")).toString();
            if (!detail.isEmpty())
                value += QStringLiteral(" (%1)").arg(detail);
            out << QStringLiteral("  %1: %2  [%3]").arg(label.isEmpty() ? param : label, value, param);
        }
    }

    out << QString() << QStringLiteral("Device: %1, %2, kernel %3")
                                .arg(deviceName(), QSysInfo::prettyProductName(), QSysInfo::kernelVersion());
    return out.join(QLatin1Char('\n')) + QLatin1Char('\n');
}
