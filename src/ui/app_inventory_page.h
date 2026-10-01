#pragma once

#include "../intelligence/app_inventory_types.h"
#include "../monitoring/system_snapshot.h"

#include <QWidget>
#include <QHash>

class QLabel;
class QLineEdit;
class QPushButton;
class QToolButton;
class QTableWidget;
class QTabWidget;
class QTextBrowser;
class QListWidget;

namespace Ausyn {

class AppInventoryPage final : public QWidget {
    Q_OBJECT
public:
    explicit AppInventoryPage(QWidget* parent = nullptr);
    void setBusy(bool busy);
    void setUpdate(const AppInventoryUpdate& update);
    void setSoftwareChangeHistory(const QStringList& changes);
    void setRunningProcesses(const QVector<ProcessSample>& processes, const QDateTime& sampledAt);
    [[nodiscard]] QVector<StartupResourceSignal> proactiveReviewSignals() const;

signals:
    void refreshRequested();
    void clearSoftwareHistoryRequested();

private slots:
    void applyFilter();
    void showStartupDetails();
    void showInstalledDetails();
    void updateStartupActivity();
    void exportCurrentList();

private:
    QVector<StartupEntry> startupEntries_;
    QVector<InstalledAppEntry> installedApps_;
    QVector<StartupEntry> visibleStartup_;
    QVector<InstalledAppEntry> visibleApps_;
    QVector<ProcessSample> runningProcesses_;
    QHash<QString, int> consecutiveHighResourceSamples_;
    QDateTime runningProcessesSampledAt_;
    QDateTime lastStartupSampleAt_;
    QLabel* status_ = nullptr;
    QLineEdit* search_ = nullptr;
    QPushButton* refreshButton_ = nullptr;
    QPushButton* exportButton_ = nullptr;
    QTableWidget* startupTable_ = nullptr;
    QTableWidget* appsTable_ = nullptr;
    QTabWidget* tabs_ = nullptr;
    QTextBrowser* details_ = nullptr;
    QListWidget* softwareChanges_ = nullptr;
    QPushButton* clearSoftwareChanges_ = nullptr;
};

} // namespace Ausyn
