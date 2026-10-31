#include <QDir>
#include <QFile>
#include <QtTest>

#include "chargemonitor.h"
#include "kernellog.h"

// What the app makes of a phone's sysfs files: the mode and every section the page shows.
static QString snapshot(const ChargeMonitor &monitor)
{
    QStringList out;
    out << QStringLiteral("mode: %1").arg(monitor.mode());
    out << QStringLiteral("plugged: %1").arg(monitor.plugged() ? QStringLiteral("yes") : QStringLiteral("no"));
    out << QStringLiteral("watts: %1").arg(QString::number(monitor.watts(), 'f', 2));
    const QVariantList offers = monitor.offers();
    for (const QVariant &entry : offers) {
        const QVariantMap o = entry.toMap();
        out << QStringLiteral("offer #%1: %2 %3-%4 mV %5 mA%6")
                       .arg(o.value(QStringLiteral("index")).toInt())
                       .arg(o.value(QStringLiteral("pps")).toBool() ? QStringLiteral("pps") : QStringLiteral("fixed"))
                       .arg(o.value(QStringLiteral("minMv")).toInt())
                       .arg(o.value(QStringLiteral("maxMv")).toInt())
                       .arg(o.value(QStringLiteral("ma")).toInt())
                       .arg(o.value(QStringLiteral("inUse")).toBool() ? QStringLiteral(" [in use]") : QString());
    }
    const QVariantList sections = monitor.sections();
    for (const QVariant &entry : sections) {
        const QVariantMap section = entry.toMap();
        out << QString() << QStringLiteral("[%1]").arg(section.value(QStringLiteral("title")).toString());
        const QVariantList items = section.value(QStringLiteral("items")).toList();
        for (const QVariant &i : items) {
            const QVariantMap item = i.toMap();
            QString line = QStringLiteral("%1: %2").arg(item.value(QStringLiteral("label")).toString(),
                                                        item.value(QStringLiteral("value")).toString());
            const QString detail = item.value(QStringLiteral("detail")).toString();
            if (!detail.isEmpty())
                line += QStringLiteral(" (%1)").arg(detail);
            if (item.value(QStringLiteral("dim")).toBool())
                line += QStringLiteral(" [dim]");
            out << line + QStringLiteral("  <- ") + item.value(QStringLiteral("param")).toString();
        }
    }
    return out.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

class TestChargeScope : public QObject
{
    Q_OBJECT

private slots:
    void parseJollaPhone();
    void parseKeepsNewestListing();
    void parseIgnoresListingsBeforeMark();
    void parseRangesAndBareNumbers();
    void devices_data();
    void devices();
};

// As the Jolla Phone's pd_adapter logs a 45 W charger (no PPS) while on its 9 V offer.
static const QString Log = QStringLiteral(
        "[99445.745391] [  T569] pd_adapter pd-adapter: pd_get_cap_pdo nr:4 idx:1\n"
        "[99445.745441] [  T569] pd_adapter pd-adapter: pd_get_cap_pdo cap number = 4\n"
        "[99445.745461] [  T569] pd_adapter pd-adapter: pd_get_cap_pdo cap[0], 5000mV, 2400mA, 12000000uW\n"
        "[99445.745481] [  T569] pd_adapter pd-adapter: pd_get_cap_pdo cap[1], 9000mV, 3000mA, 27000000uW\n"
        "[99455.759430] [  T569] pd_adapter pd-adapter: pd_get_cap_pdo nr:4 idx:2\n"
        "[99455.759494] [  T569] pd_adapter pd-adapter: pd_get_cap_pdo cap number = 4\n"
        "[99455.759536] [  T569] pd_adapter pd-adapter: pd_get_cap_pdo cap[0], 5000mV, 2400mA, 12000000uW\n"
        "[99455.759585] [  T569] pd_adapter pd-adapter: pd_get_cap_pdo cap[1], 9000mV, 3000mA, 27000000uW\n"
        "[99455.759632] [  T569] pd_adapter pd-adapter: pd_get_cap_pdo cap[2], 15000mV, 3000mA, 45000000uW\n"
        "[99455.759680] [  T569] pd_adapter pd-adapter: pd_get_cap_pdo cap[3], 20000mV, 2250mA, 45000000uW\n"
        "[99455.779111] [  T569] pd_adapter pd-adapter: pd_authentication pd type is not snk apdo\n"
        "[99456.000000] [T20272] mtk_mminfra_pd_callback: disable clk ref_cnt=0\n");

void TestChargeScope::parseJollaPhone()
{
    const KernelLog::Scan scan = KernelLog::parse(Log, 0);
    QVERIFY(scan.readable);
    QCOMPARE(scan.pdos.size(), 4);
    QCOMPARE(scan.pdos.at(0).maxMv, 5000);
    QCOMPARE(scan.pdos.at(0).ma, 2400);
    QCOMPARE(scan.pdos.at(3).maxMv, 20000);
    QCOMPARE(scan.pdos.at(3).ma, 2250);
    for (const KernelLog::Pdo &pdo : scan.pdos)
        QVERIFY(!pdo.pps);
}

void TestChargeScope::parseKeepsNewestListing()
{
    const KernelLog::Scan scan = KernelLog::parse(Log, 0);
    QCOMPARE(scan.pdos.size(), 4);       // the 99455 s listing, not the shorter one before it
    QCOMPARE(scan.selected, 1);          // idx:2 counts from 1: cap[1], the 9 V offer
    QCOMPARE(scan.lastTime, 99456.0);
}

void TestChargeScope::parseIgnoresListingsBeforeMark()
{
    QVERIFY(KernelLog::parse(Log, 99455.8).pdos.isEmpty());
    QCOMPARE(KernelLog::parse(Log, 99450).pdos.size(), 4);
}

// Formats not seen on a phone yet, that the parser takes anyway: a PPS range, and no units.
void TestChargeScope::parseRangesAndBareNumbers()
{
    const KernelLog::Scan scan = KernelLog::parse(QStringLiteral(
            "[1.0] pd_get_cap_pdo cap[0], 5000, 3000\n"
            "[1.1] pd_get_cap_pdo cap[1], 3300-11000mV, 3000mA\n"
            "[1.2] pd_get_cap_pdo cap[2], 3300mV, 16000mV, 2000mA\n"), 0);
    QCOMPARE(scan.pdos.size(), 3);
    QCOMPARE(scan.pdos.at(0).maxMv, 5000);
    QCOMPARE(scan.pdos.at(0).ma, 3000);
    QVERIFY(scan.pdos.at(1).pps);
    QCOMPARE(scan.pdos.at(1).minMv, 3300);
    QCOMPARE(scan.pdos.at(1).maxMv, 11000);
    QVERIFY(scan.pdos.at(2).pps);
    QCOMPARE(scan.pdos.at(2).maxMv, 16000);
    QCOMPARE(scan.pdos.at(2).ma, 2000);
}

// Every devices/<device>/<state>/ that holds a sys/ copy (and maybe a dmesg.txt), compared with
// its expected.txt.
// Run with CHARGESCOPE_UPDATE=1 to write expected.txt from what the app makes of it now.
void TestChargeScope::devices_data()
{
    QTest::addColumn<QString>("dir");
    QDir root(QStringLiteral(FIXTURES));
    const QStringList devices = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &device : devices) {
        const QStringList states = QDir(root.filePath(device)).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString &state : states) {
            const QString dir = root.filePath(device + QLatin1Char('/') + state);
            if (QDir(dir + QStringLiteral("/sys")).exists())
                QTest::newRow(qPrintable(device + QLatin1Char('/') + state)) << dir;
        }
    }
}

void TestChargeScope::devices()
{
    QFETCH(QString, dir);
    // A saved kernel log next to sys/, if the device has one; never the host's own.
    ChargeMonitor monitor(dir + QStringLiteral("/sys"), dir + QStringLiteral("/dmesg.txt"));
    const QString actual = snapshot(monitor);

    QFile expected(dir + QStringLiteral("/expected.txt"));
    if (qEnvironmentVariableIsSet("CHARGESCOPE_UPDATE")) {
        QVERIFY(expected.open(QIODevice::WriteOnly | QIODevice::Truncate));
        expected.write(actual.toUtf8());
        return;
    }
    QVERIFY2(expected.open(QIODevice::ReadOnly),
             "no expected.txt: run with CHARGESCOPE_UPDATE=1 and check what it writes");
    const QStringList want = QString::fromUtf8(expected.readAll()).split(QLatin1Char('\n'));
    const QStringList got = actual.split(QLatin1Char('\n'));
    for (int i = 0; i < std::max(want.size(), got.size()); ++i)
        QCOMPARE(got.value(i), want.value(i));
}

QTEST_GUILESS_MAIN(TestChargeScope)
#include "tst_chargescope.moc"
