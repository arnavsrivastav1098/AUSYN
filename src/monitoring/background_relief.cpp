#include "background_relief.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QTimer>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#include <vector>
#endif
namespace Ausyn {
namespace {
#ifdef Q_OS_WIN
bool sameOwner(HANDLE process) {
    HANDLE other = nullptr, own = nullptr;
    if (!OpenProcessToken(process, TOKEN_QUERY, &other)) return false;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &own)) { CloseHandle(other); return false; }
    const auto user = [](HANDLE token) {
        DWORD size = 0; GetTokenInformation(token, TokenUser, nullptr, 0, &size);
        std::vector<unsigned char> data(size);
        if (size == 0 || !GetTokenInformation(token, TokenUser, data.data(), size, &size)) data.clear();
        return data;
    };
    const auto a = user(other), b = user(own);
    const bool equal = !a.empty() && !b.empty() && EqualSid(reinterpret_cast<const TOKEN_USER*>(a.data())->User.Sid, reinterpret_cast<const TOKEN_USER*>(b.data())->User.Sid);
    CloseHandle(other); CloseHandle(own); return equal;
}
#endif
}
BackgroundRelief::BackgroundRelief(QObject* parent) : QObject(parent), expiry_(new QTimer(this)) {
    expiry_->setSingleShot(true);
    connect(expiry_, &QTimer::timeout, this, &BackgroundRelief::restore);
}
BackgroundRelief::~BackgroundRelief() { restore(); }
bool BackgroundRelief::apply(const ProcessSample& p, const SystemSnapshot& s, const QString& keptApp, QString* error) {
    const auto fail = [error](const QString& message) { if (error) *error = message; return false; };
    const qint64 age = s.processSamplesCapturedAt.isValid() ? s.processSamplesCapturedAt.msecsTo(QDateTime::currentDateTime()) : -1;
    if (age < 0 || age > 30'000 || p.processId == 0 || p.executablePath.isEmpty()) return fail(QStringLiteral("Wait for a fresh process reading before making this change."));
    if (p.name.compare(keptApp, Qt::CaseInsensitive) == 0 || p.name.compare(s.foregroundProcessName, Qt::CaseInsensitive) == 0 || p.processId == s.foregroundProcessId)
        return fail(QStringLiteral("This is your current or kept app. Choose a different background app."));
#ifdef Q_OS_WIN
    if (p.processId == GetCurrentProcessId()) return fail(QStringLiteral("Ausyn cannot select itself for this action."));
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_SET_INFORMATION | SYNCHRONIZE, FALSE, p.processId);
    if (!process) return fail(QStringLiteral("Windows denied permission to change this process. Ausyn will not elevate itself."));
    const auto closeFail = [process, &fail](const QString& message) { CloseHandle(process); return fail(message); };
    wchar_t buffer[32768]; DWORD count = 32768;
    if (!QueryFullProcessImageNameW(process, 0, buffer, &count)) return closeFail(QStringLiteral("Windows could not confirm the process identity."));
    const QString path = QDir::cleanPath(QDir::fromNativeSeparators(QString::fromWCharArray(buffer, static_cast<qsizetype>(count))));
    const QString expected = QDir::cleanPath(QDir::fromNativeSeparators(p.executablePath));
    wchar_t windowsPath[MAX_PATH]; const UINT windowsLength = GetWindowsDirectoryW(windowsPath, MAX_PATH);
    if (!windowsLength || windowsLength >= MAX_PATH) return closeFail(QStringLiteral("Windows system path could not be checked."));
    const QString system = QDir::cleanPath(QDir::fromNativeSeparators(QString::fromWCharArray(windowsPath))) + QLatin1Char('/');
    BOOL critical = FALSE;
    if (path.compare(expected, Qt::CaseInsensitive) != 0 || path.startsWith(system, Qt::CaseInsensitive) ||
        !IsProcessCritical(process, &critical) || critical || !sameOwner(process))
        return closeFail(QStringLiteral("Only a verified non-system app owned by your Windows account can be changed."));
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(process, &created, &exited, &kernel, &user)) return closeFail(QStringLiteral("The process instance could not be verified."));
    ULARGE_INTEGER createdTime{}; createdTime.LowPart = created.dwLowDateTime; createdTime.HighPart = created.dwHighDateTime;
    constexpr quint64 windowsEpochMs = 11644473600000ULL;
    const qint64 createdMs = static_cast<qint64>(createdTime.QuadPart / 10000ULL - windowsEpochMs);
    if (createdMs > s.processSamplesCapturedAt.toMSecsSinceEpoch()) return closeFail(QStringLiteral("This app restarted after the sample. Wait for fresh readings and review again."));
    DWORD foregroundPid = 0; GetWindowThreadProcessId(GetForegroundWindow(), &foregroundPid);
    if (foregroundPid == p.processId || WaitForSingleObject(process, 0) != WAIT_TIMEOUT)
        return closeFail(QStringLiteral("The app is now in front or has exited. No priority change was made."));
    if (GetPriorityClass(process) != NORMAL_PRIORITY_CLASS)
        return closeFail(QStringLiteral("This process already uses a custom priority. Ausyn will leave it as configured."));
    restore();
    if (!SetPriorityClass(process, BELOW_NORMAL_PRIORITY_CLASS) || GetPriorityClass(process) != BELOW_NORMAL_PRIORITY_CLASS) {
        SetPriorityClass(process, NORMAL_PRIORITY_CLASS); CloseHandle(process);
        return fail(QStringLiteral("Windows did not verify the requested priority change."));
    }
    process_ = process; pid_ = p.processId; name_ = p.name;
    status_ = QStringLiteral("%1 · PID %2: CPU priority temporarily below normal. Auto-restore in 10 minutes, when it moves to the foreground, or when you end the session. RAM is unchanged.").arg(name_).arg(pid_);
    expiry_->start(10 * 60 * 1000); emit changed(status_); return true;
#else
    return fail(QStringLiteral("This action is available only on Windows."));
#endif
}
void BackgroundRelief::restore() {
    expiry_->stop();
    if (!process_) return;
#ifdef Q_OS_WIN
    HANDLE process = static_cast<HANDLE>(process_);
    const bool running = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
    const DWORD priority = running ? GetPriorityClass(process) : 0;
    const bool restored = running && priority == BELOW_NORMAL_PRIORITY_CLASS && SetPriorityClass(process, NORMAL_PRIORITY_CLASS) && GetPriorityClass(process) == NORMAL_PRIORITY_CLASS;
    status_ = !running ? QStringLiteral("%1 exited; the temporary change has ended.").arg(name_)
        : restored ? QStringLiteral("%1: normal CPU priority restored and verified.").arg(name_)
        : priority != BELOW_NORMAL_PRIORITY_CLASS ? QStringLiteral("%1 has a different priority now; Ausyn preserved that change.").arg(name_)
        : QStringLiteral("Windows could not restore %1’s priority. Review this app in Task Manager.").arg(name_);
    CloseHandle(process);
#endif
    process_ = nullptr; pid_ = 0; emit changed(status_);
}
void BackgroundRelief::guard(const QString& keptApp, const QString& foregroundApp) {
    if (!process_) return;
#ifdef Q_OS_WIN
    DWORD foreground = 0; GetWindowThreadProcessId(GetForegroundWindow(), &foreground);
    if (foreground == pid_ || name_.compare(keptApp, Qt::CaseInsensitive) == 0 || name_.compare(foregroundApp, Qt::CaseInsensitive) == 0 || WaitForSingleObject(static_cast<HANDLE>(process_), 0) != WAIT_TIMEOUT) restore();
#endif
}
}
