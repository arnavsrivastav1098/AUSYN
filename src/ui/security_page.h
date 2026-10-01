#pragma once

#include "../intelligence/security_status_types.h"

#include <QWidget>

class QLabel;
class QPushButton;
class QTextBrowser;

namespace Ausyn {

class SecurityPage final : public QWidget {
    Q_OBJECT
public:
    explicit SecurityPage(QWidget* parent = nullptr);
    void setSecurityStatus(const SecurityStatusUpdate& update);
    void setUpdateCacheResult(const UpdateCacheResult& result);
    void setSecurityBusy(bool busy);
    void setUpdateBusy(bool busy);

signals:
    void refreshSecurityRequested();
    void scanUpdatesRequested();

private:
    QLabel* antivirusValue_ = nullptr;
    QLabel* antivirusDetail_ = nullptr;
    QLabel* firewallValue_ = nullptr;
    QLabel* firewallDetail_ = nullptr;
    QLabel* autoUpdateValue_ = nullptr;
    QLabel* autoUpdateDetail_ = nullptr;
    QLabel* restartValue_ = nullptr;
    QLabel* updateStatus_ = nullptr;
    QTextBrowser* updateTitles_ = nullptr;
    QPushButton* refreshSecurityButton_ = nullptr;
    QPushButton* scanUpdatesButton_ = nullptr;
};

} // namespace Ausyn
