#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <winevt.h>

#include "event_log_collector.h"

#include <QHash>
#include <QStringList>
#include <QXmlStreamReader>

#include <algorithm>
#include <array>
#include <string>
#include <utility>
#include <vector>

#pragma comment(lib, "Wevtapi.lib")

namespace Ausyn {
namespace {

class EventHandle final {
public:
    explicit EventHandle(EVT_HANDLE handle = nullptr) noexcept : handle_(handle) {}
    ~EventHandle() { if (handle_) EvtClose(handle_); }
    EventHandle(const EventHandle&) = delete;
    EventHandle& operator=(const EventHandle&) = delete;
    [[nodiscard]] EVT_HANDLE get() const noexcept { return handle_; }
private:
    EVT_HANDLE handle_ = nullptr;
};

struct EventKey {
    QString channel;
    QString provider;
    quint32 id = 0;
    QString discriminator;

    [[nodiscard]] QString hashKey() const
    {
        return channel + QLatin1Char('|') + provider + QLatin1Char('|') + QString::number(id) +
            QLatin1Char('|') + discriminator;
    }
};

struct ParsedEvent {
    QString provider;
    QString timestamp;
    quint32 id = 0;
    quint32 level = 0;
    bool hasId = false;
    QHash<QString, QString> eventData;
};

ParsedEvent parseEventXml(const QString& xml)
{
    ParsedEvent event;
    QXmlStreamReader reader(xml);
    while (!reader.atEnd()) {
        reader.readNext();
        if (!reader.isStartElement()) continue;
        const QStringView name = reader.name();
        if (name == QLatin1String("Provider")) {
            event.provider = reader.attributes().value(QLatin1String("Name")).toString();
        } else if (name == QLatin1String("TimeCreated")) {
            event.timestamp = reader.attributes().value(QLatin1String("SystemTime")).toString();
        } else if (name == QLatin1String("EventID") || name == QLatin1String("Level")) {
            bool ok = false;
            const quint32 value = reader.readElementText(QXmlStreamReader::SkipChildElements).toUInt(&ok);
            if (!ok) continue;
            if (name == QLatin1String("EventID")) {
                event.id = value;
                event.hasId = true;
            } else {
                event.level = value;
            }
        } else if (name == QLatin1String("Data")) {
            const QString fieldName = reader.attributes().value(QLatin1String("Name")).toString();
            const QString value = reader.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
            if (!fieldName.isEmpty() && !value.isEmpty() && value.size() <= 512)
                event.eventData.insert(fieldName, value);
        }
    }
    if (reader.hasError() || event.provider.isEmpty()) return {};
    return event;
}

QString formatEventMessage(EVT_HANDLE event, const QString& provider)
{
    const std::wstring publisherName = provider.toStdWString();
    EventHandle metadata(EvtOpenPublisherMetadata(nullptr, publisherName.c_str(), nullptr, 0, 0));
    if (!metadata.get()) return {};

    DWORD required = 0;
    EvtFormatMessage(metadata.get(), event, 0, 0, nullptr, EvtFormatMessageEvent,
                     0, nullptr, &required);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || required == 0 || required > 32'768) return {};

    std::vector<wchar_t> buffer(required + 1, L'\0');
    DWORD used = 0;
    if (!EvtFormatMessage(metadata.get(), event, 0, 0, nullptr, EvtFormatMessageEvent,
                          static_cast<DWORD>(buffer.size()), buffer.data(), &used)) {
        return {};
    }
    return QString::fromWCharArray(buffer.data(), static_cast<qsizetype>(used)).trimmed().left(1000);
}

QString severityName(EventSeverity severity)
{
    switch (severity) {
    case EventSeverity::Critical: return QStringLiteral("Critical");
    case EventSeverity::Error: return QStringLiteral("Error");
    case EventSeverity::Warning: return QStringLiteral("Warning");
    }
    return QStringLiteral("Unknown");
}

QString extractFaultingApplication(const QString& message)
{
    constexpr auto label = "Faulting application name:";
    const qsizetype labelPosition = message.indexOf(QLatin1String(label), Qt::CaseInsensitive);
    if (labelPosition < 0) return {};

    const qsizetype valueStart = labelPosition + static_cast<qsizetype>(sizeof(label) - 1);
    qsizetype valueEnd = message.indexOf(QLatin1Char('\n'), valueStart);
    if (valueEnd < 0) valueEnd = message.size();
    const qsizetype separator = message.indexOf(QLatin1Char(';'), valueStart);
    if (separator >= 0 && separator < valueEnd) valueEnd = separator;
    return message.mid(valueStart, valueEnd - valueStart).trimmed().left(160);
}

void collectChannel(const QString& channel, QHash<QString, EventInsight>& grouped,
                    QStringList& errors, int lookbackSeconds, int maximumEvents,
                    bool includeFormattedMessages)
{
    const int lookbackMinutes = std::max(1, (lookbackSeconds + 59) / 60);
    const QString lookbackLabel = lookbackMinutes % 60 == 0
        ? QStringLiteral("%1 hour(s)").arg(lookbackMinutes / 60)
        : QStringLiteral("%1 minute(s)").arg(lookbackMinutes);
    const std::wstring queryText = QStringLiteral(
        "*[System[(Level=1 or Level=2 or Level=3) and TimeCreated[timediff(@SystemTime) <= %1]]]")
        .arg(lookbackSeconds * 1000).toStdWString();
    const std::wstring channelPath = channel.toStdWString();
    EventHandle query(EvtQuery(nullptr, channelPath.c_str(), queryText.c_str(),
                               EvtQueryChannelPath | EvtQueryReverseDirection));
    if (!query.get()) {
        errors.append(QStringLiteral("%1: Windows denied or could not read this log (error %2).")
            .arg(channel).arg(GetLastError()));
        return;
    }

    constexpr DWORD kBatchSize = 16;
    std::array<EVT_HANDLE, kBatchSize> handles{};
    int readCount = 0;
    while (readCount < maximumEvents) {
        DWORD returned = 0;
        if (!EvtNext(query.get(), kBatchSize, handles.data(), 0, 0, &returned)) {
            if (GetLastError() != ERROR_NO_MORE_ITEMS) {
                errors.append(QStringLiteral("%1: Windows stopped the event query (error %2).")
                    .arg(channel).arg(GetLastError()));
            }
            break;
        }

        for (DWORD i = 0; i < returned; ++i) {
            EventHandle event(handles[i]);
            if (readCount >= maximumEvents) continue;
            ++readCount;
            DWORD bufferBytes = 0;
            DWORD propertyCount = 0;
            EvtRender(nullptr, event.get(), EvtRenderEventXml, 0, nullptr, &bufferBytes, &propertyCount);
            if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || bufferBytes == 0 || bufferBytes > 65'536) continue;

            std::vector<wchar_t> xmlBuffer((bufferBytes / sizeof(wchar_t)) + 1, L'\0');
            if (!EvtRender(nullptr, event.get(), EvtRenderEventXml, bufferBytes,
                           xmlBuffer.data(), &bufferBytes, &propertyCount)) continue;

            const QString xml = QString::fromWCharArray(xmlBuffer.data(),
                static_cast<qsizetype>(bufferBytes / sizeof(wchar_t)));
            const ParsedEvent parsed = parseEventXml(xml);
            if (!parsed.hasId || parsed.provider.isEmpty() || parsed.level < 1 || parsed.level > 3) continue;

            const bool applicationError = channel == QLatin1String("Application") && parsed.id == 1000 &&
                parsed.provider.compare(QLatin1String("Application Error"), Qt::CaseInsensitive) == 0;
            const QString crashApplication = applicationError
                ? parsed.eventData.value(QStringLiteral("AppName")).trimmed().toCaseFolded()
                : QString();
            const EventKey key{channel, parsed.provider, parsed.id, crashApplication};
            const QString id = key.hashKey();
            auto it = grouped.find(id);
            if (it == grouped.end()) {
                EventInsight insight;
                insight.channel = channel;
                insight.provider = parsed.provider;
                insight.eventId = parsed.id;
                insight.severity = parsed.level == 1 ? EventSeverity::Critical
                    : (parsed.level == 2 ? EventSeverity::Error : EventSeverity::Warning);
                insight.latestAt = QDateTime::fromString(parsed.timestamp, Qt::ISODateWithMs);
                if (!insight.latestAt.isValid()) insight.latestAt = QDateTime::fromString(parsed.timestamp, Qt::ISODate);
                insight.latestAt = insight.latestAt.toLocalTime();
                insight.firstAt = insight.latestAt;
                insight.occurrenceCount = 1;
                insight.likelyApplicationCrash = channel == QLatin1String("Application") &&
                    parsed.id == 1000 && parsed.provider.compare(QLatin1String("Application Error"), Qt::CaseInsensitive) == 0;
                if (insight.likelyApplicationCrash) {
                    insight.affectedApplication = parsed.eventData.value(QStringLiteral("AppName")).left(160);
                    insight.faultingModule = parsed.eventData.value(QStringLiteral("ModuleName")).left(160);
                    insight.exceptionCode = parsed.eventData.value(QStringLiteral("ExceptionCode")).left(32);
                }
                insight.windowsHardwareError = channel == QLatin1String("System") &&
                    parsed.provider.compare(QLatin1String("Microsoft-Windows-WHEA-Logger"), Qt::CaseInsensitive) == 0;
                insight.windowsStorageEvent =
                    (channel == QLatin1String("System") ||
                     channel == QLatin1String("Microsoft-Windows-Storage-Storport/Operational")) &&
                    (parsed.provider.compare(QLatin1String("Microsoft-Windows-Storage-Storport"), Qt::CaseInsensitive) == 0 ||
                     parsed.provider.compare(QLatin1String("Microsoft-Windows-StorPort"), Qt::CaseInsensitive) == 0);
                insight.unexpectedShutdown = channel == QLatin1String("System") &&
                    ((parsed.id == 41 && parsed.provider.compare(
                        QLatin1String("Microsoft-Windows-Kernel-Power"), Qt::CaseInsensitive) == 0) ||
                     (parsed.id == 6008 && parsed.provider.compare(
                        QLatin1String("EventLog"), Qt::CaseInsensitive) == 0));
                insight.windowsBugCheck = channel == QLatin1String("System") && parsed.id == 1001 &&
                    parsed.provider.compare(QLatin1String("Microsoft-Windows-WER-SystemErrorReporting"),
                                            Qt::CaseInsensitive) == 0;
                insight.applicationHang = channel == QLatin1String("Application") && parsed.id == 1002 &&
                    parsed.provider.compare(QLatin1String("Application Hang"), Qt::CaseInsensitive) == 0;
                if (insight.unexpectedShutdown || insight.windowsBugCheck)
                    insight.severity = EventSeverity::Critical;
                if (includeFormattedMessages || insight.likelyApplicationCrash || insight.windowsHardwareError) {
                    insight.message = formatEventMessage(event.get(), parsed.provider);
                    if (insight.likelyApplicationCrash && insight.affectedApplication.isEmpty())
                        insight.affectedApplication = extractFaultingApplication(insight.message);
                }
                insight.explanation = QStringLiteral("Windows recorded a %1 event (ID %2) from %3 in the %4 log during the last %5. This signal may help with diagnosis, but it does not establish a root cause.")
                    .arg(severityName(insight.severity)).arg(insight.eventId)
                    .arg(insight.provider, insight.channel, lookbackLabel);
                if (insight.unexpectedShutdown) {
                    insight.explanation = QStringLiteral("Windows recorded an unexpected-shutdown signal (%1, event ID %2). This can follow power loss, a crash, or a forced shutdown; it does not identify the underlying cause or prove hardware failure.")
                        .arg(insight.provider).arg(insight.eventId);
                } else if (insight.windowsBugCheck) {
                    insight.explanation = QStringLiteral("Windows recorded a restart after a bugcheck (blue-screen stop error), event ID 1001. The formatted Windows message may include a stop code; this record does not identify the faulty component or establish why the crash occurred.");
                } else if (insight.applicationHang) {
                    insight.explanation = QStringLiteral("Windows recorded an application-hang event (ID 1002). The app stopped responding at that time; this record does not prove what caused the hang.");
                } else if (insight.windowsHardwareError) {
                    insight.explanation = QStringLiteral("Windows recorded a Windows Hardware Error Architecture (WHEA) report, event ID %1. Review the formatted Windows message and whether it recurs. A log entry is evidence to investigate, not a diagnosis of a component or proof that hardware is failing.")
                        .arg(insight.eventId);
                } else if (insight.windowsStorageEvent) {
                    insight.explanation = QStringLiteral("Windows recorded a storage-driver event, event ID %1. Review the provider message and whether it recurs. A storage event can be useful evidence, but by itself does not diagnose a drive or establish a cause.")
                        .arg(insight.eventId);
                }
                grouped.insert(id, std::move(insight));
            } else {
                ++it->occurrenceCount;
                QDateTime occurredAt = QDateTime::fromString(parsed.timestamp, Qt::ISODateWithMs);
                if (!occurredAt.isValid()) occurredAt = QDateTime::fromString(parsed.timestamp, Qt::ISODate);
                if (occurredAt.isValid()) {
                    occurredAt = occurredAt.toLocalTime();
                    if (!it->firstAt.isValid() || occurredAt < it->firstAt) it->firstAt = occurredAt;
                    if (!it->latestAt.isValid() || occurredAt > it->latestAt) it->latestAt = occurredAt;
                }
            }
        }
    }
}

EventLogUpdate collectWindow(int lookbackMinutes, int maximumEvents, bool includeFormattedMessages)
{
    EventLogUpdate update;
    update.checkedAt = QDateTime::currentDateTime();
    QHash<QString, EventInsight> grouped;
    QStringList errors;
    const int minutes = std::clamp(lookbackMinutes, 1, 24 * 60);
    const int eventLimit = std::clamp(maximumEvents, 1, 500);
    collectChannel(QStringLiteral("System"), grouped, errors, minutes * 60, eventLimit, includeFormattedMessages);
    collectChannel(QStringLiteral("Application"), grouped, errors, minutes * 60, eventLimit, includeFormattedMessages);
    collectChannel(QStringLiteral("Microsoft-Windows-Storage-Storport/Operational"), grouped,
                   errors, minutes * 60, eventLimit, includeFormattedMessages);

    update.insights.reserve(grouped.size());
    for (auto it = grouped.cbegin(); it != grouped.cend(); ++it) update.insights.append(it.value());
    std::sort(update.insights.begin(), update.insights.end(), [](const EventInsight& left, const EventInsight& right) {
        if (left.severity != right.severity) return left.severity > right.severity;
        if (left.occurrenceCount != right.occurrenceCount) return left.occurrenceCount > right.occurrenceCount;
        return left.latestAt > right.latestAt;
    });
    if (update.insights.size() > 100) update.insights.resize(100);
    update.available = errors.size() < 3;
    if (errors.isEmpty()) {
        update.status = minutes == 24 * 60 && eventLimit == 500 && includeFormattedMessages
            ? QStringLiteral("Read local Windows System, Application, and storage-driver logs · last 24 hours · grouped repeated events · capped at 500 events per log")
            : QStringLiteral("Read local Windows System, Application, and storage-driver logs · last %1 minutes · capped at %2 events per log")
                  .arg(minutes).arg(eventLimit);
    } else {
        update.status = QStringLiteral("Some Windows event logs could not be read; results may be partial.\n%1")
            .arg(errors.join(QLatin1Char('\n')));
        if (!update.insights.isEmpty()) update.status += QStringLiteral("\nOther readable events are still shown.");
    }
    return update;
}

} // namespace

EventLogUpdate EventLogCollector::collect()
{
    return collectWindow(24 * 60, 500, true);
}

EventLogUpdate EventLogCollector::collectRecent(int lookbackMinutes)
{
    return collectWindow(lookbackMinutes, 100, false);
}

} // namespace Ausyn
