#pragma once

#include "../monitoring/system_snapshot.h"

#include <QElapsedTimer>
#include <QVector>
#include <QWidget>

class QLabel;
class QTableWidget;
class QTimer;
class QWidget;

namespace Ausyn {

class AgentHealthPage final : public QWidget {
    Q_OBJECT
public:
    explicit AgentHealthPage(QWidget* parent = nullptr);
    void setSnapshot(const SystemSnapshot& snapshot);
    void setSamplingIntervalSeconds(int seconds);

private slots:
    void refreshFreshness();

private:
    QLabel* status_ = nullptr;
    QLabel* uptime_ = nullptr;
    QLabel* freshness_ = nullptr;
    QLabel* cadence_ = nullptr;
    QLabel* collectionLatency_ = nullptr;
    QLabel* ausynResourceUse_ = nullptr;
    QWidget* ausynCpuTrend_ = nullptr;
    QTableWidget* collectors_ = nullptr;
    QElapsedTimer uptimeClock_;
    QElapsedTimer sampleClock_;
    QDateTime previousCapture_;
    qint64 previousMonotonicSampleMs_ = -1;
    quint64 sampleCount_ = 0;
    quint64 previousAusynCpuTicks_ = 0;
    qint64 previousAusynSampleMs_ = -1;
    QVector<qint64> recentCollectionMicroseconds_;
    QVector<double> recentAusynCpuPercent_;
    QVector<double> recentAusynCpuTrendPercent_;
    QVector<double> recentAusynWorkingSetMb_;
    QVector<double> recentAusynPrivateMemoryMb_;
    int samplingIntervalSeconds_ = 5;
    QTimer* refreshTimer_ = nullptr;
};

} // namespace Ausyn
