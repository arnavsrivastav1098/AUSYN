#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <wscapi.h>
#include <wuapi.h>

#include "security_status_collector.h"

#include <OleAuto.h>

#include <algorithm>
#include <utility>

#pragma comment(lib, "Wscapi.lib")
#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "OleAut32.lib")

namespace Ausyn {
namespace {

template <typename T>
class ComOwner final {
public:
    ComOwner() = default;
    ~ComOwner() { if (value_) value_->Release(); }
    ComOwner(const ComOwner&) = delete;
    ComOwner& operator=(const ComOwner&) = delete;
    [[nodiscard]] T* get() const noexcept { return value_; }
    [[nodiscard]] T** put() noexcept
    {
        if (value_) value_->Release();
        value_ = nullptr;
        return &value_;
    }
private:
    T* value_ = nullptr;
};

class BstrOwner final {
public:
    explicit BstrOwner(BSTR value = nullptr) noexcept : value_(value) {}
    ~BstrOwner() { if (value_) SysFreeString(value_); }
    BstrOwner(const BstrOwner&) = delete;
    BstrOwner& operator=(const BstrOwner&) = delete;
    [[nodiscard]] BSTR get() const noexcept { return value_; }
private:
    BSTR value_ = nullptr;
};

ProviderHealth readProvider(const QString& title, WSC_SECURITY_PROVIDER provider)
{
    ProviderHealth result;
    result.title = title;
    WSC_SECURITY_PROVIDER_HEALTH health = WSC_SECURITY_PROVIDER_HEALTH_NOTMONITORED;
    const HRESULT status = WscGetSecurityProviderHealth(provider, &health);
    if (FAILED(status)) {
        result.status = QStringLiteral("Unavailable");
        result.detail = QStringLiteral("Windows Security Center did not provide this status (0x%1).")
            .arg(static_cast<quint32>(status), 8, 16, QLatin1Char('0'));
        return result;
    }

    result.available = true;
    switch (health) {
    case WSC_SECURITY_PROVIDER_HEALTH_GOOD:
        result.status = QStringLiteral("Good");
        result.detail = QStringLiteral("Windows Security Center reports this category as healthy.");
        break;
    case WSC_SECURITY_PROVIDER_HEALTH_POOR:
        result.status = QStringLiteral("Needs attention");
        result.detail = QStringLiteral("Windows Security Center reports this category as unhealthy.");
        break;
    case WSC_SECURITY_PROVIDER_HEALTH_SNOOZE:
        result.status = QStringLiteral("Temporarily snoozed");
        result.detail = QStringLiteral("Windows Security Center reports a temporary snoozed state.");
        break;
    case WSC_SECURITY_PROVIDER_HEALTH_NOTMONITORED:
    default:
        result.status = QStringLiteral("Not monitored");
        result.detail = QStringLiteral("Windows Security Center is not monitoring this category.");
        break;
    }
    return result;
}

bool registryKeyExists(HKEY root, const wchar_t* subkey)
{
    HKEY key = nullptr;
    const LSTATUS status = RegOpenKeyExW(root, subkey, 0, KEY_READ, &key);
    if (status != ERROR_SUCCESS) return false;
    RegCloseKey(key);
    return true;
}

bool restartPending()
{
    constexpr wchar_t kWindowsUpdateReboot[] =
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\WindowsUpdate\\Auto Update\\RebootRequired";
    constexpr wchar_t kCbsReboot[] =
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Component Based Servicing\\RebootPending";
    constexpr wchar_t kCbsPackagesPending[] =
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Component Based Servicing\\PackagesPending";
    return registryKeyExists(HKEY_LOCAL_MACHINE, kWindowsUpdateReboot) ||
           registryKeyExists(HKEY_LOCAL_MACHINE, kCbsReboot) ||
           registryKeyExists(HKEY_LOCAL_MACHINE, kCbsPackagesPending);
}

QString bstrToString(BSTR value)
{
    return value ? QString::fromWCharArray(value, SysStringLen(value)) : QString();
}

} // namespace

SecurityStatusUpdate SecurityStatusCollector::readSecurityStatus()
{
    SecurityStatusUpdate update;
    update.checkedAt = QDateTime::currentDateTime();
    update.antivirus = readProvider(QStringLiteral("Antivirus protection"), WSC_SECURITY_PROVIDER_ANTIVIRUS);
    update.firewall = readProvider(QStringLiteral("Firewall"), WSC_SECURITY_PROVIDER_FIREWALL);
    update.automaticUpdates = readProvider(QStringLiteral("Automatic update settings"), WSC_SECURITY_PROVIDER_AUTOUPDATE_SETTINGS);
    update.restartRequired = restartPending();
    return update;
}

UpdateCacheResult SecurityStatusCollector::scanLocalUpdateCache()
{
    UpdateCacheResult result;
    result.checkedAt = QDateTime::currentDateTime();
    result.restartRequired = restartPending();

    const HRESULT comStatus = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool mustUninitialize = SUCCEEDED(comStatus);
    if (FAILED(comStatus)) {
        result.status = QStringLiteral("Could not initialize the Windows Update interface (0x%1).")
            .arg(static_cast<quint32>(comStatus), 8, 16, QLatin1Char('0'));
        return result;
    }
    struct ComApartment final {
        bool initialized;
        ~ComApartment() { if (initialized) CoUninitialize(); }
    } apartment{mustUninitialize};

    ComOwner<IUpdateSession> session;
    CLSID sessionClass{};
    HRESULT status = CLSIDFromProgID(L"Microsoft.Update.Session", &sessionClass);
    if (SUCCEEDED(status)) {
        status = CoCreateInstance(sessionClass, nullptr, CLSCTX_INPROC_SERVER,
                                  __uuidof(IUpdateSession), reinterpret_cast<void**>(session.put()));
    }
    if (FAILED(status) || !session.get()) {
        result.status = QStringLiteral("Windows Update Agent is unavailable (0x%1).")
            .arg(static_cast<quint32>(status), 8, 16, QLatin1Char('0'));
        return result;
    }

    ComOwner<IUpdateSearcher> searcher;
    status = session.get()->CreateUpdateSearcher(searcher.put());
    if (FAILED(status) || !searcher.get()) {
        result.status = QStringLiteral("Windows could not prepare an update-cache scan (0x%1).")
            .arg(static_cast<quint32>(status), 8, 16, QLatin1Char('0'));
        return result;
    }
    // This deliberately searches only the local Windows Update cache. It does
    // not start an online scan or install anything.
    status = searcher.get()->put_Online(VARIANT_FALSE);
    if (FAILED(status)) {
        result.status = QStringLiteral("Windows could not set the scan to offline mode (0x%1).")
            .arg(static_cast<quint32>(status), 8, 16, QLatin1Char('0'));
        return result;
    }
    status = searcher.get()->put_CanAutomaticallyUpgradeService(VARIANT_FALSE);
    if (FAILED(status)) {
        result.status = QStringLiteral("Windows could not disable automatic Update Agent upgrade for this scan (0x%1).")
            .arg(static_cast<quint32>(status), 8, 16, QLatin1Char('0'));
        return result;
    }

    BstrOwner criteria(SysAllocString(L"IsInstalled=0 and IsHidden=0 and Type='Software'"));
    if (!criteria.get()) {
        result.status = QStringLiteral("Ausyn could not allocate the local update query.");
        return result;
    }
    ComOwner<ISearchResult> searchResult;
    status = searcher.get()->Search(criteria.get(), searchResult.put());
    if (FAILED(status) || !searchResult.get()) {
        result.status = QStringLiteral("The local update cache could not be searched (0x%1).")
            .arg(static_cast<quint32>(status), 8, 16, QLatin1Char('0'));
        return result;
    }

    ComOwner<IUpdateCollection> updates;
    status = searchResult.get()->get_Updates(updates.put());
    LONG count = 0;
    if (FAILED(status) || !updates.get() || FAILED(updates.get()->get_Count(&count)) || count < 0) {
        result.status = QStringLiteral("Windows returned an invalid local update result.");
        return result;
    }
    result.available = true;
    result.updateCount = count;
    const LONG displayCount = std::min<LONG>(count, 12);
    for (LONG index = 0; index < displayCount; ++index) {
        ComOwner<IUpdate> update;
        if (FAILED(updates.get()->get_Item(index, update.put())) || !update.get()) continue;
        BSTR rawTitle = nullptr;
        if (SUCCEEDED(update.get()->get_Title(&rawTitle))) {
            BstrOwner title(rawTitle);
            const QString text = bstrToString(title.get()).trimmed();
            if (!text.isEmpty()) result.updateTitles.append(text);
        }
    }
    if (result.restartRequired) {
        result.status = QStringLiteral("Restart required · %1 update%2 in the local cache")
            .arg(result.updateCount).arg(result.updateCount == 1 ? QString() : QStringLiteral("s"));
    } else if (count > 0) {
        result.status = QStringLiteral("%1 update%2 found in the local cache. This is not a live online check.")
            .arg(count).arg(count == 1 ? QString() : QStringLiteral("s"));
    } else {
        result.status = QStringLiteral("No pending software updates were found in the local cache. This does not prove the device is up to date.");
    }
    return result;
}

} // namespace Ausyn
