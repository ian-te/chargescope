#include "kernellog.h"

#include <QByteArray>
#include <QFile>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>

#include <cerrno>
#include <cstring>
#include <sys/klog.h>

namespace KernelLog {

namespace {

const int ActionReadAll = 3;      // SYSLOG_ACTION_READ_ALL
const int ActionSizeBuffer = 10;  // SYSLOG_ACTION_SIZE_BUFFER

// The whole ring buffer, as dmesg reads it, or a saved log.
bool read(QString *log, QString *error, const QString &file)
{
    if (!file.isEmpty()) {
        QFile saved(file);
        if (!saved.open(QIODevice::ReadOnly)) {
            *error = QStringLiteral("Can't read %1").arg(file);
            return false;
        }
        *log = QString::fromUtf8(saved.readAll());
        return true;
    }
    int size = klogctl(ActionSizeBuffer, nullptr, 0);
    if (size <= 0)
        size = 1 << 20;
    QByteArray buffer(size, Qt::Uninitialized);
    const int length = klogctl(ActionReadAll, buffer.data(), buffer.size());
    if (length < 0) {
        // dmesg_restrict, or a sandbox's seccomp filter
        *error = errno == EPERM
                ? QStringLiteral("Reading the kernel log isn't permitted")
                : QStringLiteral("Can't read the kernel log: %1").arg(QString::fromLocal8Bit(strerror(errno)));
        return false;
    }
    buffer.truncate(length);
    *log = QString::fromUtf8(buffer);
    return true;
}

}

Scan scan(double after, const QString &file)
{
    QString log;
    QString error;
    if (!read(&log, &error, file)) {
        Scan failed;
        failed.error = error;
        return failed;
    }
    return parse(log, after);
}

QStringList pdLines(int max, QString *error, const QString &file)
{
    static const QRegularExpression re(QStringLiteral(
            "pd_get_cap|pdo|apdo|\\bpps\\b|tcpc|tcpm|pe_snk|src_?cap|source_cap|typec|pd_dpm|hvdcp|\\bqc[23]|charger_type|chg_type"),
            QRegularExpression::CaseInsensitiveOption);
    QString log;
    QStringList lines;
    if (!read(&log, error, file))
        return lines;
    const QStringList all = log.split(QLatin1Char('\n'));
    for (const QString &line : all) {
        if (re.match(line).hasMatch())
            lines.append(line);
    }
    return lines.mid(std::max(0, lines.size() - max));
}

Scan parse(const QString &log, double after)
{
    // "<6>[  812.345678][T1234] ..." – the level and the caller id are optional.
    static const QRegularExpression timeRe(QStringLiteral("^(?:<\\d+>)?\\s*\\[\\s*(\\d+\\.\\d+)\\]"));
    // MediaTek's pd_adapter, one line per PDO:
    //   pd_get_cap_pdo cap[1], 9000mV, 3000mA, 27000000uW
    static const QRegularExpression capRe(QStringLiteral("pd_get_cap_pdo\\s+cap\\[(\\d+)\\]\\s*,(.*)$"));
    // Its fields: a number with an optional unit, or a range "3300-11000mV".
    static const QRegularExpression fieldRe(QStringLiteral(
            "(\\d+)\\s*(?:-\\s*(\\d+)\\s*)?(mV|mA|mW|uW|µW)?"));
    // ...and, before each listing, the PDO in use, counting from 1:
    //   pd_get_cap_pdo nr:4 idx:2
    static const QRegularExpression selectedRe(QStringLiteral("pd_get_cap\\w*\\s+nr:\\s*\\d+\\s+idx:\\s*(\\d+)"));

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

        QRegularExpressionMatch m = selectedRe.match(line);
        if (m.hasMatch()) {
            pendingSelected = m.captured(1).toInt() - 1;
            continue;
        }
        m = capRe.match(line);
        if (!m.hasMatch())
            continue;

        Pdo pdo;
        pdo.index = m.captured(1).toInt();
        QVector<int> volts;
        QVector<int> plain;
        QRegularExpressionMatchIterator fields = fieldRe.globalMatch(m.captured(2));
        while (fields.hasNext()) {
            const QRegularExpressionMatch f = fields.next();
            const QString unit = f.captured(3);
            if (unit == QLatin1String("mV")) {
                volts << f.captured(1).toInt();
                if (f.capturedLength(2) > 0)
                    volts << f.captured(2).toInt();
            } else if (unit == QLatin1String("mA")) {
                pdo.ma = f.captured(1).toInt();
            } else if (unit.isEmpty()) {
                plain << f.captured(1).toInt();
            }
        }
        if (volts.isEmpty()) {
            // No units: <mV>, <mA>, or <min mV>, <max mV>, <mA>.
            if (plain.size() < 2)
                continue;
            volts = plain.mid(0, plain.size() - 1);
            pdo.ma = plain.last();
        }
        pdo.minMv = *std::min_element(volts.begin(), volts.end());
        pdo.maxMv = *std::max_element(volts.begin(), volts.end());
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
