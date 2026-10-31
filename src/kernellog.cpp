#include "kernellog.h"

#include <QByteArray>
#include <QRegularExpression>
#include <QStringList>

#include <cerrno>
#include <cstring>
#include <utility>
#include <sys/klog.h>

namespace KernelLog {

namespace {
const int ActionReadAll = 3;      // SYSLOG_ACTION_READ_ALL
const int ActionSizeBuffer = 10;  // SYSLOG_ACTION_SIZE_BUFFER
}

Scan scan(double after)
{
    int size = klogctl(ActionSizeBuffer, nullptr, 0);
    if (size <= 0)
        size = 1 << 20;
    QByteArray buffer(size, Qt::Uninitialized);
    const int read = klogctl(ActionReadAll, buffer.data(), buffer.size());
    if (read < 0) {
        Scan failed;
        failed.error = errno == EPERM
                ? QStringLiteral("The kernel log is restricted (kernel.dmesg_restrict)")
                : QStringLiteral("Can't read the kernel log: %1").arg(QString::fromLocal8Bit(strerror(errno)));
        return failed;
    }
    buffer.truncate(read);
    return parse(QString::fromUtf8(buffer), after);
}

Scan parse(const QString &log, double after)
{
    // "<6>[  812.345678][T1234] ..." – the level and the caller id are optional.
    static const QRegularExpression timeRe(QStringLiteral("^(?:<\\d+>)?\\s*\\[\\s*(\\d+\\.\\d+)\\]"));
    // pd_get_cap_pdo cap[n], <mV>, <mA>, or <min mV>, <max mV>, <mA> for a range.
    static const QRegularExpression capRe(QStringLiteral(
            "pd_get_cap_pdo\\s+cap\\[(\\d+)\\]\\s*,\\s*(\\d+)\\s*,\\s*(\\d+)(?:\\s*,\\s*(\\d+))?"));
    // [pd_get_cap]:<n> mv:[<min>,<max>] <mA> ...
    static const QRegularExpression rangeRe(QStringLiteral(
            "pd_get_cap\\]?\\s*:\\s*(\\d+)\\s+mv:\\[\\s*(\\d+)\\s*,\\s*(\\d+)\\s*\\]\\s+(\\d+)"));
    // [pd_get_cap] nr:<count> idx:<selected>, logged before a listing
    static const QRegularExpression selectedRe(QStringLiteral("pd_get_cap\\]?.*\\bnr:\\s*\\d+\\s+idx:\\s*(-?\\d+)"));

    Scan result;
    result.readable = true;

    QVector<Pdo> group;
    double groupTime = 0;
    int groupSelected = -1;
    int pendingSelected = -1;
    auto finish = [&]() {
        if (!group.isEmpty() && groupTime > after) {
            result.pdos = group;
            result.selected = groupSelected;
        }
        group.clear();
    };

    const QStringList lines = log.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        double time = 0;
        const QRegularExpressionMatch t = timeRe.match(line);
        if (t.hasMatch()) {
            time = t.captured(1).toDouble();
            result.lastTime = time;
        }
        if (!line.contains(QLatin1String("pd_get_cap")))
            continue;

        Pdo pdo;
        QRegularExpressionMatch m = capRe.match(line);
        if (m.hasMatch()) {
            pdo.index = m.captured(1).toInt();
            if (m.capturedLength(4) > 0) {
                pdo.minMv = m.captured(2).toInt();
                pdo.maxMv = m.captured(3).toInt();
                pdo.ma = m.captured(4).toInt();
            } else {
                pdo.minMv = pdo.maxMv = m.captured(2).toInt();
                pdo.ma = m.captured(3).toInt();
            }
        } else if ((m = rangeRe.match(line)).hasMatch()) {
            pdo.index = m.captured(1).toInt();
            pdo.minMv = m.captured(2).toInt();
            pdo.maxMv = m.captured(3).toInt();
            pdo.ma = m.captured(4).toInt();
        } else {
            if ((m = selectedRe.match(line)).hasMatch())
                pendingSelected = m.captured(1).toInt();
            continue;
        }
        if (pdo.minMv > pdo.maxMv)
            std::swap(pdo.minMv, pdo.maxMv);
        pdo.pps = pdo.minMv != pdo.maxMv;

        // A listing counts up from cap[0]; going back, or a second's pause, starts the next one.
        if (!group.isEmpty() && (pdo.index <= group.last().index || time - groupTime > 1.0))
            finish();
        if (group.isEmpty()) {
            groupSelected = pendingSelected;
            pendingSelected = -1;
        }
        group.append(pdo);
        groupTime = time;
    }
    finish();
    return result;
}

}
