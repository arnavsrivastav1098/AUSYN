#include "gaming_readiness.h"

#include <algorithm>
#include <utility>

namespace Ausyn {
namespace {

void addCheck(GameReadiness& result, QString name, double actual, double required,
              const QString& unit, bool known)
{
    if (required <= 0.0) return;
    RequirementCheck check;
    check.name = std::move(name);
    check.required = QStringLiteral("%1 %2 minimum").arg(required, 0, 'f', 1).arg(unit);
    if (!known) {
        check.actual = QStringLiteral("Not reported by Windows");
        check.state = RequirementState::Unknown;
    } else {
    check.actual = QStringLiteral("%1 %2 detected").arg(actual, 0, 'f', 2).arg(unit);
        check.state = actual >= required ? RequirementState::Met : RequirementState::NotMet;
        ++result.knownChecks;
        if (check.state == RequirementState::Met) ++result.passedChecks;
        else ++result.failedChecks;
    }
    result.checks.append(std::move(check));
}

} // namespace

GameReadiness GamingReadinessEngine::analyze(const GameRequirements& requirements,
                                             const SystemSnapshot& snapshot)
{
    GameReadiness result;
    result.title = requirements.title.trimmed().isEmpty()
        ? QStringLiteral("Custom game profile") : requirements.title.trimmed().left(80);
    addCheck(result, QStringLiteral("Installed memory"),
        static_cast<double>(snapshot.memoryTotalBytes) / 1'000'000'000.0,
        requirements.minimumRamGb, QStringLiteral("GB"), snapshot.memoryTotalBytes > 0);
    addCheck(result, QStringLiteral("Dedicated graphics memory"),
        static_cast<double>(snapshot.graphicsMemoryBytes) / 1'000'000'000.0,
        requirements.minimumVramGb, QStringLiteral("GB"), snapshot.graphicsMemoryBytes > 0);

    if (requirements.requiredFreeStorageGb > 0.0) {
        RequirementCheck check;
        check.name = QStringLiteral("Free space on %1").arg(requirements.driveRoot.isEmpty()
            ? QStringLiteral("selected drive") : requirements.driveRoot);
        check.required = QStringLiteral("%1 GB minimum").arg(requirements.requiredFreeStorageGb, 0, 'f', 1);
        const auto drive = std::find_if(snapshot.volumes.cbegin(), snapshot.volumes.cend(),
            [&requirements](const VolumeSample& volume) { return volume.rootPath.compare(requirements.driveRoot, Qt::CaseInsensitive) == 0; });
        if (requirements.driveRoot.isEmpty() || drive == snapshot.volumes.cend()) {
            check.actual = QStringLiteral("Choose an available drive");
            check.state = RequirementState::Unknown;
        } else {
            const double freeGb = static_cast<double>(drive->freeBytes) / 1'000'000'000.0;
            check.actual = QStringLiteral("%1 GB free").arg(freeGb, 0, 'f', 2);
            check.state = freeGb >= requirements.requiredFreeStorageGb
                ? RequirementState::Met : RequirementState::NotMet;
            ++result.knownChecks;
            if (check.state == RequirementState::Met) ++result.passedChecks;
            else ++result.failedChecks;
        }
        result.checks.append(std::move(check));
    }

    if (result.checks.isEmpty()) {
        result.summary = QStringLiteral("Enter one or more minimum requirements to assess this profile.");
        result.confidence = QStringLiteral("No requirements entered");
    } else if (result.failedChecks > 0) {
        result.summary = QStringLiteral("One or more measured minimums are below the values you entered. The game may struggle or fail to launch.");
        result.confidence = result.knownChecks == result.checks.size() ? QStringLiteral("All listed checks measured")
                                                                       : QStringLiteral("Partial measurements");
    } else if (result.knownChecks == 0) {
        result.summary = QStringLiteral("Ausyn cannot compare these requirements with the hardware readings Windows exposed.");
        result.confidence = QStringLiteral("Insufficient hardware data");
    } else if (result.knownChecks < result.checks.size()) {
        result.summary = QStringLiteral("Some minimums match, but at least one requirement could not be measured.");
        result.confidence = QStringLiteral("Partial measurements");
    } else {
        result.summary = QStringLiteral("All entered minimums are met by the measured resources. Actual frame rate and compatibility are not guaranteed.");
        result.confidence = QStringLiteral("All listed checks measured");
    }
    return result;
}

} // namespace Ausyn
