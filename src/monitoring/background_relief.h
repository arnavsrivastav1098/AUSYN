#pragma once
#include "system_snapshot.h"
#include <QObject>
class QTimer;
namespace Ausyn {
class BackgroundRelief final : public QObject {
    Q_OBJECT
public:
    explicit BackgroundRelief(QObject* parent = nullptr);
    ~BackgroundRelief() override;
    bool apply(const ProcessSample& process, const SystemSnapshot& snapshot, const QString& keptApp, QString* error);
    void restore();
    void guard(const QString& keptApp, const QString& foregroundApp = {});
    [[nodiscard]] bool active() const { return process_ != nullptr; }
    [[nodiscard]] QString status() const { return status_; }
signals:
    void changed(QString status);
private:
    void* process_ = nullptr;
    quint32 pid_ = 0;
    QString name_, status_;
    QTimer* expiry_;
};
}
