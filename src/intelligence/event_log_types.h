#pragma once

#include "insight_types.h"

#include <QDateTime>
#include <QString>
#include <QVector>

namespace Ausyn {

enum class EventSeverity : int { Warning = 1, Error = 2, Critical = 3 };

struct EventInsight {
    QString channel;
    QString provider;
    quint32 eventId = 0;
    EventSeverity severity = EventSeverity::Warning;
    int occurrenceCount = 0;
    QDateTime firstAt;
    QDateTime latestAt;
    QString message;
    QString explanation;
    bool likelyApplicationCrash = false;
    QString affectedApplication;
    QString faultingModule;
    QString exceptionCode;
    bool windowsHardwareError = false;
    bool windowsStorageEvent = false;
    bool unexpectedShutdown = false;
    bool windowsBugCheck = false;
    bool applicationHang = false;
};

struct EventLogUpdate {
    QVector<EventInsight> insights;
    QDateTime checkedAt;
    bool available = false;
    QString status;
};

} // namespace Ausyn
