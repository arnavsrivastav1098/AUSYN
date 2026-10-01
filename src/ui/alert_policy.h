#pragma once
#include "../settings/user_preferences.h"

namespace Ausyn {
enum class AlertCategory { SystemFinding, Forecast, EventLog, Startup, Security };
struct AlertPresentation {
    bool ownPopup = false;
    bool windowsRequest = false;
    QString reason;
};
class AlertPolicy final {
public:
    static bool categoryEnabled(const UserPreferences& preferences, AlertCategory category);
    static bool quietNow(const UserPreferences& preferences, const QDateTime& now);
    static AlertPresentation presentation(const UserPreferences& preferences, AlertCategory category,
        bool appHasFocus, bool windowsAvailable, const QDateTime& now, const QDateTime& snoozedUntil = {});
};
}
