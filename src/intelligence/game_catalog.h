#pragma once

#include <QDate>
#include <QString>
#include <QStringList>
#include <QVector>

namespace Ausyn {

struct GameCatalogProfile {
    QString id;
    QString title;
    QStringList executableNames;
    double minimumRamGb = 0.0;
    double recommendedRamGb = 0.0;
    double minimumVramGb = 0.0;
    double recommendedVramGb = 0.0;
    double minimumStorageGb = 0.0;
    QString minimumCpu;
    QString recommendedCpu;
    QString minimumGpu;
    QString recommendedGpu;
    QString performanceGuidance;
    QString balancedGuidance;
    QString sourceUrl;
    QDate sourceCheckedDate;
};

class GameCatalog final {
public:
    [[nodiscard]] static const QVector<GameCatalogProfile>& profiles();
    [[nodiscard]] static const GameCatalogProfile* find(const QString& id);
    [[nodiscard]] static const GameCatalogProfile* findExecutable(const QString& executableName);
};

} // namespace Ausyn
