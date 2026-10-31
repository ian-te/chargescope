#include "chargemonitor.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSysInfo>
#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace {

const QString PowerSupply = QStringLiteral("/sys/class/power_supply");
const QString Typec = QStringLiteral("/sys/class/typec");
const qint64 HistoryMs = 10 * 60 * 1000;
const int PollMs = 2000;
// After a plug-in the PD contract takes a moment, so look at the log again a few times.
const int RescanMs[] = {2000, 5000, 15000};
const int Rescans = sizeof(RescanMs) / sizeof(RescanMs[0]);

// The supplies MediaTek's charger framework reports VBUS and the input limit on, best first.
const QStringList InputSupplies = {QStringLiteral("mtk-master-charger"), QStringLiteral("mtk_charger_type"),
                                   QStringLiteral("usb"), QStringLiteral("ac")};

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

// The first supply in `preferred`, then any other online one, that reports `attr` above zero.
qint64 readInput(const QString &attr, const QStringList &online)
{
    QStringList order = InputSupplies;
    for (const QString &name : online) {
        if (!order.contains(name))
            order.append(name);
    }
    for (const QString &name : order) {
        qint64 v = 0;
        if (readInt(PowerSupply + QLatin1Char('/') + name + QLatin1Char('/') + attr, &v) && v > 0)
            return v;
    }
    return -1;
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

ChargeMonitor::ChargeMonitor(QObject *parent)
    : QObject(parent)
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
    QStringList online;
    bool plugged = false;
    bool divider = false;
    m_usbType.clear();
    m_chips.clear();

    const QStringList names = QDir(PowerSupply).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &name : names) {
        const QString dir = PowerSupply + QLatin1Char('/') + name + QLatin1Char('/');
        const QString type = readText(dir + QStringLiteral("type"));
        if (type == QLatin1String("Battery")) {
            if (battery.isEmpty() || name == QLatin1String("battery"))
                battery = dir;
            continue;
        }
        qint64 state = -1;
        readInt(dir + QStringLiteral("online"), &state);
        if (state == 1) {
            plugged = true;
            online.append(name);
            if (name.contains(QLatin1String("divider")) || name.contains(QLatin1String("dvchg")))
                divider = true;
        }
        if (name.startsWith(QLatin1String("mtk"))) {
            QVariantMap chip;
            chip.insert(QStringLiteral("name"), name);
            chip.insert(QStringLiteral("online"), int(state));
            m_chips.append(chip);
        }
        // "Unknown SDP DCP CDP [PD] PD_PPS": the one in brackets is what's connected.
        const QString usbType = readText(dir + QStringLiteral("usb_type"));
        const int open = usbType.indexOf(QLatin1Char('['));
        const int close = usbType.indexOf(QLatin1Char(']'), open);
        if (open >= 0 && close > open && m_usbType.isEmpty() && state == 1) {
            const QString selected = usbType.mid(open + 1, close - open - 1);
            if (selected != QLatin1String("Unknown"))
                m_usbType = selected;
        }
    }

    const qint64 vbus = plugged ? readInput(QStringLiteral("voltage_now"), online) : -1;
    // Microvolts as the ABI says, but a few drivers give millivolts.
    m_vbusMv = vbus < 0 ? -1 : int(vbus >= 100000 ? vbus / 1000 : vbus);
    const qint64 limit = plugged ? readInput(QStringLiteral("input_current_limit"), online) : -1;
    m_inputLimitMa = limit < 0 ? -1 : int(limit / 1000);

    m_status = battery.isEmpty() ? QString() : readText(battery + QStringLiteral("status"));
    qint64 v = 0;
    m_batteryMv = readInt(battery + QStringLiteral("voltage_now"), &v) ? int(v >= 100000 ? v / 1000 : v) : -1;
    m_capacity = readInt(battery + QStringLiteral("capacity"), &v) ? int(v) : -1;
    m_temperature = readInt(battery + QStringLiteral("temp"), &v) ? v / 10.0
                                                                  : std::numeric_limits<double>::quiet_NaN();
    if (readInt(battery + QStringLiteral("current_now"), &v)) {
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

    QString partnerRevision;
    QString portRevision;
    const QStringList ports = QDir(Typec).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &port : ports) {
        QString revision = readText(Typec + QLatin1Char('/') + port + QStringLiteral("/usb_power_delivery_revision"));
        if (revision.isEmpty() || revision == QLatin1String("0.0"))
            continue;
        if (port.endsWith(QLatin1String("-partner")))
            partnerRevision = revision;
        else if (!port.contains(QLatin1Char('-')))
            portRevision = revision;
    }
    m_pdRevision = !partnerRevision.isEmpty() ? partnerRevision : portRevision;

    if (m_first || plugged != m_plugged) {
        const bool plugIn = !m_first && plugged;
        m_first = false;
        m_plugged = plugged;
        scanOffers();
        if (plugIn) {
            m_rescansLeft = Rescans;
            m_rescan.start(RescanMs[0]);
        }
    }

    m_direct = divider || m_usbType == QLatin1String("PD_PPS");
    const int inUse = inUseIndex();
    if (!m_plugged)
        m_mode = QStringLiteral("none");
    else if (m_direct || (inUse >= 0 && m_pdos.at(inUse).pps))
        m_mode = QStringLiteral("pps");
    else if (m_usbType.startsWith(QLatin1String("PD")) || !partnerRevision.isEmpty() || !m_pdos.isEmpty())
        m_mode = QStringLiteral("pd");
    else
        m_mode = QStringLiteral("basic");

    m_updated = QDateTime::currentDateTime();
    const qint64 now = m_updated.toMSecsSinceEpoch();
    m_history.append(qMakePair(now, std::max(0.0, m_watts)));
    while (!m_history.isEmpty() && m_history.first().first < now - HistoryMs)
        m_history.removeFirst();

    emit sampled();
    emit historyChanged();
}

void ChargeMonitor::scanOffers()
{
    if (!m_plugged) {
        // Remember where the log ends, so the next charger isn't shown this one's offers.
        const KernelLog::Scan scan = KernelLog::scan(std::numeric_limits<double>::max());
        if (scan.readable)
            m_logMark = scan.lastTime;
        m_logError = scan.error;
        m_pdos.clear();
        m_logSelected = -1;
        m_offersSource.clear();
        emit offersChanged();
        return;
    }

    const KernelLog::Scan scan = KernelLog::scan(m_logMark);
    m_logError = scan.error;
    if (!scan.pdos.isEmpty()) {
        m_pdos = scan.pdos;
        m_logSelected = scan.selected;
        m_offersSource = QStringLiteral("dmesg");
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
    const QStringList ports = QDir(Typec).entryList({QStringLiteral("*-partner")}, QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &port : ports) {
        const QString caps = Typec + QLatin1Char('/') + port + QStringLiteral("/usb_power_delivery/source-capabilities");
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
            if (m_pdos.at(i).index == m_logSelected)
                return i;
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
    return QStringLiteral("Not charging");
}

QString ChargeMonitor::report() const
{
    QStringList out;
    const auto known = [](int value, const QString &text) { return value >= 0 ? text : QStringLiteral("—"); };

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

    out << QString() << QStringLiteral("Phone:");
    out << QStringLiteral("  VBUS: %1").arg(known(m_vbusMv, volts(m_vbusMv)));
    out << QStringLiteral("  Input current limit: %1").arg(known(m_inputLimitMa, QStringLiteral("%1 mA").arg(m_inputLimitMa)));
    out << QStringLiteral("  Battery current: %1 mA").arg(m_batteryMa);
    out << QStringLiteral("  Battery voltage: %1").arg(known(m_batteryMv, volts(m_batteryMv)));
    out << QStringLiteral("  Battery temperature: %1")
                   .arg(std::isnan(m_temperature) ? QStringLiteral("—")
                                                  : QString::number(m_temperature, 'f', 1) + QStringLiteral(" °C"));
    out << QStringLiteral("  Charge level: %1, %2")
                   .arg(known(m_capacity, QStringLiteral("%1 %").arg(m_capacity)),
                        m_status.isEmpty() ? QStringLiteral("—") : m_status);
    QStringList chips;
    for (const QVariant &item : m_chips) {
        const QVariantMap c = item.toMap();
        const int state = c.value(QStringLiteral("online")).toInt();
        chips << QStringLiteral("%1 %2").arg(c.value(QStringLiteral("name")).toString(),
                                             state == 1 ? QStringLiteral("online")
                                                        : state == 0 ? QStringLiteral("offline") : QStringLiteral("?"));
    }
    out << QStringLiteral("  Charger chips: %1").arg(chips.isEmpty() ? QStringLiteral("none found") : chips.join(QStringLiteral(", ")));

    QString device;
    const QStringList hw = readText(QStringLiteral("/etc/hw-release")).split(QLatin1Char('\n'));
    for (const QString &line : hw) {
        if (line.startsWith(QLatin1String("NAME=")))
            device = line.mid(5).remove(QLatin1Char('"'));
    }
    out << QString() << QStringLiteral("Device: %1, %2, kernel %3")
                                .arg(device.isEmpty() ? QStringLiteral("unknown") : device,
                                     QSysInfo::prettyProductName(), QSysInfo::kernelVersion());
    return out.join(QLatin1Char('\n')) + QLatin1Char('\n');
}
