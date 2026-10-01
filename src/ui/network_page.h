#pragma once

#include "../monitoring/system_snapshot.h"

#include <QWidget>
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QtGlobal>

class QLabel;
class QLineEdit;
class QComboBox;
class QPushButton;
class QTextBrowser;
template <typename T> class QFutureWatcher;

namespace Ausyn {

struct GatewayProbeResult {
    QString gatewayAddress;
    quint32 sent = 0;
    quint32 received = 0;
    double averageRoundTripMs = 0.0;
    quint32 minimumRoundTripMs = 0;
    quint32 maximumRoundTripMs = 0;
    QString error;
};

struct SecureEndpointResult {
    QString host;
    QString address;
    QString error;
    qint64 dnsMilliseconds = 0;
    qint64 connectionMilliseconds = 0;
    bool dnsSucceeded = false;
    bool tlsSucceeded = false;
};

struct ProcessConnectionResult {
    quint32 processId = 0;
    QString processName;
    QString error;
    QStringList tcpEndpoints;
    QStringList udpEndpoints;
    QDateTime capturedAt;
    quint64 tcpTotal = 0;
    quint64 udpTotal = 0;
    bool processIdentityVerified = false;
    bool endpointTablesAvailable = false;
};

class NetworkPage final : public QWidget {
    Q_OBJECT
public:
    explicit NetworkPage(QWidget* parent = nullptr);
    void setSnapshot(const SystemSnapshot& snapshot);

private:
    void runGatewayCheck();
    void runSecureEndpointCheck();
    void inspectProcessConnections();

    QLabel* download_ = nullptr;
    QLabel* upload_ = nullptr;
    QLabel* adapterDetails_ = nullptr;
    QLabel* connectionDetails_ = nullptr;
    QLabel* gatewayResult_ = nullptr;
    QPushButton* gatewayButton_ = nullptr;
    QFutureWatcher<GatewayProbeResult>* gatewayWatcher_ = nullptr;
    QLineEdit* endpointHost_ = nullptr;
    QLabel* endpointResult_ = nullptr;
    QPushButton* endpointButton_ = nullptr;
    QFutureWatcher<SecureEndpointResult>* endpointWatcher_ = nullptr;
    QComboBox* connectionProcess_ = nullptr;
    QPushButton* inspectConnectionsButton_ = nullptr;
    QTextBrowser* processConnectionsResult_ = nullptr;
    QFutureWatcher<ProcessConnectionResult>* processConnectionsWatcher_ = nullptr;
};

} // namespace Ausyn
