#ifndef CHARGEMONITOR_H
#define CHARGEMONITOR_H

#include <QDateTime>
#include <QObject>
#include <QPair>
#include <QTimer>
#include <QVariantList>
#include <QVector>

#include "kernellog.h"

// Reads the charger and battery from sysfs every two seconds while `active`, and what the
// charger offers from the kernel log at start and whenever a charger is plugged in.
class ChargeMonitor : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(QDateTime updated READ updated NOTIFY sampled)

    // "pps", "pd", "basic" or "none"
    Q_PROPERTY(QString mode READ mode NOTIFY sampled)
    Q_PROPERTY(bool plugged READ plugged NOTIFY sampled)
    // Into the battery; negative while it discharges.
    Q_PROPERTY(double watts READ watts NOTIFY sampled)

    // -1 where the phone doesn't report it.
    Q_PROPERTY(int vbusMv READ vbusMv NOTIFY sampled)
    Q_PROPERTY(int inputLimitMa READ inputLimitMa NOTIFY sampled)
    Q_PROPERTY(int batteryMa READ batteryMa NOTIFY sampled)
    Q_PROPERTY(int batteryMv READ batteryMv NOTIFY sampled)
    Q_PROPERTY(double temperature READ temperature NOTIFY sampled)   // °C, NaN if unknown
    Q_PROPERTY(int capacity READ capacity NOTIFY sampled)
    Q_PROPERTY(QString status READ status NOTIFY sampled)
    Q_PROPERTY(QString usbType READ usbType NOTIFY sampled)
    Q_PROPERTY(QString pdRevision READ pdRevision NOTIFY sampled)
    // [{name, online: 1/0/-1}] for every mtk* power supply
    Q_PROPERTY(QVariantList chips READ chips NOTIFY sampled)

    // [{index, pps, minMv, maxMv, ma, watts, inUse}]
    Q_PROPERTY(QVariantList offers READ offers NOTIFY sampled)
    // "dmesg", "typec" or "" when nothing was found
    Q_PROPERTY(QString offersSource READ offersSource NOTIFY offersChanged)
    Q_PROPERTY(QString logError READ logError NOTIFY offersChanged)

    // [{t: ms since the epoch, w}] for the last ten minutes, charging watts only
    Q_PROPERTY(QVariantList history READ history NOTIFY historyChanged)
    Q_PROPERTY(double historyPeak READ historyPeak NOTIFY historyChanged)

    Q_PROPERTY(QString version READ version CONSTANT)

public:
    explicit ChargeMonitor(QObject *parent = nullptr);

    bool active() const { return m_active; }
    void setActive(bool active);
    QDateTime updated() const { return m_updated; }

    QString mode() const { return m_mode; }
    bool plugged() const { return m_plugged; }
    double watts() const { return m_watts; }
    int vbusMv() const { return m_vbusMv; }
    int inputLimitMa() const { return m_inputLimitMa; }
    int batteryMa() const { return m_batteryMa; }
    int batteryMv() const { return m_batteryMv; }
    double temperature() const { return m_temperature; }
    int capacity() const { return m_capacity; }
    QString status() const { return m_status; }
    QString usbType() const { return m_usbType; }
    QString pdRevision() const { return m_pdRevision; }
    QVariantList chips() const { return m_chips; }
    QVariantList offers() const;
    QString offersSource() const { return m_offersSource; }
    QString logError() const { return m_logError; }
    QVariantList history() const;
    double historyPeak() const;
    QString version() const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE QString report() const;

signals:
    void activeChanged();
    void sampled();
    void offersChanged();
    void historyChanged();

private:
    void sample();
    void scanOffers();
    bool readTypecOffers();
    int inUseIndex() const;
    QString modeName() const;

    QTimer m_poll;
    QTimer m_rescan;
    int m_rescansLeft = 0;
    bool m_active = false;
    bool m_first = true;
    QDateTime m_updated;

    QString m_mode = QStringLiteral("none");
    bool m_plugged = false;
    bool m_direct = false;
    double m_watts = 0;
    int m_vbusMv = -1;
    int m_inputLimitMa = -1;
    int m_batteryMa = -1;
    int m_batteryMv = -1;
    double m_temperature;
    int m_capacity = -1;
    int m_currentSign = 0;   // +1 or -1 once the battery's sign convention is known
    QString m_status;
    QString m_usbType;
    QString m_pdRevision;
    QVariantList m_chips;

    QVector<KernelLog::Pdo> m_pdos;
    int m_logSelected = -1;
    double m_logMark = 0;
    QString m_offersSource;
    QString m_logError;

    QVector<QPair<qint64, double>> m_history;
};

#endif
