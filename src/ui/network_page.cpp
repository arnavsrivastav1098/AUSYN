#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>
#include <Iphlpapi.h>
#include <Icmpapi.h>
#include <tcpmib.h>
#include <udpmib.h>

#include "network_page.h"

#include <QFutureWatcher>
#include <QElapsedTimer>
#include <QComboBox>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHostAddress>
#include <QHostInfo>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSslSocket>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <limits>
#include <vector>

namespace Ausyn {
namespace {

QFrame* makePanel(QWidget* parent)
{
    auto* frame = new QFrame(parent);
    frame->setObjectName(QStringLiteral("panel"));
    return frame;
}

QLabel* makeValue(QWidget* parent)
{
    auto* label = new QLabel(QStringLiteral("—"), parent);
    label->setStyleSheet(QStringLiteral("color:#c3c2ff;font-size:23px;font-weight:700;"));
    return label;
}

QWidget* makeMetric(const QString& title, QLabel*& value, QWidget* parent)
{
    auto* frame = makePanel(parent);
    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(18, 16, 18, 16);
    layout->setSpacing(8);
    auto* name = new QLabel(title, frame);
    name->setObjectName(QStringLiteral("metricName"));
    value = makeValue(frame);
    layout->addWidget(name);
    layout->addWidget(value);
    return frame;
}

QString formatRate(const std::optional<double>& bytesPerSecond)
{
    if (!bytesPerSecond) return QStringLiteral("Sampling…");
    if (*bytesPerSecond >= 1'000'000.0)
        return QStringLiteral("%1 MB/s").arg(*bytesPerSecond / 1'000'000.0, 0, 'f', 1);
    if (*bytesPerSecond >= 1'000.0)
        return QStringLiteral("%1 KB/s").arg(*bytesPerSecond / 1'000.0, 0, 'f', 0);
    return QStringLiteral("%1 B/s").arg(*bytesPerSecond, 0, 'f', 0);
}

QString formatLinkSpeed(quint64 bitsPerSecond)
{
    if (bitsPerSecond == 0) return QStringLiteral("link speed not reported");
    if (bitsPerSecond >= 1'000'000'000ULL)
        return QStringLiteral("%1 Gbps").arg(static_cast<double>(bitsPerSecond) / 1'000'000'000.0, 0, 'f', 1);
    return QStringLiteral("%1 Mbps").arg(static_cast<double>(bitsPerSecond) / 1'000'000.0, 0, 'f', 0);
}

GatewayProbeResult probeDefaultGateway()
{
    GatewayProbeResult result;
    ULONG adapterBufferSize = 16 * 1024;
    std::vector<std::byte> adapterBuffer(adapterBufferSize);
    auto* adapters = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(adapterBuffer.data());
    ULONG routeStatus = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS, nullptr,
        adapters, &adapterBufferSize);
    if (routeStatus == ERROR_BUFFER_OVERFLOW && adapterBufferSize <= 64 * 1024 * 1024) {
        adapterBuffer.resize(adapterBufferSize);
        adapters = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(adapterBuffer.data());
        routeStatus = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS, nullptr,
            adapters, &adapterBufferSize);
    }
    if (routeStatus != NO_ERROR || !adapters) {
        result.error = QStringLiteral("Windows could not provide the IPv4 route table.");
        return result;
    }

    const IP_ADAPTER_ADDRESSES* selectedAdapter = nullptr;
    const SOCKADDR_IN* selectedGateway = nullptr;
    for (const IP_ADAPTER_ADDRESSES* adapter = adapters; adapter; adapter = adapter->Next) {
        if (adapter->OperStatus != IfOperStatusUp || adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        for (const IP_ADAPTER_GATEWAY_ADDRESS_LH* gateway = adapter->FirstGatewayAddress;
             gateway; gateway = gateway->Next) {
            if (!gateway->Address.lpSockaddr || gateway->Address.lpSockaddr->sa_family != AF_INET) continue;
            const auto* ipv4 = reinterpret_cast<const SOCKADDR_IN*>(gateway->Address.lpSockaddr);
            if (ipv4->sin_addr.s_addr == INADDR_ANY) continue;
            if (!selectedAdapter || adapter->Ipv4Metric < selectedAdapter->Ipv4Metric) {
                selectedAdapter = adapter;
                selectedGateway = ipv4;
            }
        }
    }
    if (!selectedGateway) {
        result.error = QStringLiteral("No IPv4 default gateway is available to check.");
        return result;
    }
    const IPAddr destination = selectedGateway->sin_addr.s_addr;
    IN_ADDR gatewayAddress = selectedGateway->sin_addr;
    std::array<wchar_t, 64> gatewayText{};
    if (InetNtopW(AF_INET, &gatewayAddress, gatewayText.data(), static_cast<DWORD>(gatewayText.size())))
        result.gatewayAddress = QString::fromWCharArray(gatewayText.data());

    HANDLE icmp = IcmpCreateFile();
    if (icmp == INVALID_HANDLE_VALUE) {
        result.error = QStringLiteral("Windows could not open its local ICMP echo service.");
        return result;
    }
    constexpr DWORD kAttempts = 5;
    constexpr DWORD kTimeoutMilliseconds = 600;
    constexpr char kPayload[] = "Ausyn network check";
    std::array<std::byte, sizeof(ICMP_ECHO_REPLY) + sizeof(kPayload) + 16> replyBuffer{};
    quint64 roundTripTotal = 0;
    result.sent = kAttempts;
    for (DWORD attempt = 0; attempt < kAttempts; ++attempt) {
        replyBuffer.fill(std::byte{0});
        const DWORD replies = IcmpSendEcho(icmp, destination,
            const_cast<char*>(kPayload), static_cast<WORD>(sizeof(kPayload) - 1), nullptr,
            replyBuffer.data(), static_cast<DWORD>(replyBuffer.size()), kTimeoutMilliseconds);
        if (replies == 0) continue;
        const auto* echo = reinterpret_cast<const ICMP_ECHO_REPLY*>(replyBuffer.data());
        if (echo->Status != IP_SUCCESS) continue;
        ++result.received;
        roundTripTotal += echo->RoundTripTime;
        if (result.received == 1) {
            result.minimumRoundTripMs = echo->RoundTripTime;
            result.maximumRoundTripMs = echo->RoundTripTime;
        } else {
            result.minimumRoundTripMs = std::min(result.minimumRoundTripMs,
                static_cast<quint32>(echo->RoundTripTime));
            result.maximumRoundTripMs = std::max(result.maximumRoundTripMs,
                static_cast<quint32>(echo->RoundTripTime));
        }
    }
    IcmpCloseHandle(icmp);
    if (result.received > 0)
        result.averageRoundTripMs = static_cast<double>(roundTripTotal) / result.received;
    return result;
}

QString validateEndpointHost(const QString& input)
{
    const QString host = input.trimmed();
    if (host.isEmpty() || host.size() > 253 || host.contains(QLatin1Char('/')) ||
        host.contains(QLatin1Char('@')) || host.contains(QLatin1Char('?')) || host.contains(QLatin1Char('#'))) {
        return QStringLiteral("Enter a host name or IP address only, without a URL path or port.");
    }

    QHostAddress address;
    if (address.setAddress(host)) return {};
    if (host.contains(QLatin1Char(':')))
        return QStringLiteral("Enter an IPv6 address without brackets or a port, or a host name only.");

    const QStringList labels = host.split(QLatin1Char('.'), Qt::KeepEmptyParts);
    for (const QString& label : labels) {
        if (label.isEmpty() || label.size() > 63 || label.startsWith(QLatin1Char('-')) ||
            label.endsWith(QLatin1Char('-'))) {
            return QStringLiteral("That host name does not look valid.");
        }
        for (const QChar character : label) {
            if (!character.isLetterOrNumber() && character != QLatin1Char('-'))
                return QStringLiteral("Enter a host name or IP address only, without a URL path or port.");
        }
    }
    return {};
}

SecureEndpointResult probeSecureEndpoint(const QString& host)
{
    SecureEndpointResult result;
    result.host = host;
    if (!QSslSocket::supportsSsl()) {
        result.error = QStringLiteral("No supported TLS backend is available in this Ausyn installation.");
        return result;
    }

    QElapsedTimer timer;
    timer.start();
    const QHostInfo hostInfo = QHostInfo::fromName(host);
    result.dnsMilliseconds = timer.elapsed();
    if (hostInfo.error() != QHostInfo::NoError || hostInfo.addresses().isEmpty()) {
        result.error = QStringLiteral("DNS lookup failed: %1")
            .arg(hostInfo.errorString().isEmpty() ? QStringLiteral("Windows returned no addresses.")
                                                   : hostInfo.errorString());
        return result;
    }
    result.dnsSucceeded = true;
    result.address = hostInfo.addresses().first().toString();

    QSslSocket socket;
    socket.setPeerVerifyMode(QSslSocket::VerifyPeer);
    timer.restart();
    socket.connectToHostEncrypted(host, 443);
    result.tlsSucceeded = socket.waitForEncrypted(8000);
    result.connectionMilliseconds = timer.elapsed();
    if (!socket.peerAddress().isNull()) result.address = socket.peerAddress().toString();
    if (!result.tlsSucceeded) {
        result.error = socket.errorString();
        const auto sslErrors = socket.sslHandshakeErrors();
        if (!sslErrors.isEmpty()) result.error = sslErrors.first().errorString();
    }
    socket.abort();
    return result;
}

template<typename Query>
DWORD readOwnerTable(Query&& query, std::vector<std::byte>& buffer)
{
    DWORD bytesNeeded = 0;
    DWORD status = query(nullptr, &bytesNeeded);
    if (status != ERROR_INSUFFICIENT_BUFFER && status != NO_ERROR) return status;
    if (bytesNeeded == 0) return NO_ERROR;
    constexpr DWORD kMaximumTableBytes = 64 * 1024 * 1024;
    if (bytesNeeded > kMaximumTableBytes) return ERROR_NOT_ENOUGH_MEMORY;
    buffer.resize(bytesNeeded);
    status = query(buffer.data(), &bytesNeeded);
    if (status == NO_ERROR && bytesNeeded > buffer.size()) return ERROR_INSUFFICIENT_BUFFER;
    return status;
}

QString ipv4Text(DWORD addressValue)
{
    IN_ADDR address{};
    address.S_un.S_addr = addressValue;
    std::array<wchar_t, INET_ADDRSTRLEN> text{};
    return InetNtopW(AF_INET, &address, text.data(), static_cast<DWORD>(text.size()))
        ? QString::fromWCharArray(text.data()) : QStringLiteral("address unavailable");
}

QString ipv6Text(const UCHAR* addressBytes, DWORD scopeId)
{
    IN6_ADDR address{};
    std::memcpy(&address, addressBytes, sizeof(address));
    std::array<wchar_t, INET6_ADDRSTRLEN> text{};
    QString output = InetNtopW(AF_INET6, &address, text.data(), static_cast<DWORD>(text.size()))
        ? QString::fromWCharArray(text.data()) : QStringLiteral("address unavailable");
    if (scopeId != 0) output += QStringLiteral("%%%1").arg(scopeId);
    return output;
}

QString tcpStateText(DWORD state)
{
    switch (state) {
    case MIB_TCP_STATE_CLOSED: return QStringLiteral("CLOSED");
    case MIB_TCP_STATE_LISTEN: return QStringLiteral("LISTEN");
    case MIB_TCP_STATE_SYN_SENT: return QStringLiteral("SYN_SENT");
    case MIB_TCP_STATE_SYN_RCVD: return QStringLiteral("SYN_RECEIVED");
    case MIB_TCP_STATE_ESTAB: return QStringLiteral("ESTABLISHED");
    case MIB_TCP_STATE_FIN_WAIT1: return QStringLiteral("FIN_WAIT_1");
    case MIB_TCP_STATE_FIN_WAIT2: return QStringLiteral("FIN_WAIT_2");
    case MIB_TCP_STATE_CLOSE_WAIT: return QStringLiteral("CLOSE_WAIT");
    case MIB_TCP_STATE_CLOSING: return QStringLiteral("CLOSING");
    case MIB_TCP_STATE_LAST_ACK: return QStringLiteral("LAST_ACK");
    case MIB_TCP_STATE_TIME_WAIT: return QStringLiteral("TIME_WAIT");
    case MIB_TCP_STATE_DELETE_TCB: return QStringLiteral("DELETE_TCB");
    default: return QStringLiteral("STATE_%1").arg(state);
    }
}

ProcessConnectionResult readProcessConnections(quint32 processId, const QString& expectedName)
{
    ProcessConnectionResult result;
    result.processId = processId;
    result.processName = expectedName;
    result.capturedAt = QDateTime::currentDateTime();
    if (processId <= 4) {
        result.error = QStringLiteral("Ausyn does not inspect core Windows process endpoints from this screen.");
        return result;
    }

    HANDLE processHandle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!processHandle) {
        result.error = QStringLiteral("Windows could not confirm that this process is still running. Refresh the process list and try again.");
        return result;
    }
    std::array<wchar_t, 32768> imagePath{};
    DWORD imagePathLength = static_cast<DWORD>(imagePath.size());
    const BOOL pathRead = QueryFullProcessImageNameW(processHandle, 0, imagePath.data(), &imagePathLength);
    CloseHandle(processHandle);
    if (!pathRead) {
        result.error = QStringLiteral("Windows could not confirm this process identity. Refresh the process list and try again.");
        return result;
    }
    const QString actualName = QFileInfo(QString::fromWCharArray(imagePath.data(),
        static_cast<qsizetype>(imagePathLength))).fileName();
    if (actualName.compare(expectedName, Qt::CaseInsensitive) != 0) {
        result.error = QStringLiteral("This process ID now belongs to a different executable. Refresh the process list before inspecting connections.");
        return result;
    }
    result.processIdentityVerified = true;

    QStringList queryWarnings;
    std::vector<std::byte> tcp4Buffer;
    const DWORD tcp4Status = readOwnerTable([&](PVOID table, PDWORD size) {
        return GetExtendedTcpTable(table, size, TRUE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);
    }, tcp4Buffer);
    if (tcp4Status == NO_ERROR && !tcp4Buffer.empty()) {
        const auto* table = reinterpret_cast<const MIB_TCPTABLE_OWNER_PID*>(tcp4Buffer.data());
        for (DWORD index = 0; index < table->dwNumEntries; ++index) {
            const MIB_TCPROW_OWNER_PID& row = table->table[index];
            if (row.dwOwningPid != processId) continue;
            ++result.tcpTotal;
            if (result.tcpEndpoints.size() < 50) {
                result.tcpEndpoints << QStringLiteral("%1:%2 → %3:%4 · %5 · IPv4")
                    .arg(ipv4Text(row.dwLocalAddr))
                    .arg(ntohs(static_cast<u_short>(row.dwLocalPort)))
                    .arg(ipv4Text(row.dwRemoteAddr))
                    .arg(ntohs(static_cast<u_short>(row.dwRemotePort)))
                    .arg(tcpStateText(row.dwState));
            }
        }
    } else if (tcp4Status != NO_ERROR) {
        queryWarnings << QStringLiteral("IPv4 TCP table error %1").arg(tcp4Status);
    }

    std::vector<std::byte> tcp6Buffer;
    const DWORD tcp6Status = readOwnerTable([&](PVOID table, PDWORD size) {
        return GetExtendedTcpTable(table, size, TRUE, AF_INET6, TCP_TABLE_OWNER_PID_ALL, 0);
    }, tcp6Buffer);
    if (tcp6Status == NO_ERROR && !tcp6Buffer.empty()) {
        const auto* table = reinterpret_cast<const MIB_TCP6TABLE_OWNER_PID*>(tcp6Buffer.data());
        for (DWORD index = 0; index < table->dwNumEntries; ++index) {
            const MIB_TCP6ROW_OWNER_PID& row = table->table[index];
            if (row.dwOwningPid != processId) continue;
            ++result.tcpTotal;
            if (result.tcpEndpoints.size() < 50) {
                result.tcpEndpoints << QStringLiteral("[%1]:%2 → [%3]:%4 · %5 · IPv6")
                    .arg(ipv6Text(row.ucLocalAddr, row.dwLocalScopeId))
                    .arg(ntohs(static_cast<u_short>(row.dwLocalPort)))
                    .arg(ipv6Text(row.ucRemoteAddr, row.dwRemoteScopeId))
                    .arg(ntohs(static_cast<u_short>(row.dwRemotePort)))
                    .arg(tcpStateText(row.dwState));
            }
        }
    } else if (tcp6Status != NO_ERROR) {
        queryWarnings << QStringLiteral("IPv6 TCP table error %1").arg(tcp6Status);
    }

    std::vector<std::byte> udp4Buffer;
    const DWORD udp4Status = readOwnerTable([&](PVOID table, PDWORD size) {
        return GetExtendedUdpTable(table, size, TRUE, AF_INET, UDP_TABLE_OWNER_PID, 0);
    }, udp4Buffer);
    if (udp4Status == NO_ERROR && !udp4Buffer.empty()) {
        const auto* table = reinterpret_cast<const MIB_UDPTABLE_OWNER_PID*>(udp4Buffer.data());
        for (DWORD index = 0; index < table->dwNumEntries; ++index) {
            const MIB_UDPROW_OWNER_PID& row = table->table[index];
            if (row.dwOwningPid != processId) continue;
            ++result.udpTotal;
            if (result.udpEndpoints.size() < 50) {
                result.udpEndpoints << QStringLiteral("%1:%2 · IPv4 (local endpoint)")
                    .arg(ipv4Text(row.dwLocalAddr))
                    .arg(ntohs(static_cast<u_short>(row.dwLocalPort)));
            }
        }
    } else if (udp4Status != NO_ERROR) {
        queryWarnings << QStringLiteral("IPv4 UDP table error %1").arg(udp4Status);
    }

    std::vector<std::byte> udp6Buffer;
    const DWORD udp6Status = readOwnerTable([&](PVOID table, PDWORD size) {
        return GetExtendedUdpTable(table, size, TRUE, AF_INET6, UDP_TABLE_OWNER_PID, 0);
    }, udp6Buffer);
    if (udp6Status == NO_ERROR && !udp6Buffer.empty()) {
        const auto* table = reinterpret_cast<const MIB_UDP6TABLE_OWNER_PID*>(udp6Buffer.data());
        for (DWORD index = 0; index < table->dwNumEntries; ++index) {
            const MIB_UDP6ROW_OWNER_PID& row = table->table[index];
            if (row.dwOwningPid != processId) continue;
            ++result.udpTotal;
            if (result.udpEndpoints.size() < 50) {
                result.udpEndpoints << QStringLiteral("[%1]:%2 · IPv6 (local endpoint)")
                    .arg(ipv6Text(row.ucLocalAddr, row.dwLocalScopeId))
                    .arg(ntohs(static_cast<u_short>(row.dwLocalPort)));
            }
        }
    } else if (udp6Status != NO_ERROR) {
        queryWarnings << QStringLiteral("IPv6 UDP table error %1").arg(udp6Status);
    }

    result.endpointTablesAvailable = tcp4Status == NO_ERROR || tcp6Status == NO_ERROR ||
        udp4Status == NO_ERROR || udp6Status == NO_ERROR;

    if (queryWarnings.size() == 4) {
        result.error = QStringLiteral("Windows could not read the TCP or UDP endpoint tables: %1")
            .arg(queryWarnings.join(QStringLiteral("; ")));
    } else if (!queryWarnings.isEmpty()) {
        result.error = QStringLiteral("Some endpoint tables were unavailable: %1")
            .arg(queryWarnings.join(QStringLiteral("; ")));
    }
    result.tcpEndpoints.sort(Qt::CaseInsensitive);
    result.udpEndpoints.sort(Qt::CaseInsensitive);
    return result;
}

} // namespace

NetworkPage::NetworkPage(QWidget* parent)
    : QWidget(parent)
{
    auto* pageLayout = new QVBoxLayout(this);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* content = new QWidget(scroll);
    scroll->setWidget(content);
    pageLayout->addWidget(scroll);

    auto* outer = new QVBoxLayout(content);
    outer->setContentsMargins(30, 26, 30, 30);
    outer->setSpacing(16);
    auto* eyebrow = new QLabel(QStringLiteral("NETWORK & CONNECTIVITY"), content);
    eyebrow->setObjectName(QStringLiteral("eyebrow"));
    auto* heading = new QLabel(QStringLiteral("Network activity"), content);
    heading->setObjectName(QStringLiteral("heroTitle"));
    auto* intro = new QLabel(QStringLiteral("See live adapter throughput and check the local gateway when you choose. A gateway check measures your local path, not end-to-end Internet latency."), content);
    intro->setObjectName(QStringLiteral("heroBody"));
    intro->setWordWrap(true);
    outer->addWidget(eyebrow);
    outer->addWidget(heading);
    outer->addWidget(intro);

    auto* metrics = new QGridLayout;
    metrics->setHorizontalSpacing(12);
    metrics->setVerticalSpacing(12);
    metrics->addWidget(makeMetric(QStringLiteral("Download rate"), download_, content), 0, 0);
    metrics->addWidget(makeMetric(QStringLiteral("Upload rate"), upload_, content), 0, 1);
    outer->addLayout(metrics);

    auto* adapterPanel = makePanel(content);
    auto* adapterLayout = new QVBoxLayout(adapterPanel);
    adapterLayout->setContentsMargins(19, 17, 19, 17);
    adapterLayout->setSpacing(8);
    auto* adapterTitle = new QLabel(QStringLiteral("Active network adapters"), adapterPanel);
    adapterTitle->setObjectName(QStringLiteral("panelTitle"));
    adapterDetails_ = new QLabel(QStringLiteral("Waiting for Windows adapter data…"), adapterPanel);
    adapterDetails_->setObjectName(QStringLiteral("subtle"));
    adapterDetails_->setWordWrap(true);
    adapterLayout->addWidget(adapterTitle);
    adapterLayout->addWidget(adapterDetails_);
    outer->addWidget(adapterPanel);

    auto* connectionPanel = makePanel(content);
    auto* connectionLayout = new QVBoxLayout(connectionPanel);
    connectionLayout->setContentsMargins(19, 17, 19, 17);
    connectionLayout->setSpacing(8);
    auto* connectionTitle = new QLabel(QStringLiteral("Current connection activity"), connectionPanel);
    connectionTitle->setObjectName(QStringLiteral("panelTitle"));
    connectionDetails_ = new QLabel(QStringLiteral("Waiting for Windows connection tables…"), connectionPanel);
    connectionDetails_->setObjectName(QStringLiteral("subtle"));
    connectionDetails_->setWordWrap(true);
    connectionLayout->addWidget(connectionTitle);
    connectionLayout->addWidget(connectionDetails_);
    outer->addWidget(connectionPanel);

    auto* gatewayPanel = makePanel(content);
    auto* gatewayLayout = new QVBoxLayout(gatewayPanel);
    gatewayLayout->setContentsMargins(19, 17, 19, 17);
    gatewayLayout->setSpacing(10);
    auto* gatewayTitle = new QLabel(QStringLiteral("Local gateway latency and echo response"), gatewayPanel);
    gatewayTitle->setObjectName(QStringLiteral("panelTitle"));
    gatewayResult_ = new QLabel(QStringLiteral("No check has been run. Ausyn sends five small ICMP echo requests only when you press the button."), gatewayPanel);
    gatewayResult_->setObjectName(QStringLiteral("subtle"));
    gatewayResult_->setWordWrap(true);
    gatewayButton_ = new QPushButton(QStringLiteral("Check local gateway"), gatewayPanel);
    gatewayButton_->setObjectName(QStringLiteral("secondaryButton"));
    gatewayButton_->setCursor(Qt::PointingHandCursor);
    gatewayLayout->addWidget(gatewayTitle);
    gatewayLayout->addWidget(gatewayResult_);
    gatewayLayout->addWidget(gatewayButton_, 0, Qt::AlignLeft);
    outer->addWidget(gatewayPanel);

    auto* endpointPanel = makePanel(content);
    auto* endpointLayout = new QVBoxLayout(endpointPanel);
    endpointLayout->setContentsMargins(19, 17, 19, 17);
    endpointLayout->setSpacing(9);
    auto* endpointTitle = new QLabel(QStringLiteral("Secure Internet endpoint check"), endpointPanel);
    endpointTitle->setObjectName(QStringLiteral("panelTitle"));
    auto* endpointNote = new QLabel(QStringLiteral(
        "On demand: looks up the host and checks a verified TLS connection on port 443. Ausyn sends no web-page request or content, and does not save the result."), endpointPanel);
    endpointNote->setObjectName(QStringLiteral("subtle"));
    endpointNote->setWordWrap(true);
    auto* endpointControls = new QHBoxLayout;
    endpointHost_ = new QLineEdit(endpointPanel);
    endpointHost_->setObjectName(QStringLiteral("searchField"));
    endpointHost_->setPlaceholderText(QStringLiteral("Host name, for example www.microsoft.com"));
    endpointHost_->setText(QStringLiteral("www.microsoft.com"));
    endpointHost_->setClearButtonEnabled(true);
    endpointButton_ = new QPushButton(QStringLiteral("Check secure connection"), endpointPanel);
    endpointButton_->setObjectName(QStringLiteral("secondaryButton"));
    endpointButton_->setCursor(Qt::PointingHandCursor);
    endpointControls->addWidget(endpointHost_, 1);
    endpointControls->addWidget(endpointButton_);
    endpointResult_ = new QLabel(QStringLiteral("No endpoint check has been run."), endpointPanel);
    endpointResult_->setObjectName(QStringLiteral("subtle"));
    endpointResult_->setWordWrap(true);
    endpointLayout->addWidget(endpointTitle);
    endpointLayout->addWidget(endpointNote);
    endpointLayout->addLayout(endpointControls);
    endpointLayout->addWidget(endpointResult_);
    outer->addWidget(endpointPanel);

    auto* processConnectionsPanel = makePanel(content);
    auto* processConnectionsLayout = new QVBoxLayout(processConnectionsPanel);
    processConnectionsLayout->setContentsMargins(19, 17, 19, 17);
    processConnectionsLayout->setSpacing(9);
    auto* processConnectionsTitle = new QLabel(QStringLiteral("Inspect a process’s current network endpoints"), processConnectionsPanel);
    processConnectionsTitle->setObjectName(QStringLiteral("panelTitle"));
    auto* processConnectionsNote = new QLabel(QStringLiteral(
        "Read-only Windows connection tables. TCP remote addresses can reveal services the app is contacting; UDP rows show local endpoints only. Nothing is saved or sent to cloud AI."), processConnectionsPanel);
    processConnectionsNote->setObjectName(QStringLiteral("subtle"));
    processConnectionsNote->setWordWrap(true);
    auto* processConnectionControls = new QHBoxLayout;
    connectionProcess_ = new QComboBox(processConnectionsPanel);
    connectionProcess_->setObjectName(QStringLiteral("searchField"));
    connectionProcess_->setMinimumWidth(220);
    inspectConnectionsButton_ = new QPushButton(QStringLiteral("Inspect connections"), processConnectionsPanel);
    inspectConnectionsButton_->setObjectName(QStringLiteral("secondaryButton"));
    inspectConnectionsButton_->setEnabled(false);
    processConnectionControls->addWidget(connectionProcess_, 1);
    processConnectionControls->addWidget(inspectConnectionsButton_);
    processConnectionsResult_ = new QTextBrowser(processConnectionsPanel);
    processConnectionsResult_->setObjectName(QStringLiteral("subtle"));
    processConnectionsResult_->setReadOnly(true);
    processConnectionsResult_->setOpenLinks(false);
    processConnectionsResult_->setMaximumHeight(230);
    processConnectionsResult_->setPlainText(QStringLiteral("Choose a running process and inspect its current endpoints when you need them."));
    processConnectionsLayout->addWidget(processConnectionsTitle);
    processConnectionsLayout->addWidget(processConnectionsNote);
    processConnectionsLayout->addLayout(processConnectionControls);
    processConnectionsLayout->addWidget(processConnectionsResult_);
    outer->addWidget(processConnectionsPanel);
    outer->addStretch(1);

    gatewayWatcher_ = new QFutureWatcher<GatewayProbeResult>(this);
    endpointWatcher_ = new QFutureWatcher<SecureEndpointResult>(this);
    processConnectionsWatcher_ = new QFutureWatcher<ProcessConnectionResult>(this);
    connect(gatewayButton_, &QPushButton::clicked, this, &NetworkPage::runGatewayCheck);
    connect(endpointButton_, &QPushButton::clicked, this, &NetworkPage::runSecureEndpointCheck);
    connect(inspectConnectionsButton_, &QPushButton::clicked, this, &NetworkPage::inspectProcessConnections);
    connect(gatewayWatcher_, &QFutureWatcher<GatewayProbeResult>::finished, this, [this] {
        const GatewayProbeResult result = gatewayWatcher_->result();
        gatewayButton_->setEnabled(true);
        gatewayButton_->setText(QStringLiteral("Check local gateway"));
        if (!result.error.isEmpty()) {
            gatewayResult_->setText(result.error);
            return;
        }
        const double noReplyPercent = result.sent == 0 ? 0.0
            : 100.0 * static_cast<double>(result.sent - result.received) / result.sent;
        if (result.received == 0) {
            gatewayResult_->setText(QStringLiteral("Gateway %1 replied to 0 of %2 echo requests. ICMP may be filtered by the gateway; this result does not prove the Internet connection is down.")
                .arg(result.gatewayAddress, QString::number(result.sent)));
            return;
        }
        gatewayResult_->setText(QStringLiteral("Gateway %1 · %2/%3 replies · average %4 ms (min %5, max %6) · %7% did not reply. Missing echo replies can reflect ICMP filtering, not necessarily packet loss on other traffic.")
            .arg(result.gatewayAddress)
            .arg(result.received).arg(result.sent)
            .arg(result.averageRoundTripMs, 0, 'f', 1)
            .arg(result.minimumRoundTripMs).arg(result.maximumRoundTripMs)
            .arg(noReplyPercent, 0, 'f', 0));
    });
    connect(endpointWatcher_, &QFutureWatcher<SecureEndpointResult>::finished, this, [this] {
        const SecureEndpointResult result = endpointWatcher_->result();
        endpointButton_->setEnabled(true);
        endpointButton_->setText(QStringLiteral("Check secure connection"));
        if (!result.dnsSucceeded) {
            endpointResult_->setText(QStringLiteral("%1 · %2")
                .arg(result.host, result.error));
            return;
        }
        if (!result.tlsSucceeded) {
            endpointResult_->setText(QStringLiteral("DNS resolved %1 to %2 in %3 ms, but the verified TLS connection failed: %4")
                .arg(result.host, result.address)
                .arg(result.dnsMilliseconds)
                .arg(result.error));
            return;
        }
        endpointResult_->setText(QStringLiteral("Secure TLS handshake verified for %1 · %2 · DNS %3 ms · connection setup %4 ms. Ausyn closed the connection without requesting a page.")
            .arg(result.host, result.address)
            .arg(result.dnsMilliseconds)
            .arg(result.connectionMilliseconds));
    });
    connect(processConnectionsWatcher_, &QFutureWatcher<ProcessConnectionResult>::finished, this, [this] {
        const ProcessConnectionResult result = processConnectionsWatcher_->result();
        connectionProcess_->setEnabled(connectionProcess_->count() > 0);
        inspectConnectionsButton_->setEnabled(connectionProcess_->count() > 0);
        inspectConnectionsButton_->setText(QStringLiteral("Inspect connections"));
        if (!result.processIdentityVerified || !result.endpointTablesAvailable) {
            processConnectionsResult_->setPlainText(result.error.isEmpty()
                ? QStringLiteral("Windows did not provide usable endpoint tables for this process.")
                : result.error);
            return;
        }

        QStringList lines;
        lines << QStringLiteral("%1 · PID %2 · checked %3 · %4 TCP endpoint(s), %5 UDP local endpoint(s)")
            .arg(result.processName).arg(result.processId)
            .arg(result.capturedAt.toLocalTime().toString(QStringLiteral("h:mm:ss ap")))
            .arg(result.tcpTotal).arg(result.udpTotal);
        if (!result.error.isEmpty()) lines << result.error;
        lines << QStringLiteral("TCP endpoints");
        if (result.tcpEndpoints.isEmpty()) lines << QStringLiteral("  None observed in the current Windows tables.");
        else for (const QString& endpoint : result.tcpEndpoints) lines << QStringLiteral("  %1").arg(endpoint);
        if (result.tcpTotal > static_cast<quint64>(result.tcpEndpoints.size()))
            lines << QStringLiteral("  Showing %1 of %2 TCP entries.").arg(result.tcpEndpoints.size()).arg(result.tcpTotal);
        lines << QStringLiteral("UDP local endpoints");
        if (result.udpEndpoints.isEmpty()) lines << QStringLiteral("  None observed in the current Windows tables.");
        else for (const QString& endpoint : result.udpEndpoints) lines << QStringLiteral("  %1").arg(endpoint);
        if (result.udpTotal > static_cast<quint64>(result.udpEndpoints.size()))
            lines << QStringLiteral("  Showing %1 of %2 UDP entries.").arg(result.udpEndpoints.size()).arg(result.udpTotal);
        processConnectionsResult_->setPlainText(lines.join(QLatin1Char('\n')));
    });
}

void NetworkPage::setSnapshot(const SystemSnapshot& snapshot)
{
    download_->setText(formatRate(snapshot.networkReceiveBytesPerSecond));
    upload_->setText(formatRate(snapshot.networkSendBytesPerSecond));

    QStringList adapters;
    for (const NetworkAdapterSample& adapter : snapshot.activeNetworkAdapters) {
        adapters << QStringLiteral("%1 · %2 · receive link %3 · transmit link %4")
            .arg(adapter.name.isEmpty() ? QStringLiteral("Network adapter") : adapter.name,
                 adapter.description.isEmpty() ? QStringLiteral("Description not reported") : adapter.description,
                 formatLinkSpeed(adapter.receiveLinkSpeedBitsPerSecond),
                 formatLinkSpeed(adapter.transmitLinkSpeedBitsPerSecond));
    }
    adapterDetails_->setText(adapters.isEmpty()
        ? QStringLiteral("No active physical network adapters are currently reported.")
        : adapters.join(QLatin1Char('\n')) + QStringLiteral("\nRates above combine active hardware adapters; virtual, tunnel, and loopback interfaces are excluded."));

    if (snapshot.networkTcpEntryCount && snapshot.networkUdpEndpointCount) {
        connectionDetails_->setText(QStringLiteral("%1 TCP table entries · %2 UDP endpoints. TCP entries include listening sockets. Ausyn shows counts here; remote addresses and traffic contents are not collected.")
            .arg(*snapshot.networkTcpEntryCount).arg(*snapshot.networkUdpEndpointCount));
    } else {
        connectionDetails_->setText(QStringLiteral("Windows did not provide one or more IPv4/IPv6 connection tables."));
    }

    if (connectionProcess_) {
        const quint32 selectedPid = connectionProcess_->currentData().toUInt();
        connectionProcess_->blockSignals(true);
        connectionProcess_->clear();
        for (const ProcessSample& process : snapshot.topProcesses) {
            const QString label = QStringLiteral("%1 · PID %2").arg(process.name).arg(process.processId);
            connectionProcess_->addItem(label, process.processId);
            connectionProcess_->setItemData(connectionProcess_->count() - 1, process.name, Qt::UserRole + 1);
            if (process.processId == selectedPid) connectionProcess_->setCurrentIndex(connectionProcess_->count() - 1);
        }
        connectionProcess_->blockSignals(false);
        const bool hasProcesses = connectionProcess_->count() > 0;
        connectionProcess_->setEnabled(hasProcesses && !processConnectionsWatcher_->isRunning());
        inspectConnectionsButton_->setEnabled(hasProcesses && !processConnectionsWatcher_->isRunning());
    }
}

void NetworkPage::runGatewayCheck()
{
    if (!gatewayWatcher_ || gatewayWatcher_->isRunning()) return;
    gatewayButton_->setEnabled(false);
    gatewayButton_->setText(QStringLiteral("Checking gateway…"));
    gatewayResult_->setText(QStringLiteral("Measuring the local gateway path. This can take a few seconds if echo replies are filtered."));
    gatewayWatcher_->setFuture(QtConcurrent::run(probeDefaultGateway));
}

void NetworkPage::runSecureEndpointCheck()
{
    if (!endpointWatcher_ || endpointWatcher_->isRunning()) return;
    const QString host = endpointHost_ ? endpointHost_->text().trimmed() : QString{};
    const QString validationError = validateEndpointHost(host);
    if (!validationError.isEmpty()) {
        endpointResult_->setText(validationError);
        return;
    }
    endpointButton_->setEnabled(false);
    endpointButton_->setText(QStringLiteral("Checking endpoint…"));
    endpointResult_->setText(QStringLiteral("Looking up the host and verifying its TLS certificate…"));
    endpointWatcher_->setFuture(QtConcurrent::run([host] { return probeSecureEndpoint(host); }));
}

void NetworkPage::inspectProcessConnections()
{
    if (!processConnectionsWatcher_ || processConnectionsWatcher_->isRunning() || !connectionProcess_) return;
    bool pidOk = false;
    const quint32 processId = connectionProcess_->currentData().toUInt(&pidOk);
    const QString expectedName = connectionProcess_->currentData(Qt::UserRole + 1).toString();
    if (!pidOk || processId == 0 || expectedName.isEmpty()) {
        processConnectionsResult_->setPlainText(QStringLiteral("Choose a readable process from the current list first."));
        return;
    }
    connectionProcess_->setEnabled(false);
    inspectConnectionsButton_->setEnabled(false);
    inspectConnectionsButton_->setText(QStringLiteral("Reading Windows tables…"));
    processConnectionsResult_->setPlainText(QStringLiteral("Confirming process identity and reading current TCP/UDP endpoint tables…"));
    processConnectionsWatcher_->setFuture(QtConcurrent::run([processId, expectedName] {
        return readProcessConnections(processId, expectedName);
    }));
}

} // namespace Ausyn
