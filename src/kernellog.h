#ifndef KERNELLOG_H
#define KERNELLOG_H

#include <QString>
#include <QStringList>
#include <QVector>

// What the charger offers, as MediaTek's PD driver logs it to the kernel ring buffer.
namespace KernelLog {

struct Pdo {
    int index = 0;      // the source's PDO number (cap[n]), as logged
    int minMv = 0;      // equal to maxMv for a fixed supply
    int maxMv = 0;
    int ma = 0;
    bool pps = false;   // an augmented (programmable) PDO: minMv..maxMv
};

struct Scan {
    bool readable = false;
    QString error;          // why the log couldn't be read
    QVector<Pdo> pdos;      // the newest listing after the mark, if any
    int selected = -1;      // the cap[n] the driver says is in use, when it logs it
    double lastTime = 0;    // timestamp of the newest line, the next mark
};

// Reads the ring buffer (klogctl, as dmesg does) and keeps the newest PDO listing that was
// logged after the kernel timestamp `after`, so a listing from an earlier charger is ignored.
// With `file`, reads that saved log instead: tests do.
Scan scan(double after, const QString &file = QString());

// The parser on its own, for text already read.
Scan parse(const QString &log, double after);

// The newest `max` lines that look like they're about USB PD or charging, for device reports:
// other phones' drivers log the charger's offers in their own formats.
QStringList pdLines(int max, QString *error, const QString &file = QString());

}

#endif
