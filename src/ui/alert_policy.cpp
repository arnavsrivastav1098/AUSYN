#include "alert_policy.h"
#include <QTime>
namespace Ausyn {
bool AlertPolicy::categoryEnabled(const UserPreferences& p, AlertCategory category) {
    switch (category) {
    case AlertCategory::SystemFinding: return p.notifySystemFindings;
    case AlertCategory::Forecast: return p.notifyForecasts;
    case AlertCategory::EventLog: return p.notifyEventLogs;
    case AlertCategory::Startup: return p.notifyStartupApps;
    case AlertCategory::Security: return p.notifySecurity;
    }
    return false;
}
bool AlertPolicy::quietNow(const UserPreferences& p, const QDateTime& now) {
    if (!p.quietHoursEnabled) return false;
    const int minute = now.toLocalTime().time().hour() * 60 + now.toLocalTime().time().minute();
    const int start = p.quietHoursStartMinute, end = p.quietHoursEndMinute;
    return start == end || (start < end ? minute >= start && minute < end : minute >= start || minute < end);
}
AlertPresentation AlertPolicy::presentation(const UserPreferences& p, AlertCategory category,
    bool appHasFocus, bool windowsAvailable, const QDateTime& now, const QDateTime& snoozedUntil) {
    AlertPresentation result;
    if (!p.monitoringEnabled) result.reason = QStringLiteral("Monitoring is paused");
    else if (!p.desktopNotificationsEnabled) result.reason = QStringLiteral("Desktop alerts are disabled in Settings");
    else if (!categoryEnabled(p, category)) result.reason = QStringLiteral("This alert category is disabled in Settings");
    else if (quietNow(p, now)) result.reason = QStringLiteral("Quiet hours are active");
    else if (snoozedUntil.isValid() && snoozedUntil > now) result.reason = QStringLiteral("Desktop alerts are snoozed");
    else if (p.alertsOnlyInBackground && appHasFocus) result.reason = QStringLiteral("Advice is shown in Ausyn while you are using its window");
    else if (p.ausynPopupCards) result.ownPopup = true;
    else if (windowsAvailable) result.windowsRequest = true;
    else result.reason = QStringLiteral("Windows notification delivery is unavailable; select Ausyn cards in Settings");
    return result;
}
}
