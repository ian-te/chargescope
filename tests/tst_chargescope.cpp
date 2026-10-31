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
    void parseFixedAndRange();
    void parseKeepsNewestListing();
    void parseIgnoresListingsBeforeMark();
    void devices_data();
    void devices();
};

static const QString Log = QStringLiteral(
        "<6>[  10.000000][T100] pd_get_cap_pdo cap[0], 5000, 3000\n"
        "<6>[  10.000100][T100] pd_get_cap_pdo cap[1], 9000, 2220\n"
        "<6>[  50.000000][T100] [pd_get_cap] nr:4 idx:1\n"
        "<6>[  50.000100][T100] pd_get_cap_pdo cap[0], 5000, 3000\n"
        "<6>[  50.000200][T100] pd_get_cap_pdo cap[1], 9000, 3000\n"
        "<6>[  50.000300][T100] pd_get_cap_pdo cap[2], 3300, 11000, 3000\n"
        "<6>[  50.000400][T100] [pd_get_cap]:3 mv:[3300,16000] 2000 max:0 min:0 type:4 3\n"
        "<6>[  60.000000][T200] mtk_mminfra_pd_callback: disable clk ref_cnt=0\n");

void TestChargeScope::parseFixedAndRange()
{
    const KernelLog::Scan scan = KernelLog::parse(Log, 0);
    QVERIFY(scan.readable);
    QCOMPARE(scan.pdos.size(), 4);
    QCOMPARE(scan.pdos.at(1).maxMv, 9000);
    QVERIFY(!scan.pdos.at(1).pps);
    QCOMPARE(scan.pdos.at(2).minMv, 3300);
    QCOMPARE(scan.pdos.at(2).maxMv, 11000);
    QVERIFY(scan.pdos.at(2).pps);
    QCOMPARE(scan.pdos.at(3).maxMv, 16000);
    QCOMPARE(scan.pdos.at(3).ma, 2000);
}

void TestChargeScope::parseKeepsNewestListing()
{
    const KernelLog::Scan scan = KernelLog::parse(Log, 0);
    QCOMPARE(scan.pdos.at(1).ma, 3000);   // the 50 s listing, not the 10 s one
    QCOMPARE(scan.selected, 1);
    QCOMPARE(scan.lastTime, 60.0);
}

void TestChargeScope::parseIgnoresListingsBeforeMark()
{
    QVERIFY(KernelLog::parse(Log, 55).pdos.isEmpty());
    QCOMPARE(KernelLog::parse(Log, 20).pdos.size(), 4);
}

// Every devices/<device>/<state>/ that holds a sys/ copy, compared with its expected.txt.
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
    ChargeMonitor monitor(dir + QStringLiteral("/sys"));
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
