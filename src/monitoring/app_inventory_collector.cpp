#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <ShlObj.h>

#include "app_inventory_collector.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <array>
#include <string>
#include <utility>
#include <vector>

#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Ole32.lib")

namespace Ausyn {
namespace {

class RegistryKey final {
public:
    explicit RegistryKey(HKEY key = nullptr) noexcept : key_(key) {}
    ~RegistryKey() { if (key_) RegCloseKey(key_); }
    RegistryKey(const RegistryKey&) = delete;
    RegistryKey& operator=(const RegistryKey&) = delete;
    RegistryKey(RegistryKey&& other) noexcept : key_(std::exchange(other.key_, nullptr)) {}
    RegistryKey& operator=(RegistryKey&& other) noexcept
    {
        if (this != &other) {
            if (key_) RegCloseKey(key_);
            key_ = std::exchange(other.key_, nullptr);
        }
        return *this;
    }
    [[nodiscard]] HKEY get() const noexcept { return key_; }
private:
    HKEY key_ = nullptr;
};

QString readStringValue(HKEY key, const wchar_t* valueName, bool expand = false)
{
    DWORD type = 0;
    DWORD bytes = 0;
    if (RegQueryValueExW(key, valueName, nullptr, &type, nullptr, &bytes) != ERROR_SUCCESS ||
        (type != REG_SZ && type != REG_EXPAND_SZ) || bytes == 0 || bytes > 65'536 ||
        bytes % sizeof(wchar_t) != 0) return {};
    std::vector<wchar_t> buffer(bytes / sizeof(wchar_t) + 1, L'\0');
    if (RegQueryValueExW(key, valueName, nullptr, &type,
            reinterpret_cast<BYTE*>(buffer.data()), &bytes) != ERROR_SUCCESS) return {};
    QString value = QString::fromWCharArray(buffer.data()).trimmed();
    if (expand && type == REG_EXPAND_SZ && !value.isEmpty()) {
        const DWORD required = ExpandEnvironmentStringsW(value.toStdWString().c_str(), nullptr, 0);
        if (required > 0 && required <= 32'768) {
            std::vector<wchar_t> expanded(required, L'\0');
            if (ExpandEnvironmentStringsW(value.toStdWString().c_str(), expanded.data(), required) > 0) {
                value = QString::fromWCharArray(expanded.data()).trimmed();
            }
        }
    }
    return value;
}

bool registryOpen(HKEY root, const wchar_t* path, REGSAM view, RegistryKey& key)
{
    HKEY raw = nullptr;
    if (RegOpenKeyExW(root, path, 0, KEY_READ | view, &raw) != ERROR_SUCCESS) return false;
    key = RegistryKey(raw);
    return true;
}

void collectRunKey(HKEY root, const wchar_t* path, REGSAM view, const QString& source,
                   QVector<StartupEntry>& output, QSet<QString>& seen, QStringList& errors)
{
    RegistryKey key;
    if (!registryOpen(root, path, view, key)) return;
    for (DWORD index = 0; index < 1024; ++index) {
        std::array<wchar_t, 16'384> valueName{};
        DWORD nameLength = static_cast<DWORD>(valueName.size());
        DWORD dataBytes = 0;
        DWORD type = 0;
        const LSTATUS result = RegEnumValueW(key.get(), index, valueName.data(), &nameLength,
                                             nullptr, &type, nullptr, &dataBytes);
        if (result == ERROR_NO_MORE_ITEMS) break;
        if (result == ERROR_ACCESS_DENIED) {
            errors.append(QStringLiteral("Some %1 startup entries could not be read.").arg(source));
            break;
        }
        if (result != ERROR_SUCCESS || nameLength >= valueName.size() ||
            (type != REG_SZ && type != REG_EXPAND_SZ) || dataBytes == 0 || dataBytes > 65'536) continue;
        std::vector<BYTE> data(dataBytes + sizeof(wchar_t), 0);
        DWORD actualBytes = dataBytes;
        nameLength = static_cast<DWORD>(valueName.size());
        if (RegEnumValueW(key.get(), index, valueName.data(), &nameLength, nullptr, &type,
                          data.data(), &actualBytes) != ERROR_SUCCESS) continue;
        QString command = QString::fromWCharArray(reinterpret_cast<const wchar_t*>(data.data())).trimmed();
        if (type == REG_EXPAND_SZ && !command.isEmpty()) {
            const DWORD required = ExpandEnvironmentStringsW(command.toStdWString().c_str(), nullptr, 0);
            if (required > 0 && required <= 32'768) {
                std::vector<wchar_t> expanded(required, L'\0');
                if (ExpandEnvironmentStringsW(command.toStdWString().c_str(), expanded.data(), required) > 0)
                    command = QString::fromWCharArray(expanded.data()).trimmed();
            }
        }
        const QString name = QString::fromWCharArray(valueName.data(), static_cast<qsizetype>(nameLength));
        const QString unique = (source + QLatin1Char('|') + name + QLatin1Char('|') + command).toCaseFolded();
        if (command.isEmpty() || seen.contains(unique)) continue;
        seen.insert(unique);
        output.append({name, command, source,
            QStringLiteral("Registered in the Windows startup registry. Ausyn does not determine whether Windows delays or blocks it.")});
    }
}

void collectStartupFolder(const KNOWNFOLDERID& folderId, const QString& source,
                          QVector<StartupEntry>& output, QSet<QString>& seen)
{
    PWSTR folderPath = nullptr;
    if (FAILED(SHGetKnownFolderPath(folderId, KF_FLAG_DEFAULT, nullptr, &folderPath)) || !folderPath) return;
    const QString path = QString::fromWCharArray(folderPath);
    CoTaskMemFree(folderPath);
    QDir directory(path);
    const QFileInfoList entries = directory.entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo& file : entries) {
        const QString suffix = file.suffix().toCaseFolded();
        if (suffix != QStringLiteral("lnk") && suffix != QStringLiteral("exe") &&
            suffix != QStringLiteral("bat") && suffix != QStringLiteral("cmd")) continue;
        const QString unique = (source + QLatin1Char('|') + file.fileName()).toCaseFolded();
        if (seen.contains(unique)) continue;
        seen.insert(unique);
        output.append({file.completeBaseName(), file.absoluteFilePath(), source,
            QStringLiteral("File present in a Windows Startup folder. Ausyn does not run or modify it.")});
    }
}

QString installedAppsPath()
{
    return QStringLiteral("Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall");
}

bool readDword(HKEY key, const wchar_t* name, DWORD& value)
{
    DWORD type = 0;
    DWORD bytes = sizeof(value);
    return RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(&value), &bytes) == ERROR_SUCCESS &&
           type == REG_DWORD && bytes == sizeof(value);
}

void collectInstalledApps(HKEY root, const QString& rootName, REGSAM view,
                          QVector<InstalledAppEntry>& output, QSet<QString>& seen,
                          QStringList& errors)
{
    const std::wstring path = installedAppsPath().toStdWString();
    RegistryKey rootKey;
    if (!registryOpen(root, path.c_str(), view, rootKey)) return;
    for (DWORD index = 0; index < 10'000; ++index) {
        std::array<wchar_t, 512> subKeyName{};
        DWORD nameLength = static_cast<DWORD>(subKeyName.size());
        const LSTATUS result = RegEnumKeyExW(rootKey.get(), index, subKeyName.data(), &nameLength,
                                              nullptr, nullptr, nullptr, nullptr);
        if (result == ERROR_NO_MORE_ITEMS) break;
        if (result == ERROR_ACCESS_DENIED) {
            errors.append(QStringLiteral("Some %1 installed-app entries could not be read.").arg(rootName));
            break;
        }
        if (result != ERROR_SUCCESS || nameLength >= subKeyName.size()) continue;
        HKEY raw = nullptr;
        if (RegOpenKeyExW(rootKey.get(), subKeyName.data(), 0, KEY_READ, &raw) != ERROR_SUCCESS) continue;
        RegistryKey appKey(raw);
        DWORD flag = 0;
        if ((readDword(appKey.get(), L"SystemComponent", flag) && flag != 0) ||
            (readDword(appKey.get(), L"NoDisplay", flag) && flag != 0)) continue;
        InstalledAppEntry app;
        app.name = readStringValue(appKey.get(), L"DisplayName");
        if (app.name.isEmpty()) continue;
        app.publisher = readStringValue(appKey.get(), L"Publisher");
        app.version = readStringValue(appKey.get(), L"DisplayVersion");
        app.installDate = readStringValue(appKey.get(), L"InstallDate");
        app.source = rootName + (view == KEY_WOW64_32KEY ? QStringLiteral(" · 32-bit registry")
                                                          : QStringLiteral(" · 64-bit registry"));
        const QString unique = (app.name + QLatin1Char('|') + app.publisher + QLatin1Char('|') + app.version).toCaseFolded();
        if (seen.contains(unique)) continue;
        seen.insert(unique);
        output.append(std::move(app));
    }
}

} // namespace

AppInventoryUpdate AppInventoryCollector::collect()
{
    AppInventoryUpdate update;
    update.checkedAt = QDateTime::currentDateTime();
    QStringList errors;
    QSet<QString> seenStartup;
    QSet<QString> seenApps;
    constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    constexpr wchar_t kRunOnceKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce";

    collectRunKey(HKEY_CURRENT_USER, kRunKey, KEY_WOW64_64KEY, QStringLiteral("Current user · Run"), update.startupEntries, seenStartup, errors);
    collectRunKey(HKEY_CURRENT_USER, kRunKey, KEY_WOW64_32KEY, QStringLiteral("Current user · Run"), update.startupEntries, seenStartup, errors);
    collectRunKey(HKEY_LOCAL_MACHINE, kRunKey, KEY_WOW64_64KEY, QStringLiteral("All users · Run"), update.startupEntries, seenStartup, errors);
    collectRunKey(HKEY_LOCAL_MACHINE, kRunKey, KEY_WOW64_32KEY, QStringLiteral("All users · Run"), update.startupEntries, seenStartup, errors);
    collectRunKey(HKEY_CURRENT_USER, kRunOnceKey, KEY_WOW64_64KEY, QStringLiteral("Current user · Run once"), update.startupEntries, seenStartup, errors);
    collectRunKey(HKEY_CURRENT_USER, kRunOnceKey, KEY_WOW64_32KEY, QStringLiteral("Current user · Run once"), update.startupEntries, seenStartup, errors);
    collectRunKey(HKEY_LOCAL_MACHINE, kRunOnceKey, KEY_WOW64_64KEY, QStringLiteral("All users · Run once"), update.startupEntries, seenStartup, errors);
    collectRunKey(HKEY_LOCAL_MACHINE, kRunOnceKey, KEY_WOW64_32KEY, QStringLiteral("All users · Run once"), update.startupEntries, seenStartup, errors);
    collectStartupFolder(FOLDERID_Startup, QStringLiteral("Current user · Startup folder"), update.startupEntries, seenStartup);
    collectStartupFolder(FOLDERID_CommonStartup, QStringLiteral("All users · Startup folder"), update.startupEntries, seenStartup);

    collectInstalledApps(HKEY_CURRENT_USER, QStringLiteral("Current user"), KEY_WOW64_64KEY, update.installedApps, seenApps, errors);
    collectInstalledApps(HKEY_CURRENT_USER, QStringLiteral("Current user"), KEY_WOW64_32KEY, update.installedApps, seenApps, errors);
    collectInstalledApps(HKEY_LOCAL_MACHINE, QStringLiteral("All users"), KEY_WOW64_64KEY, update.installedApps, seenApps, errors);
    collectInstalledApps(HKEY_LOCAL_MACHINE, QStringLiteral("All users"), KEY_WOW64_32KEY, update.installedApps, seenApps, errors);

    std::sort(update.startupEntries.begin(), update.startupEntries.end(), [](const auto& a, const auto& b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    std::sort(update.installedApps.begin(), update.installedApps.end(), [](const auto& a, const auto& b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    update.available = true;
    update.status = errors.isEmpty()
        ? QStringLiteral("Read-only inventory · startup registrations and installed-app registry entries · no programs were run")
        : errors.join(QLatin1Char('\n')) + QStringLiteral("\nReadable entries are still shown; this inventory may be partial.");
    return update;
}

} // namespace Ausyn
