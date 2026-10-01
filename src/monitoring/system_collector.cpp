#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <Wbemidl.h>
#include <winioctl.h>
#include <ntddvol.h>
#include <ws2def.h>
#include <ws2ipdef.h>
#include <Iphlpapi.h>
#include <netioapi.h>
#include <Pdh.h>
#include <PdhMsg.h>
#include <Psapi.h>
#include <PowrProf.h>
#include <TlHelp32.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include "system_collector.h"

#include <QSysInfo>
#include <QHash>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "Dxgi.lib")
#pragma comment(lib, "Iphlpapi.lib")
#pragma comment(lib, "Pdh.lib")
#pragma comment(lib, "Psapi.lib")
#pragma comment(lib, "PowrProf.lib")

namespace Ausyn {
namespace {

using Microsoft::WRL::ComPtr;

quint64 fileTimeToUInt64(const FILETIME& value) noexcept
{
    ULARGE_INTEGER result{};
    result.LowPart = value.dwLowDateTime;
    result.HighPart = value.dwHighDateTime;
    return result.QuadPart;
}

QString readProcessorName()
{
    HKEY key = nullptr;
    constexpr wchar_t kProcessorKey[] =
        L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0";
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kProcessorKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return {};
    }

    DWORD type = 0;
    DWORD byteCount = 0;
    const auto sizeResult = RegQueryValueExW(key, L"ProcessorNameString", nullptr, &type, nullptr, &byteCount);
    if (sizeResult != ERROR_SUCCESS || type != REG_SZ || byteCount < sizeof(wchar_t)) {
        RegCloseKey(key);
        return {};
    }

    std::wstring name(static_cast<size_t>(byteCount / sizeof(wchar_t)), L'\0');
    const auto readResult = RegQueryValueExW(
        key, L"ProcessorNameString", nullptr, &type,
        reinterpret_cast<BYTE*>(name.data()), &byteCount);
    RegCloseKey(key);
    if (readResult != ERROR_SUCCESS) {
        return {};
    }

    while (!name.empty() && name.back() == L'\0') {
        name.pop_back();
    }
    return QString::fromStdWString(name).trimmed();
}

QString readBiosValue(const wchar_t* valueName)
{
    HKEY key = nullptr;
    constexpr wchar_t kBiosKey[] = L"HARDWARE\\DESCRIPTION\\System\\BIOS";
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kBiosKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return {};
    DWORD type = 0;
    DWORD bytes = 0;
    if (RegQueryValueExW(key, valueName, nullptr, &type, nullptr, &bytes) != ERROR_SUCCESS ||
        (type != REG_SZ && type != REG_EXPAND_SZ) || bytes < sizeof(wchar_t) || bytes > 32'768 ||
        bytes % sizeof(wchar_t) != 0) {
        RegCloseKey(key);
        return {};
    }
    std::vector<wchar_t> value(bytes / sizeof(wchar_t) + 1, L'\0');
    const LSTATUS result = RegQueryValueExW(key, valueName, nullptr, &type,
        reinterpret_cast<BYTE*>(value.data()), &bytes);
    RegCloseKey(key);
    return result == ERROR_SUCCESS ? QString::fromWCharArray(value.data()).trimmed() : QString();
}

QString storageBusName(STORAGE_BUS_TYPE bus)
{
    switch (bus) {
    case BusTypeScsi: return QStringLiteral("SCSI");
    case BusTypeAtapi: return QStringLiteral("ATAPI");
    case BusTypeAta: return QStringLiteral("ATA");
    case BusTypeUsb: return QStringLiteral("USB");
    case BusTypeRAID: return QStringLiteral("RAID");
    case BusTypeiScsi: return QStringLiteral("iSCSI");
    case BusTypeSas: return QStringLiteral("SAS");
    case BusTypeSata: return QStringLiteral("SATA");
    case BusTypeSd: return QStringLiteral("SD");
    case BusTypeMmc: return QStringLiteral("MMC");
    case BusTypeVirtual: return QStringLiteral("Virtual");
    case BusTypeSpaces: return QStringLiteral("Storage Spaces");
    case BusTypeNvme: return QStringLiteral("NVMe");
    case BusTypeUfs: return QStringLiteral("UFS");
    default: return QStringLiteral("Unknown / driver-defined");
    }
}

QString descriptorString(const std::vector<BYTE>& buffer, DWORD offset, DWORD validBytes)
{
    if (offset == 0 || offset >= validBytes || validBytes > buffer.size()) return {};
    const char* text = reinterpret_cast<const char*>(buffer.data() + offset);
    const size_t remaining = validBytes - offset;
    const void* terminator = std::memchr(text, '\0', remaining);
    if (!terminator) return {};
    const auto length = static_cast<qsizetype>(static_cast<const char*>(terminator) - text);
    return QString::fromLocal8Bit(text, length).trimmed();
}

PhysicalDiskSample readPhysicalDisk(DWORD number)
{
    PhysicalDiskSample result;
    result.deviceNumber = number;
    const std::wstring path = L"\\\\.\\PhysicalDrive" + std::to_wstring(number);
    HANDLE handle = CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                OPEN_EXISTING, 0, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return {};

    STORAGE_PROPERTY_QUERY query{};
    query.PropertyId = StorageDeviceProperty;
    query.QueryType = PropertyStandardQuery;
    STORAGE_DESCRIPTOR_HEADER header{};
    DWORD bytesReturned = 0;
    const bool headerOk = DeviceIoControl(handle, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query),
        &header, sizeof(header), &bytesReturned, nullptr) && header.Size >= sizeof(STORAGE_DEVICE_DESCRIPTOR) &&
        header.Size <= 65'536;
    if (!headerOk) {
        CloseHandle(handle);
        return {};
    }

    std::vector<BYTE> buffer(header.Size, 0);
    bytesReturned = 0;
    const bool descriptorOk = DeviceIoControl(handle, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query),
        buffer.data(), static_cast<DWORD>(buffer.size()), &bytesReturned, nullptr);
    CloseHandle(handle);
    if (!descriptorOk || bytesReturned < sizeof(STORAGE_DEVICE_DESCRIPTOR)) return {};
    const auto* descriptor = reinterpret_cast<const STORAGE_DEVICE_DESCRIPTOR*>(buffer.data());
    if (descriptor->Size < sizeof(STORAGE_DEVICE_DESCRIPTOR) || descriptor->Size > bytesReturned) return {};

    result.vendor = descriptorString(buffer, descriptor->VendorIdOffset, descriptor->Size);
    result.model = descriptorString(buffer, descriptor->ProductIdOffset, descriptor->Size);
    result.firmware = descriptorString(buffer, descriptor->ProductRevisionOffset, descriptor->Size);
    result.busType = storageBusName(descriptor->BusType);
    if (result.model.isEmpty() && result.vendor.isEmpty()) return {};
    return result;
}

QString physicalDisksForVolume(const QString& rootPath)
{
    if (rootPath.size() < 2 || rootPath.at(1) != QLatin1Char(':')) return {};
    const std::wstring path = L"\\\\.\\" + rootPath.left(2).toStdWString();
    HANDLE handle = CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                OPEN_EXISTING, 0, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return {};
    constexpr DWORD kMaxExtents = 16;
    constexpr DWORD kBufferSize = static_cast<DWORD>(offsetof(VOLUME_DISK_EXTENTS, Extents) +
        kMaxExtents * sizeof(DISK_EXTENT));
    std::vector<BYTE> buffer(kBufferSize, 0);
    DWORD bytesReturned = 0;
    const BOOL ok = DeviceIoControl(handle, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, nullptr, 0,
        buffer.data(), kBufferSize, &bytesReturned, nullptr);
    CloseHandle(handle);
    if (!ok || bytesReturned < offsetof(VOLUME_DISK_EXTENTS, Extents)) return {};
    const auto* extents = reinterpret_cast<const VOLUME_DISK_EXTENTS*>(buffer.data());
    const DWORD count = std::min<DWORD>(extents->NumberOfDiskExtents, kMaxExtents);
    if (bytesReturned < offsetof(VOLUME_DISK_EXTENTS, Extents) + count * sizeof(DISK_EXTENT)) return {};
    QStringList numbers;
    for (DWORD i = 0; i < count; ++i) numbers.append(QString::number(extents->Extents[i].DiskNumber));
    return numbers.join(QStringLiteral(", "));
}

QVector<GraphicsAdapterSample> readGraphicsAdapters()
{
    QVector<GraphicsAdapterSample> results;
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        return results;
    }

    for (UINT index = 0;; ++index) {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(index, &adapter) == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        if (!adapter) {
            continue;
        }

        DXGI_ADAPTER_DESC1 description{};
        if (FAILED(adapter->GetDesc1(&description)) || (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) {
            continue;
        }

        GraphicsAdapterSample sample;
        sample.name = QString::fromWCharArray(description.Description).trimmed();
        sample.vendorId = description.VendorId;
        sample.dedicatedMemoryBytes = description.DedicatedVideoMemory;
        ComPtr<IDXGIAdapter3> adapter3;
        if (SUCCEEDED(adapter.As(&adapter3))) {
            DXGI_QUERY_VIDEO_MEMORY_INFO memoryInfo{};
            if (SUCCEEDED(adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memoryInfo)) &&
                memoryInfo.Budget > 0) {
                sample.localMemoryUsageBytes = memoryInfo.CurrentUsage;
                sample.localMemoryBudgetBytes = memoryInfo.Budget;
            }
        }
        results.append(std::move(sample));
    }
    return results;
}

QString driveRootForWindows()
{
    std::array<wchar_t, 32768> windowsDirectory{};
    const UINT length = GetWindowsDirectoryW(windowsDirectory.data(), static_cast<UINT>(windowsDirectory.size()));
    if (length == 0 || length >= windowsDirectory.size()) {
        return {};
    }

    std::array<wchar_t, 32768> volumeRoot{};
    if (!GetVolumePathNameW(windowsDirectory.data(), volumeRoot.data(), static_cast<DWORD>(volumeRoot.size()))) {
        return {};
    }
    return QString::fromWCharArray(volumeRoot.data());
}

QString processorArchitectureName(WORD architecture)
{
    switch (architecture) {
    case PROCESSOR_ARCHITECTURE_AMD64: return QStringLiteral("x64 (AMD64)");
    case PROCESSOR_ARCHITECTURE_ARM64: return QStringLiteral("ARM64");
    case PROCESSOR_ARCHITECTURE_INTEL: return QStringLiteral("x86 (Intel)");
    default: return QStringLiteral("Unknown (%1)").arg(architecture);
    }
}

struct PreviousProcessSample {
    std::optional<quint64> creationTime100ns;
    std::optional<quint64> cpuTime100ns;
    std::optional<quint64> readIoTransferBytes;
    std::optional<quint64> writeIoTransferBytes;
    std::chrono::steady_clock::time_point capturedAt;
};

template <typename TableType, typename TableClass, typename QueryFunction>
std::optional<quint64> readNetworkTableCount(QueryFunction query, ULONG family, TableClass tableClass)
{
    DWORD byteCount = 0;
    DWORD status = query(nullptr, &byteCount, FALSE, family, tableClass, 0);
    if (status == NO_ERROR && byteCount == 0) return quint64{0};
    if (status != ERROR_INSUFFICIENT_BUFFER || byteCount == 0 || byteCount > 64 * 1024 * 1024)
        return std::nullopt;
    std::vector<std::byte> buffer(byteCount);
    status = query(buffer.data(), &byteCount, FALSE, family, tableClass, 0);
    if (status != NO_ERROR) return std::nullopt;
    return static_cast<quint64>(reinterpret_cast<const TableType*>(buffer.data())->dwNumEntries);
}

std::optional<quint64> readTcpEntryCount(ULONG family)
{
    if (family == AF_INET)
        return readNetworkTableCount<MIB_TCPTABLE_OWNER_PID, TCP_TABLE_CLASS>(
            [](PVOID table, PDWORD size, BOOL order, ULONG af, TCP_TABLE_CLASS type, ULONG reserved) {
                return GetExtendedTcpTable(table, size, order, af, type, reserved);
            }, family, TCP_TABLE_OWNER_PID_ALL);
    return readNetworkTableCount<MIB_TCP6TABLE_OWNER_PID, TCP_TABLE_CLASS>(
        [](PVOID table, PDWORD size, BOOL order, ULONG af, TCP_TABLE_CLASS type, ULONG reserved) {
            return GetExtendedTcpTable(table, size, order, af, type, reserved);
        }, family, TCP_TABLE_OWNER_PID_ALL);
}

std::optional<quint64> readUdpEndpointCount(ULONG family)
{
    if (family == AF_INET)
        return readNetworkTableCount<MIB_UDPTABLE_OWNER_PID, UDP_TABLE_CLASS>(
            [](PVOID table, PDWORD size, BOOL order, ULONG af, UDP_TABLE_CLASS type, ULONG reserved) {
                return GetExtendedUdpTable(table, size, order, af, type, reserved);
            }, family, UDP_TABLE_OWNER_PID);
    return readNetworkTableCount<MIB_UDP6TABLE_OWNER_PID, UDP_TABLE_CLASS>(
        [](PVOID table, PDWORD size, BOOL order, ULONG af, UDP_TABLE_CLASS type, ULONG reserved) {
            return GetExtendedUdpTable(table, size, order, af, type, reserved);
        }, family, UDP_TABLE_OWNER_PID);
}

template <typename TableType, typename TableClass, typename QueryFunction>
std::optional<QHash<quint32, quint32>> readNetworkProcessCounts(
    QueryFunction query, ULONG family, TableClass tableClass)
{
    DWORD byteCount = 0;
    DWORD status = query(nullptr, &byteCount, FALSE, family, tableClass, 0);
    if (status == NO_ERROR && byteCount == 0) return QHash<quint32, quint32>{};
    if (status != ERROR_INSUFFICIENT_BUFFER || byteCount == 0 || byteCount > 64 * 1024 * 1024)
        return std::nullopt;
    std::vector<std::byte> buffer(byteCount);
    status = query(buffer.data(), &byteCount, FALSE, family, tableClass, 0);
    if (status != NO_ERROR) return std::nullopt;

    const auto* table = reinterpret_cast<const TableType*>(buffer.data());
    QHash<quint32, quint32> counts;
    for (DWORD index = 0; index < table->dwNumEntries; ++index)
        ++counts[table->table[index].dwOwningPid];
    return counts;
}

template <typename TableType, typename TableClass, typename QueryFunction>
std::optional<QHash<quint32, quint32>> readOwnedTcpTable(QueryFunction query, ULONG family,
                                                         TableClass tableClass)
{
    return readNetworkProcessCounts<TableType>(query, family, tableClass);
}

std::optional<QHash<quint32, quint32>> readTcpProcessCounts(ULONG family)
{
    if (family == AF_INET)
        return readOwnedTcpTable<MIB_TCPTABLE_OWNER_PID>(
            [](PVOID table, PDWORD size, BOOL order, ULONG af, TCP_TABLE_CLASS type, ULONG reserved) {
                return GetExtendedTcpTable(table, size, order, af, type, reserved);
            }, family, TCP_TABLE_OWNER_PID_ALL);
    return readOwnedTcpTable<MIB_TCP6TABLE_OWNER_PID>(
        [](PVOID table, PDWORD size, BOOL order, ULONG af, TCP_TABLE_CLASS type, ULONG reserved) {
            return GetExtendedTcpTable(table, size, order, af, type, reserved);
        }, family, TCP_TABLE_OWNER_PID_ALL);
}

std::optional<QHash<quint32, quint32>> readUdpProcessCounts(ULONG family)
{
    if (family == AF_INET)
        return readNetworkProcessCounts<MIB_UDPTABLE_OWNER_PID>(
            [](PVOID table, PDWORD size, BOOL order, ULONG af, UDP_TABLE_CLASS type, ULONG reserved) {
                return GetExtendedUdpTable(table, size, order, af, type, reserved);
            }, family, UDP_TABLE_OWNER_PID);
    return readNetworkProcessCounts<MIB_UDP6TABLE_OWNER_PID>(
        [](PVOID table, PDWORD size, BOOL order, ULONG af, UDP_TABLE_CLASS type, ULONG reserved) {
            return GetExtendedUdpTable(table, size, order, af, type, reserved);
        }, family, UDP_TABLE_OWNER_PID);
}

std::optional<QHash<quint32, quint32>> combineProcessCounts(
    const std::optional<QHash<quint32, quint32>>& first,
    const std::optional<QHash<quint32, quint32>>& second)
{
    if (!first || !second) return std::nullopt;
    QHash<quint32, quint32> combined = *first;
    for (auto it = second->cbegin(); it != second->cend(); ++it)
        combined[it.key()] += it.value();
    return combined;
}

std::optional<quint64> unsignedVariant(const VARIANT& value)
{
    switch (value.vt) {
    case VT_UI1: return value.bVal;
    case VT_UI2: return value.uiVal;
    case VT_UI4: return value.ulVal;
    case VT_UI8: return value.ullVal;
    case VT_I1: return value.cVal >= 0 ? std::optional<quint64>(static_cast<quint64>(value.cVal)) : std::nullopt;
    case VT_I2: return value.iVal >= 0 ? std::optional<quint64>(static_cast<quint64>(value.iVal)) : std::nullopt;
    case VT_I4: return value.lVal >= 0 ? std::optional<quint64>(static_cast<quint64>(value.lVal)) : std::nullopt;
    case VT_I8: return value.llVal >= 0 ? std::optional<quint64>(static_cast<quint64>(value.llVal)) : std::nullopt;
    default: return std::nullopt;
    }
}

std::optional<quint64> readUnsignedProperty(IWbemClassObject* object, const wchar_t* name)
{
    VARIANT value{};
    VariantInit(&value);
    const HRESULT result = object->Get(name, 0, &value, nullptr, nullptr);
    const auto converted = SUCCEEDED(result) ? unsignedVariant(value) : std::nullopt;
    VariantClear(&value);
    return converted;
}

QString readStringProperty(IWbemClassObject* object, const wchar_t* name)
{
    VARIANT value{};
    VariantInit(&value);
    const HRESULT result = object->Get(name, 0, &value, nullptr, nullptr);
    const QString converted = SUCCEEDED(result) && value.vt == VT_BSTR
        ? QString::fromWCharArray(value.bstrVal).trimmed() : QString();
    VariantClear(&value);
    return converted;
}

std::optional<bool> readBoolProperty(IWbemClassObject* object, const wchar_t* name)
{
    VARIANT value{};
    VariantInit(&value);
    const HRESULT result = object->Get(name, 0, &value, nullptr, nullptr);
    const std::optional<bool> converted = SUCCEEDED(result) && value.vt == VT_BOOL
        ? std::optional<bool>(value.boolVal != VARIANT_FALSE) : std::nullopt;
    VariantClear(&value);
    return converted;
}

} // namespace

struct SystemCollector::Impl {
    PDH_HQUERY gpuQuery = nullptr;
    PDH_HCOUNTER gpuCounter = nullptr;
    bool gpuCounterAvailable = false;
    PDH_HQUERY processorQuery = nullptr;
    PDH_HCOUNTER processorCounter = nullptr;
    bool processorCounterAvailable = false;
    PDH_HQUERY diskQuery = nullptr;
    PDH_HCOUNTER diskReadCounter = nullptr;
    PDH_HCOUNTER diskWriteCounter = nullptr;
    bool diskCountersAvailable = false;

    bool hasCpuBaseline = false;
    quint64 previousIdle = 0;
    quint64 previousKernel = 0;
    quint64 previousUser = 0;

    bool hasNetworkBaseline = false;
    quint64 previousReceiveBytes = 0;
    quint64 previousSendBytes = 0;
    std::chrono::steady_clock::time_point previousNetworkAt;
    ULONG activeNetworkInterfaceCount = 0;
    std::chrono::steady_clock::time_point connectionRefreshAt;
    std::optional<quint64> tcpEntryCount;
    std::optional<quint64> udpEndpointCount;
    std::optional<QHash<quint32, quint32>> tcpConnectionsByProcess;
    std::optional<QHash<quint32, quint32>> udpEndpointsByProcess;

    QString processorName = readProcessorName();
    QString graphicsName;
    quint64 graphicsMemoryBytes = 0;
    QVector<GraphicsAdapterSample> graphicsAdapters;
    QString systemManufacturer = readBiosValue(L"SystemManufacturer");
    QString systemModel = readBiosValue(L"SystemProductName");
    QString biosVendor = readBiosValue(L"BIOSVendor");
    QString biosVersion = readBiosValue(L"BIOSVersion");
    QString biosReleaseDate = readBiosValue(L"BIOSReleaseDate");
    QVector<PhysicalDiskSample> physicalDisks;
    std::chrono::steady_clock::time_point storageReliabilityRefreshAt;
    std::optional<quint64> batteryDesignCapacityMwh;
    std::optional<quint64> batteryFullChargeCapacityMwh;
    std::chrono::steady_clock::time_point batteryCapacityRefreshAt;
    QVector<ThermalSensorSample> thermalSensors;
    QString thermalSensorStatus = QStringLiteral("Thermal sensor status has not been checked yet.");
    std::chrono::steady_clock::time_point thermalRefreshAt;
    QHash<QString, std::chrono::steady_clock::time_point> abovePassiveTripSince;
    QVector<FanSample> fans;
    std::chrono::steady_clock::time_point fanRefreshAt;
    QHash<QString, quint64> previousFanSpeeds;
    QHash<QString, QDateTime> fanSpeedChangedAt;
    QHash<QString, QString> physicalDisksByVolume;
    std::unordered_map<DWORD, PreviousProcessSample> processBaselines;
    std::unordered_map<DWORD, PreviousProcessSample> processBaselinesScratch;
    QVector<ProcessSample> previousTopProcesses;
    QDateTime processSamplesCapturedAt;
    QString processCollectionStatus;
    std::chrono::steady_clock::time_point previousProcessCollectionAt;
    bool hasProcessSnapshot = false;

    Impl()
    {
        graphicsAdapters = readGraphicsAdapters();
        if (!graphicsAdapters.isEmpty()) {
            graphicsName = graphicsAdapters.first().name;
            graphicsMemoryBytes = graphicsAdapters.first().dedicatedMemoryBytes;
        }

        if (PdhOpenQueryW(nullptr, 0, &processorQuery) == ERROR_SUCCESS) {
            constexpr wchar_t kProcessorCounterPath[] = L"\\Processor Information(*)\\% Processor Time";
            processorCounterAvailable = PdhAddEnglishCounterW(
                processorQuery, kProcessorCounterPath, 0, &processorCounter) == ERROR_SUCCESS;
            if (!processorCounterAvailable) {
                PdhCloseQuery(processorQuery);
                processorQuery = nullptr;
            }
        }

        if (PdhOpenQueryW(nullptr, 0, &diskQuery) == ERROR_SUCCESS) {
            constexpr wchar_t kDiskReadCounterPath[] = L"\\PhysicalDisk(_Total)\\Disk Read Bytes/sec";
            constexpr wchar_t kDiskWriteCounterPath[] = L"\\PhysicalDisk(_Total)\\Disk Write Bytes/sec";
            diskCountersAvailable =
                PdhAddEnglishCounterW(diskQuery, kDiskReadCounterPath, 0, &diskReadCounter) == ERROR_SUCCESS &&
                PdhAddEnglishCounterW(diskQuery, kDiskWriteCounterPath, 0, &diskWriteCounter) == ERROR_SUCCESS;
            if (!diskCountersAvailable) {
                PdhCloseQuery(diskQuery);
                diskQuery = nullptr;
                diskReadCounter = nullptr;
                diskWriteCounter = nullptr;
            }
        }

        for (DWORD number = 0; number < 32; ++number) {
            PhysicalDiskSample disk = readPhysicalDisk(number);
            if (!disk.model.isEmpty() || !disk.vendor.isEmpty()) physicalDisks.append(std::move(disk));
        }
        const DWORD logicalDrives = GetLogicalDrives();
        for (unsigned int index = 0; index < 26; ++index) {
            if ((logicalDrives & (1u << index)) == 0) continue;
            std::array<wchar_t, 4> root{
                static_cast<wchar_t>(L'A' + index), L':', L'\\', L'\0'
            };
            if (GetDriveTypeW(root.data()) == DRIVE_FIXED) {
                const QString rootPath = QString::fromWCharArray(root.data());
                physicalDisksByVolume.insert(rootPath, physicalDisksForVolume(rootPath));
            }
        }

        if (PdhOpenQueryW(nullptr, 0, &gpuQuery) == ERROR_SUCCESS) {
            constexpr wchar_t kGpuCounterPath[] = L"\\GPU Engine(*)\\Utilization Percentage";
            gpuCounterAvailable = PdhAddEnglishCounterW(gpuQuery, kGpuCounterPath, 0, &gpuCounter) == ERROR_SUCCESS;
            if (!gpuCounterAvailable) {
                PdhCloseQuery(gpuQuery);
                gpuQuery = nullptr;
            }
        }
    }

    ~Impl()
    {
        if (gpuQuery) {
            PdhCloseQuery(gpuQuery);
        }
        if (processorQuery) PdhCloseQuery(processorQuery);
        if (diskQuery) PdhCloseQuery(diskQuery);
    }

    std::optional<double> readGpuUsage()
    {
        if (!gpuCounterAvailable || !gpuQuery || PdhCollectQueryData(gpuQuery) != ERROR_SUCCESS) {
            return std::nullopt;
        }

        DWORD byteCount = 0;
        DWORD itemCount = 0;
        const PDH_STATUS sizingStatus = PdhGetFormattedCounterArrayW(
            gpuCounter, PDH_FMT_DOUBLE, &byteCount, &itemCount, nullptr);
        if (sizingStatus != PDH_MORE_DATA || byteCount == 0 || itemCount == 0) {
            return std::nullopt;
        }

        std::vector<std::byte> storage(byteCount);
        auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(storage.data());
        if (PdhGetFormattedCounterArrayW(gpuCounter, PDH_FMT_DOUBLE, &byteCount, &itemCount, items) != ERROR_SUCCESS) {
            return std::nullopt;
        }

        double busiestEngine = -1.0;
        for (DWORD i = 0; i < itemCount; ++i) {
            const auto& value = items[i].FmtValue;
            if (value.CStatus == ERROR_SUCCESS && std::isfinite(value.doubleValue)) {
                busiestEngine = std::max(busiestEngine, value.doubleValue);
            }
        }
        if (busiestEngine < 0.0) {
            return std::nullopt;
        }

        return std::clamp(busiestEngine, 0.0, 100.0);
    }

    QVector<ProcessorCoreSample> readPerCoreCpuUsage()
    {
        QVector<ProcessorCoreSample> result;
        if (!processorCounterAvailable || !processorQuery ||
            PdhCollectQueryData(processorQuery) != ERROR_SUCCESS) return result;

        DWORD byteCount = 0;
        DWORD itemCount = 0;
        const PDH_STATUS sizingStatus = PdhGetFormattedCounterArrayW(
            processorCounter, PDH_FMT_DOUBLE, &byteCount, &itemCount, nullptr);
        if (sizingStatus != PDH_MORE_DATA || byteCount == 0 || itemCount == 0) return result;
        std::vector<std::byte> storage(byteCount);
        auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(storage.data());
        if (PdhGetFormattedCounterArrayW(processorCounter, PDH_FMT_DOUBLE,
                &byteCount, &itemCount, items) != ERROR_SUCCESS) return result;

        result.reserve(static_cast<qsizetype>(itemCount));
        for (DWORD index = 0; index < itemCount; ++index) {
            const QString name = QString::fromWCharArray(items[index].szName);
            if (name.compare(QStringLiteral("_Total"), Qt::CaseInsensitive) == 0) continue;
            ProcessorCoreSample sample;
            sample.name = name;
            const PDH_FMT_COUNTERVALUE& value = items[index].FmtValue;
            if (value.CStatus == ERROR_SUCCESS && std::isfinite(value.doubleValue) &&
                value.doubleValue >= 0.0 && value.doubleValue <= 100.0)
                sample.utilizationPercent = value.doubleValue;
            result.append(std::move(sample));
        }
        return result;
    }

    DiskActivitySample readDiskActivity()
    {
        DiskActivitySample result;
        if (!diskCountersAvailable || !diskQuery || PdhCollectQueryData(diskQuery) != ERROR_SUCCESS)
            return result;
        const auto readRate = [](PDH_HCOUNTER counter) -> std::optional<double> {
            PDH_FMT_COUNTERVALUE value{};
            if (PdhGetFormattedCounterValue(counter, PDH_FMT_DOUBLE, nullptr, &value) != ERROR_SUCCESS ||
                value.CStatus != ERROR_SUCCESS || !std::isfinite(value.doubleValue) || value.doubleValue < 0.0)
                return std::nullopt;
            return value.doubleValue;
        };
        result.readBytesPerSecond = readRate(diskReadCounter);
        result.writeBytesPerSecond = readRate(diskWriteCounter);
        return result;
    }

    void refreshBatteryCapacities()
    {
        const auto now = std::chrono::steady_clock::now();
        if (batteryCapacityRefreshAt.time_since_epoch().count() != 0 &&
            now - batteryCapacityRefreshAt < std::chrono::minutes(1)) return;
        batteryCapacityRefreshAt = now;
        batteryDesignCapacityMwh.reset();
        batteryFullChargeCapacityMwh.reset();

        const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const bool uninitializeCom = comResult == S_OK || comResult == S_FALSE;
        if (FAILED(comResult) && comResult != RPC_E_CHANGED_MODE) {
            // Keep the last known device-reported values on a transient COM
            // failure and retry sooner than the normal five-minute cadence.
            storageReliabilityRefreshAt = now - std::chrono::minutes(4) - std::chrono::seconds(30);
            return;
        }

        Microsoft::WRL::ComPtr<IWbemLocator> locator;
        HRESULT result = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
            IID_IWbemLocator, reinterpret_cast<void**>(locator.GetAddressOf()));
        Microsoft::WRL::ComPtr<IWbemServices> services;
        BSTR namespaceName = SysAllocString(L"ROOT\\WMI");
        if (SUCCEEDED(result) && namespaceName) {
            result = locator->ConnectServer(namespaceName, nullptr, nullptr, nullptr,
                0, nullptr, nullptr, services.GetAddressOf());
        } else if (SUCCEEDED(result)) {
            result = E_OUTOFMEMORY;
        }
        SysFreeString(namespaceName);
        if (SUCCEEDED(result)) {
            result = CoSetProxyBlanket(services.Get(), RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE,
                nullptr, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
        }

        QHash<quint64, quint64> designByTag;
        QHash<quint64, quint64> fullChargeByTag;
        const auto readClass = [&services, &result](const wchar_t* queryText, const auto& consume) {
            if (FAILED(result)) return;
            Microsoft::WRL::ComPtr<IEnumWbemClassObject> enumerator;
            BSTR language = SysAllocString(L"WQL");
            BSTR query = SysAllocString(queryText);
            if (language && query) {
                result = services->ExecQuery(language, query,
                    WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr,
                    enumerator.GetAddressOf());
            } else {
                result = E_OUTOFMEMORY;
            }
            SysFreeString(language);
            SysFreeString(query);
            if (FAILED(result)) return;
            for (;;) {
                Microsoft::WRL::ComPtr<IWbemClassObject> object;
                ULONG returned = 0;
                result = enumerator->Next(1000, 1, object.GetAddressOf(), &returned);
                if (FAILED(result) || returned == 0) break;
                consume(object.Get());
            }
        };
        readClass(L"SELECT Tag, DesignedCapacity, Capabilities FROM BatteryStaticData",
            [&designByTag](IWbemClassObject* object) {
                const auto tag = readUnsignedProperty(object, L"Tag");
                const auto capacity = readUnsignedProperty(object, L"DesignedCapacity");
                const auto capabilities = readUnsignedProperty(object, L"Capabilities");
                constexpr quint64 kRelativeCapacityFlag = 0x40000000u;
                constexpr quint64 kMaximumCapacityMwh = 100'000'000;
                if (tag && capacity && capabilities && *capacity > 0 && *capacity < kMaximumCapacityMwh &&
                    (*capabilities & kRelativeCapacityFlag) == 0) designByTag.insert(*tag, *capacity);
            });
        readClass(L"SELECT Tag, FullChargedCapacity FROM BatteryFullChargedCapacity",
            [&fullChargeByTag](IWbemClassObject* object) {
                const auto tag = readUnsignedProperty(object, L"Tag");
                const auto capacity = readUnsignedProperty(object, L"FullChargedCapacity");
                constexpr quint64 kMaximumCapacityMwh = 100'000'000;
                if (tag && capacity && *capacity > 0 && *capacity < kMaximumCapacityMwh)
                    fullChargeByTag.insert(*tag, *capacity);
            });

        if (SUCCEEDED(result) && !designByTag.isEmpty() && !fullChargeByTag.isEmpty()) {
            quint64 totalDesign = 0;
            quint64 totalFullCharge = 0;
            bool valid = true;
            for (auto it = designByTag.cbegin(); it != designByTag.cend(); ++it) {
                const auto full = fullChargeByTag.constFind(it.key());
                if (full == fullChargeByTag.cend()) continue;
                if (std::numeric_limits<quint64>::max() - totalDesign < it.value() ||
                    std::numeric_limits<quint64>::max() - totalFullCharge < full.value()) {
                    valid = false;
                    break;
                }
                totalDesign += it.value();
                totalFullCharge += full.value();
            }
            if (valid && totalDesign > 0 && totalFullCharge > 0) {
                batteryDesignCapacityMwh = totalDesign;
                batteryFullChargeCapacityMwh = totalFullCharge;
            }
        }
        if (uninitializeCom) CoUninitialize();
    }

    void refreshStorageReliability()
    {
        const auto now = std::chrono::steady_clock::now();
        if (storageReliabilityRefreshAt.time_since_epoch().count() != 0 &&
            now - storageReliabilityRefreshAt < std::chrono::minutes(5)) return;
        storageReliabilityRefreshAt = now;
        for (PhysicalDiskSample& disk : physicalDisks) {
            disk.windowsHealthStatus.reset();
            disk.mediaType.reset();
            disk.temperatureCelsius.reset();
            disk.maximumTemperatureCelsius.reset();
            disk.wearPercent.reset();
            disk.powerOnHours.reset();
            disk.uncorrectedReadErrors.reset();
            disk.uncorrectedWriteErrors.reset();
        }

        const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const bool uninitializeCom = comResult == S_OK || comResult == S_FALSE;
        if (FAILED(comResult) && comResult != RPC_E_CHANGED_MODE) return;

        Microsoft::WRL::ComPtr<IWbemLocator> locator;
        HRESULT result = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
            IID_IWbemLocator, reinterpret_cast<void**>(locator.GetAddressOf()));
        Microsoft::WRL::ComPtr<IWbemServices> services;
        BSTR namespaceName = SysAllocString(L"ROOT\\Microsoft\\Windows\\Storage");
        if (SUCCEEDED(result) && namespaceName) {
            result = locator->ConnectServer(namespaceName, nullptr, nullptr, nullptr,
                0, nullptr, nullptr, services.GetAddressOf());
        } else if (SUCCEEDED(result)) {
            result = E_OUTOFMEMORY;
        }
        SysFreeString(namespaceName);
        if (SUCCEEDED(result)) {
            result = CoSetProxyBlanket(services.Get(), RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE,
                nullptr, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
        }
        bool queryFailure = FAILED(result);
        if (SUCCEEDED(result)) {
            Microsoft::WRL::ComPtr<IEnumWbemClassObject> healthEnumerator;
            BSTR healthLanguage = SysAllocString(L"WQL");
            BSTR healthQuery = SysAllocString(L"SELECT DeviceId, HealthStatus, MediaType FROM MSFT_PhysicalDisk");
            HRESULT healthResult = healthLanguage && healthQuery
                ? services->ExecQuery(healthLanguage, healthQuery,
                    WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr,
                    healthEnumerator.GetAddressOf())
                : E_OUTOFMEMORY;
            SysFreeString(healthLanguage);
            SysFreeString(healthQuery);
            if (SUCCEEDED(healthResult)) {
                for (PhysicalDiskSample& disk : physicalDisks) {
                    disk.windowsHealthStatus.reset();
                    disk.mediaType.reset();
                }
                for (;;) {
                    Microsoft::WRL::ComPtr<IWbemClassObject> object;
                    ULONG returned = 0;
                    healthResult = healthEnumerator->Next(1000, 1, object.GetAddressOf(), &returned);
                    if (FAILED(healthResult) || returned == 0) break;
                    bool deviceIdOk = false;
                    const unsigned int deviceId = readStringProperty(object.Get(), L"DeviceId").toUInt(&deviceIdOk);
                    if (!deviceIdOk) continue;
                    const auto disk = std::find_if(physicalDisks.begin(), physicalDisks.end(),
                        [deviceId](const PhysicalDiskSample& sample) { return sample.deviceNumber == deviceId; });
                    if (disk == physicalDisks.end()) continue;
                    const auto health = readUnsignedProperty(object.Get(), L"HealthStatus");
                    if (health && (*health == 0 || *health == 1 || *health == 2 || *health == 5))
                        disk->windowsHealthStatus = static_cast<unsigned int>(*health);
                    const auto media = readUnsignedProperty(object.Get(), L"MediaType");
                    if (media && (*media == 0 || *media == 3 || *media == 4 || *media == 5))
                        disk->mediaType = static_cast<unsigned int>(*media);
                }
            } else {
                queryFailure = true;
            }
        }
        Microsoft::WRL::ComPtr<IEnumWbemClassObject> enumerator;
        BSTR language = SysAllocString(L"WQL");
        BSTR query = SysAllocString(L"SELECT DeviceId, Temperature, TemperatureMax, Wear, PowerOnHours, ReadErrorsUncorrected, WriteErrorsUncorrected FROM MSFT_StorageReliabilityCounter");
        if (SUCCEEDED(result) && language && query) {
            result = services->ExecQuery(language, query,
                WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr,
                enumerator.GetAddressOf());
            if (SUCCEEDED(result)) {
                for (PhysicalDiskSample& disk : physicalDisks) {
                    disk.temperatureCelsius.reset();
                    disk.maximumTemperatureCelsius.reset();
                    disk.wearPercent.reset();
                    disk.powerOnHours.reset();
                    disk.uncorrectedReadErrors.reset();
                    disk.uncorrectedWriteErrors.reset();
                }
            } else {
                queryFailure = true;
            }
        } else if (SUCCEEDED(result)) {
            result = E_OUTOFMEMORY;
            queryFailure = true;
        } else {
            queryFailure = true;
        }
        SysFreeString(language);
        SysFreeString(query);

        if (SUCCEEDED(result)) {
            for (;;) {
                Microsoft::WRL::ComPtr<IWbemClassObject> object;
                ULONG returned = 0;
                result = enumerator->Next(1000, 1, object.GetAddressOf(), &returned);
                if (FAILED(result) || returned == 0) break;
                bool deviceIdOk = false;
                const unsigned int deviceId = readStringProperty(object.Get(), L"DeviceId").toUInt(&deviceIdOk);
                if (!deviceIdOk) continue;
                const auto disk = std::find_if(physicalDisks.begin(), physicalDisks.end(),
                    [deviceId](const PhysicalDiskSample& sample) { return sample.deviceNumber == deviceId; });
                if (disk == physicalDisks.end()) continue;

                const auto temperature = readUnsignedProperty(object.Get(), L"Temperature");
                if (temperature && *temperature <= 150) disk->temperatureCelsius = static_cast<unsigned int>(*temperature);
                const auto maximumTemperature = readUnsignedProperty(object.Get(), L"TemperatureMax");
                if (maximumTemperature && *maximumTemperature > 0 && *maximumTemperature <= 200)
                    disk->maximumTemperatureCelsius = static_cast<unsigned int>(*maximumTemperature);
                const auto wear = readUnsignedProperty(object.Get(), L"Wear");
                if (wear && *wear <= 100) disk->wearPercent = static_cast<unsigned int>(*wear);
                disk->powerOnHours = readUnsignedProperty(object.Get(), L"PowerOnHours");
                disk->uncorrectedReadErrors = readUnsignedProperty(object.Get(), L"ReadErrorsUncorrected");
                disk->uncorrectedWriteErrors = readUnsignedProperty(object.Get(), L"WriteErrorsUncorrected");
            }
        }
        if (uninitializeCom) CoUninitialize();
        // Storage providers can briefly fail during resume or device changes.
        // Retry after 30 seconds instead of hiding a prior valid reading for
        // the full normal refresh interval.
        storageReliabilityRefreshAt = queryFailure
            ? now - std::chrono::minutes(4) - std::chrono::seconds(30)
            : now;
    }

    void refreshThermalSensors()
    {
        const auto now = std::chrono::steady_clock::now();
        if (thermalRefreshAt.time_since_epoch().count() != 0 &&
            now - thermalRefreshAt < std::chrono::seconds(10)) return;
        thermalRefreshAt = now;
        thermalSensors.clear();
        thermalSensorStatus = QStringLiteral("Checking the Windows ACPI thermal-zone provider.");
        QSet<QString> observedZones;

        const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const bool uninitializeCom = comResult == S_OK || comResult == S_FALSE;
        if (FAILED(comResult) && comResult != RPC_E_CHANGED_MODE) {
            thermalSensorStatus = QStringLiteral("Windows denied access while initializing its thermal-sensor service (0x%1).")
                .arg(QString::number(static_cast<quint32>(comResult), 16).toUpper());
            return;
        }

        Microsoft::WRL::ComPtr<IWbemLocator> locator;
        HRESULT result = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
            IID_IWbemLocator, reinterpret_cast<void**>(locator.GetAddressOf()));
        Microsoft::WRL::ComPtr<IWbemServices> services;
        BSTR namespaceName = SysAllocString(L"ROOT\\WMI");
        if (SUCCEEDED(result) && namespaceName) {
            result = locator->ConnectServer(namespaceName, nullptr, nullptr, nullptr,
                0, nullptr, nullptr, services.GetAddressOf());
        } else {
            result = E_OUTOFMEMORY;
        }
        SysFreeString(namespaceName);

        if (SUCCEEDED(result)) {
            result = CoSetProxyBlanket(services.Get(), RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE,
                nullptr, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
        }
        Microsoft::WRL::ComPtr<IEnumWbemClassObject> enumerator;
        BSTR language = SysAllocString(L"WQL");
        BSTR query = SysAllocString(L"SELECT CurrentTemperature, InstanceName, PassiveTripPoint, CriticalTripPoint FROM MSAcpi_ThermalZoneTemperature");
        if (SUCCEEDED(result) && language && query) {
            result = services->ExecQuery(language, query,
                WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr,
                enumerator.GetAddressOf());
        } else if (SUCCEEDED(result)) {
            result = E_OUTOFMEMORY;
        }
        SysFreeString(language);
        SysFreeString(query);

        if (SUCCEEDED(result)) {
            for (;;) {
                Microsoft::WRL::ComPtr<IWbemClassObject> object;
                ULONG returned = 0;
                result = enumerator->Next(500, 1, object.GetAddressOf(), &returned);
                if (FAILED(result) || returned == 0) break;
                VARIANT temperature{};
                VARIANT instance{};
                VariantInit(&temperature);
                VariantInit(&instance);
                const HRESULT temperatureResult = object->Get(L"CurrentTemperature", 0, &temperature, nullptr, nullptr);
                const HRESULT instanceResult = object->Get(L"InstanceName", 0, &instance, nullptr, nullptr);
                const auto currentTenthsKelvin = SUCCEEDED(temperatureResult)
                    ? unsignedVariant(temperature) : std::nullopt;
                if (currentTenthsKelvin) {
                    const double kelvinTenths = static_cast<double>(*currentTenthsKelvin);
                    const double celsius = kelvinTenths / 10.0 - 273.15;
                    if (std::isfinite(celsius) && celsius >= -100.0 && celsius <= 250.0) {
                        ThermalSensorSample sample;
                        sample.instanceName = SUCCEEDED(instanceResult) && instance.vt == VT_BSTR
                            ? QString::fromWCharArray(instance.bstrVal).left(256) : QString();
                        sample.name = sample.instanceName.isEmpty()
                            ? QStringLiteral("ACPI thermal zone %1").arg(thermalSensors.size() + 1)
                            : QString::fromWCharArray(instance.bstrVal).section(QLatin1Char('\\'), -1).left(96);
                        sample.source = QStringLiteral("Windows ACPI thermal zone");
                        sample.temperatureCelsius = celsius;
                        const auto tripPointCelsius = [&object](const wchar_t* property) -> std::optional<double> {
                            const auto raw = readUnsignedProperty(object.Get(), property);
                            if (!raw || *raw == 0 || *raw >= std::numeric_limits<quint32>::max())
                                return std::nullopt;
                            const double value = static_cast<double>(*raw) / 10.0 - 273.15;
                            return std::isfinite(value) && value >= -100.0 && value <= 250.0
                                ? std::optional<double>(value) : std::nullopt;
                        };
                        sample.passiveTripPointCelsius = tripPointCelsius(L"PassiveTripPoint");
                        sample.criticalTripPointCelsius = tripPointCelsius(L"CriticalTripPoint");
                        if (!sample.instanceName.isEmpty()) {
                            observedZones.insert(sample.instanceName);
                            if (sample.passiveTripPointCelsius &&
                                celsius >= *sample.passiveTripPointCelsius) {
                                if (!abovePassiveTripSince.contains(sample.instanceName))
                                    abovePassiveTripSince.insert(sample.instanceName, now);
                                const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                                    now - abovePassiveTripSince.value(sample.instanceName)).count();
                                sample.secondsAbovePassiveTripPoint = static_cast<quint32>(
                                    std::clamp<qint64>(elapsed, 0, std::numeric_limits<quint32>::max()));
                            } else {
                                abovePassiveTripSince.remove(sample.instanceName);
                            }
                        }
                        thermalSensors.append(std::move(sample));
                    }
                }
                VariantClear(&temperature);
                VariantClear(&instance);
                if (thermalSensors.size() >= 32) break;
            }
        }
        for (auto it = abovePassiveTripSince.begin(); it != abovePassiveTripSince.end();) {
            if (!observedZones.contains(it.key())) it = abovePassiveTripSince.erase(it);
            else ++it;
        }
        if (uninitializeCom) CoUninitialize();
        if (result == WBEM_E_ACCESS_DENIED || result == E_ACCESSDENIED) {
            thermalSensorStatus = QStringLiteral("Windows denied access to ROOT\\WMI thermal-zone data for this account. Ausyn will not elevate itself or install a hardware driver.");
        } else if (FAILED(result)) {
            thermalSensorStatus = QStringLiteral("The Windows ACPI thermal-zone query failed (0x%1). No temperature was assumed.")
                .arg(QString::number(static_cast<quint32>(result), 16).toUpper());
        } else if (thermalSensors.isEmpty()) {
            thermalSensorStatus = QStringLiteral("Windows and this device's firmware exposed no ACPI thermal-zone readings. CPU/GPU temperatures need a supported device-specific source.");
        } else {
            thermalSensorStatus = QStringLiteral("Windows exposed %1 ACPI thermal-zone reading(s). These may not be CPU/GPU die temperatures.")
                .arg(thermalSensors.size());
        }
    }

    void refreshFans()
    {
        const auto now = std::chrono::steady_clock::now();
        if (fanRefreshAt.time_since_epoch().count() != 0 &&
            now - fanRefreshAt < std::chrono::seconds(10)) return;
        fanRefreshAt = now;
        fans.clear();

        const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const bool uninitializeCom = comResult == S_OK || comResult == S_FALSE;
        if (FAILED(comResult) && comResult != RPC_E_CHANGED_MODE) return;
        Microsoft::WRL::ComPtr<IWbemLocator> locator;
        HRESULT result = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
            IID_IWbemLocator, reinterpret_cast<void**>(locator.GetAddressOf()));
        Microsoft::WRL::ComPtr<IWbemServices> services;
        BSTR namespaceName = SysAllocString(L"ROOT\\CIMV2");
        if (SUCCEEDED(result) && namespaceName) {
            result = locator->ConnectServer(namespaceName, nullptr, nullptr, nullptr,
                0, nullptr, nullptr, services.GetAddressOf());
        } else if (SUCCEEDED(result)) {
            result = E_OUTOFMEMORY;
        }
        SysFreeString(namespaceName);
        if (SUCCEEDED(result)) {
            result = CoSetProxyBlanket(services.Get(), RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE,
                nullptr, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
        }
        Microsoft::WRL::ComPtr<IEnumWbemClassObject> enumerator;
        BSTR language = SysAllocString(L"WQL");
        BSTR query = SysAllocString(L"SELECT Name, DeviceID, DesiredSpeed, ActiveCooling, VariableSpeed, Status FROM Win32_Fan");
        if (SUCCEEDED(result) && language && query) {
            result = services->ExecQuery(language, query,
                WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr,
                enumerator.GetAddressOf());
        } else if (SUCCEEDED(result)) {
            result = E_OUTOFMEMORY;
        }
        SysFreeString(language);
        SysFreeString(query);

        if (SUCCEEDED(result)) {
            for (;;) {
                Microsoft::WRL::ComPtr<IWbemClassObject> object;
                ULONG returned = 0;
                result = enumerator->Next(500, 1, object.GetAddressOf(), &returned);
                if (FAILED(result) || returned == 0) break;
                FanSample sample;
                sample.name = readStringProperty(object.Get(), L"Name");
                sample.deviceId = readStringProperty(object.Get(), L"DeviceID").left(256);
                if (sample.name.isEmpty()) sample.name = sample.deviceId.isEmpty()
                    ? QStringLiteral("Cooling fan %1").arg(fans.size() + 1) : sample.deviceId;
                sample.status = readStringProperty(object.Get(), L"Status").left(80);
                sample.activeCooling = readBoolProperty(object.Get(), L"ActiveCooling");
                sample.variableSpeed = readBoolProperty(object.Get(), L"VariableSpeed");
                const auto desiredSpeed = readUnsignedProperty(object.Get(), L"DesiredSpeed");
                if (desiredSpeed && *desiredSpeed <= 100'000) {
                    sample.requestedSpeedRpm = *desiredSpeed;
                    const QString key = sample.deviceId.isEmpty() ? sample.name : sample.deviceId;
                    if (previousFanSpeeds.contains(key) && previousFanSpeeds.value(key) != *desiredSpeed) {
                        sample.previousRequestedSpeedRpm = previousFanSpeeds.value(key);
                        fanSpeedChangedAt.insert(key, QDateTime::currentDateTime());
                    }
                    previousFanSpeeds.insert(key, *desiredSpeed);
                    sample.requestedSpeedChangedAt = fanSpeedChangedAt.value(key);
                }
                fans.append(std::move(sample));
                if (fans.size() >= 16) break;
            }
        }
        if (uninitializeCom) CoUninitialize();
    }

    void refreshNetworkConnectionCounts()
    {
        const auto now = std::chrono::steady_clock::now();
        if (connectionRefreshAt.time_since_epoch().count() != 0 &&
            now - connectionRefreshAt < std::chrono::seconds(5)) return;
        connectionRefreshAt = now;
        const auto tcp4 = readTcpEntryCount(AF_INET);
        const auto tcp6 = readTcpEntryCount(AF_INET6);
        const auto udp4 = readUdpEndpointCount(AF_INET);
        const auto udp6 = readUdpEndpointCount(AF_INET6);
        tcpEntryCount = tcp4 && tcp6 ? std::optional<quint64>(*tcp4 + *tcp6) : std::nullopt;
        udpEndpointCount = udp4 && udp6 ? std::optional<quint64>(*udp4 + *udp6) : std::nullopt;
        tcpConnectionsByProcess = combineProcessCounts(readTcpProcessCounts(AF_INET),
                                                       readTcpProcessCounts(AF_INET6));
        udpEndpointsByProcess = combineProcessCounts(readUdpProcessCounts(AF_INET),
                                                     readUdpProcessCounts(AF_INET6));
    }
};

SystemCollector::SystemCollector()
    : impl_(std::make_unique<Impl>())
{
}

SystemCollector::~SystemCollector() = default;

SystemSnapshot SystemCollector::collect()
{
    SystemSnapshot snapshot;
    snapshot.capturedAt = QDateTime::currentDateTime();
    snapshot.deviceName = QSysInfo::machineHostName();
    snapshot.operatingSystem = QSysInfo::prettyProductName();
    snapshot.operatingSystemVersion = QSysInfo::kernelVersion();
    snapshot.systemUptimeSeconds = GetTickCount64() / 1000;
    snapshot.systemBootTime = snapshot.capturedAt.addSecs(-static_cast<qint64>(snapshot.systemUptimeSeconds));
    snapshot.processorName = impl_->processorName;
    snapshot.graphicsName = impl_->graphicsName;
    snapshot.systemManufacturer = impl_->systemManufacturer;
    snapshot.systemModel = impl_->systemModel;
    snapshot.biosVendor = impl_->biosVendor;
    snapshot.biosVersion = impl_->biosVersion;
    snapshot.biosReleaseDate = impl_->biosReleaseDate;
    impl_->refreshStorageReliability();
    snapshot.physicalDisks = impl_->physicalDisks;
    impl_->refreshThermalSensors();
    snapshot.thermalSensors = impl_->thermalSensors;
    snapshot.thermalSensorStatus = impl_->thermalSensorStatus;
    impl_->refreshFans();
    snapshot.fans = impl_->fans;
    snapshot.graphicsAdapters = readGraphicsAdapters();
    if (!snapshot.graphicsAdapters.isEmpty()) {
        snapshot.graphicsName = snapshot.graphicsAdapters.first().name;
        snapshot.graphicsMemoryBytes = snapshot.graphicsAdapters.first().dedicatedMemoryBytes;
    }
    snapshot.graphicsMemoryBytes = impl_->graphicsMemoryBytes;

    SYSTEM_INFO systemInfo{};
    GetNativeSystemInfo(&systemInfo);
    snapshot.logicalProcessorCount = std::max<DWORD>(systemInfo.dwNumberOfProcessors, 1);
    snapshot.operatingSystemArchitecture = processorArchitectureName(systemInfo.wProcessorArchitecture);

    MEMORYSTATUSEX memoryStatus{};
    memoryStatus.dwLength = sizeof(memoryStatus);
    if (GlobalMemoryStatusEx(&memoryStatus)) {
        snapshot.memoryTotalBytes = memoryStatus.ullTotalPhys;
        snapshot.memoryAvailableBytes = memoryStatus.ullAvailPhys;
        if (memoryStatus.ullAvailPhys <= memoryStatus.ullTotalPhys)
            snapshot.memoryUsedBytes = memoryStatus.ullTotalPhys - memoryStatus.ullAvailPhys;
        if (memoryStatus.ullTotalPhys > 0 && memoryStatus.ullAvailPhys <= memoryStatus.ullTotalPhys) {
            snapshot.memoryUsagePercent =
                100.0 * static_cast<double>(snapshot.memoryUsedBytes) /
                static_cast<double>(memoryStatus.ullTotalPhys);
        }
    }
    PERFORMANCE_INFORMATION performanceInfo{};
    performanceInfo.cb = sizeof(performanceInfo);
    if (GetPerformanceInfo(&performanceInfo, sizeof(performanceInfo)) && performanceInfo.PageSize > 0 &&
        performanceInfo.SystemCache <= std::numeric_limits<quint64>::max() / performanceInfo.PageSize) {
        snapshot.memorySystemCacheBytes = static_cast<quint64>(performanceInfo.SystemCache) *
            static_cast<quint64>(performanceInfo.PageSize);
    }

    FILETIME idleTime{};
    FILETIME kernelTime{};
    FILETIME userTime{};
    if (GetSystemTimes(&idleTime, &kernelTime, &userTime)) {
        const quint64 idle = fileTimeToUInt64(idleTime);
        const quint64 kernel = fileTimeToUInt64(kernelTime);
        const quint64 user = fileTimeToUInt64(userTime);
        if (impl_->hasCpuBaseline && kernel >= impl_->previousKernel && user >= impl_->previousUser &&
            idle >= impl_->previousIdle) {
            const quint64 totalDelta = (kernel - impl_->previousKernel) + (user - impl_->previousUser);
            const quint64 idleDelta = idle - impl_->previousIdle;
            if (totalDelta > 0) {
                const double busy = 100.0 * static_cast<double>(totalDelta - std::min(idleDelta, totalDelta)) /
                                    static_cast<double>(totalDelta);
                snapshot.processorUsagePercent = std::clamp(busy, 0.0, 100.0);
            }
        }
        impl_->previousIdle = idle;
        impl_->previousKernel = kernel;
        impl_->previousUser = user;
        impl_->hasCpuBaseline = true;
    }

    snapshot.graphicsUsagePercent = impl_->readGpuUsage();
    snapshot.processorCores = impl_->readPerCoreCpuUsage();
    snapshot.diskActivity = impl_->readDiskActivity();

    const QString windowsVolume = driveRootForWindows();
    const DWORD logicalDrives = GetLogicalDrives();
    for (unsigned int index = 0; index < 26; ++index) {
        if ((logicalDrives & (1u << index)) == 0) {
            continue;
        }
        std::array<wchar_t, 4> root{
            static_cast<wchar_t>(L'A' + index), L':', L'\\', L'\0'
        };
        if (GetDriveTypeW(root.data()) != DRIVE_FIXED) {
            continue;
        }

        ULARGE_INTEGER available{};
        ULARGE_INTEGER total{};
        ULARGE_INTEGER totalFree{};
        if (!GetDiskFreeSpaceExW(root.data(), &available, &total, &totalFree) || total.QuadPart == 0) {
            continue;
        }

        VolumeSample volume;
        volume.rootPath = QString::fromWCharArray(root.data());
        volume.label = volume.rootPath;
        volume.totalBytes = total.QuadPart;
        volume.freeBytes = available.QuadPart;
        volume.physicalDiskNumbers = impl_->physicalDisksByVolume.value(volume.rootPath);
        snapshot.volumes.append(volume);
        if (volume.rootPath.compare(windowsVolume, Qt::CaseInsensitive) == 0) {
            snapshot.systemVolumePath = volume.rootPath;
            snapshot.systemVolumeTotalBytes = volume.totalBytes;
            snapshot.systemVolumeFreeBytes = volume.freeBytes;
        }
    }
    if (snapshot.systemVolumeTotalBytes == 0 && !snapshot.volumes.isEmpty()) {
        const auto& fallback = snapshot.volumes.first();
        snapshot.systemVolumePath = fallback.rootPath;
        snapshot.systemVolumeTotalBytes = fallback.totalBytes;
        snapshot.systemVolumeFreeBytes = fallback.freeBytes;
    }

    SYSTEM_POWER_STATUS powerStatus{};
    if (GetSystemPowerStatus(&powerStatus)) {
        if (powerStatus.BatteryLifePercent <= 100 && (powerStatus.BatteryFlag & 0x80) == 0) {
            snapshot.batteryPercent = powerStatus.BatteryLifePercent;
        }
        if (powerStatus.ACLineStatus <= 1) {
            snapshot.batteryOnAcPower = powerStatus.ACLineStatus == 1;
        }
        snapshot.batteryCharging = (powerStatus.BatteryFlag & 0x08) != 0;
    }

    SYSTEM_BATTERY_STATE batteryState{};
    if (CallNtPowerInformation(SystemBatteryState, nullptr, 0, &batteryState,
                               sizeof(batteryState)) == 0 && batteryState.BatteryPresent) {
        if (batteryState.MaxCapacity > 0) {
            snapshot.batteryMaximumCapacityMwh = batteryState.MaxCapacity;
        }
        if (batteryState.RemainingCapacity <= batteryState.MaxCapacity && batteryState.MaxCapacity > 0) {
            snapshot.batteryRemainingCapacityMwh = batteryState.RemainingCapacity;
        }
        // Rate is declared as an unsigned DWORD, but Windows specifies that its
        // high bit carries the sign. Interpret it as LONG before widening.
        const LONG rate = static_cast<LONG>(batteryState.Rate);
        if (rate != LONG_MIN) {
            snapshot.batteryRateMilliwatts = static_cast<qint64>(rate);
        }
        if (batteryState.EstimatedTime != MAXDWORD && batteryState.EstimatedTime > 0) {
            snapshot.batteryEstimatedSeconds = batteryState.EstimatedTime;
        }
    }
    impl_->refreshBatteryCapacities();
    snapshot.batteryDesignCapacityMwh = impl_->batteryDesignCapacityMwh;
    snapshot.batteryFullChargeCapacityMwh = impl_->batteryFullChargeCapacityMwh;

    const QString firmwareIdentity = (snapshot.systemManufacturer + QLatin1Char(' ') + snapshot.systemModel).toLower();
    const bool virtualMachine = firmwareIdentity.contains(QStringLiteral("virtual")) ||
        firmwareIdentity.contains(QStringLiteral("vmware")) || firmwareIdentity.contains(QStringLiteral("virtualbox")) ||
        firmwareIdentity.contains(QStringLiteral("qemu")) || firmwareIdentity.contains(QStringLiteral("kvm")) ||
        firmwareIdentity.contains(QStringLiteral("xen"));
    if (virtualMachine) {
        snapshot.deviceFormFactor = QStringLiteral("Virtual machine (firmware identity hint)");
        snapshot.deviceFormFactorBasis = QStringLiteral("Inferred from the firmware-reported manufacturer/model; virtual-machine detection is heuristic.");
    } else if (batteryState.BatteryPresent || snapshot.batteryPercent) {
        snapshot.deviceFormFactor = QStringLiteral("Portable PC (battery detected)");
        snapshot.deviceFormFactorBasis = QStringLiteral("Windows reports a system battery; docks and unusual power systems can affect this inference.");
    } else {
        snapshot.deviceFormFactor = QStringLiteral("Desktop or workstation (no system battery reported)");
        snapshot.deviceFormFactorBasis = QStringLiteral("Windows exposes no definitive PC form-factor signal here; this classification is inferred from battery absence.");
    }

    MIB_IF_TABLE2* interfaceTable = nullptr;
    if (GetIfTable2(&interfaceTable) == NO_ERROR && interfaceTable) {
        quint64 receiveBytes = 0;
        quint64 sendBytes = 0;
        ULONG activeInterfaces = 0;
        snapshot.activeNetworkAdapters.clear();
        for (ULONG i = 0; i < interfaceTable->NumEntries; ++i) {
            const MIB_IF_ROW2& row = interfaceTable->Table[i];
            if (row.OperStatus != IfOperStatusUp || row.Type == IF_TYPE_SOFTWARE_LOOPBACK ||
                row.Type == IF_TYPE_TUNNEL || !row.InterfaceAndOperStatusFlags.HardwareInterface) {
                continue;
            }
            receiveBytes += row.InOctets;
            sendBytes += row.OutOctets;
            ++activeInterfaces;
            NetworkAdapterSample adapter;
            adapter.name = QString::fromWCharArray(row.Alias).trimmed();
            adapter.description = QString::fromWCharArray(row.Description).trimmed();
            adapter.interfaceIndex = row.InterfaceIndex;
            adapter.receiveLinkSpeedBitsPerSecond = row.ReceiveLinkSpeed;
            adapter.transmitLinkSpeedBitsPerSecond = row.TransmitLinkSpeed;
            if (adapter.name.isEmpty()) adapter.name = adapter.description;
            snapshot.activeNetworkAdapters.append(std::move(adapter));
        }
        FreeMibTable(interfaceTable);

        const auto now = std::chrono::steady_clock::now();
        if (impl_->hasNetworkBaseline && impl_->activeNetworkInterfaceCount > 0 && activeInterfaces > 0) {
            const double seconds = std::chrono::duration<double>(now - impl_->previousNetworkAt).count();
            if (seconds > 0.0 && receiveBytes >= impl_->previousReceiveBytes && sendBytes >= impl_->previousSendBytes) {
                snapshot.networkReceiveBytesPerSecond =
                    static_cast<double>(receiveBytes - impl_->previousReceiveBytes) / seconds;
                snapshot.networkSendBytesPerSecond =
                    static_cast<double>(sendBytes - impl_->previousSendBytes) / seconds;
            }
        }
        impl_->previousReceiveBytes = receiveBytes;
        impl_->previousSendBytes = sendBytes;
        impl_->previousNetworkAt = now;
        impl_->activeNetworkInterfaceCount = activeInterfaces;
        impl_->hasNetworkBaseline = true;
        snapshot.networkNote = activeInterfaces == 0
            ? QStringLiteral("No active network interface")
            : QStringLiteral("Combined active hardware adapters");
    } else {
        snapshot.networkNote = QStringLiteral("Windows did not provide interface counters");
    }
    impl_->refreshNetworkConnectionCounts();
    snapshot.networkTcpEntryCount = impl_->tcpEntryCount;
    snapshot.networkUdpEndpointCount = impl_->udpEndpointCount;

    const auto now = std::chrono::steady_clock::now();
    constexpr auto kProcessSampleInterval = std::chrono::seconds(5);
    if (!impl_->hasProcessSnapshot || now - impl_->previousProcessCollectionAt >= kProcessSampleInterval) {
        const auto& previousProcessBaselines = impl_->processBaselines;
        auto& nextProcessBaselines = impl_->processBaselinesScratch;
        nextProcessBaselines.clear();
        QVector<ProcessSample> processes;
        bool processEnumerationSucceeded = false;
        DWORD processEnumerationError = ERROR_SUCCESS;

        HANDLE processSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (processSnapshot != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W entry{};
            entry.dwSize = sizeof(entry);
            if (Process32FirstW(processSnapshot, &entry)) {
                while (true) {
                    ProcessSample process;
                    process.processId = entry.th32ProcessID;
                    process.name = QString::fromWCharArray(entry.szExeFile);
                    if (impl_->tcpConnectionsByProcess)
                        process.tcpConnectionCount = impl_->tcpConnectionsByProcess->value(process.processId, 0);
                    if (impl_->udpEndpointsByProcess)
                        process.udpEndpointCount = impl_->udpEndpointsByProcess->value(process.processId, 0);

                    HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE,
                                                entry.th32ProcessID);
                    if (handle) {
                        const auto previous = previousProcessBaselines.find(entry.th32ProcessID);
                        PreviousProcessSample current;
                        current.capturedAt = now;

                        std::wstring imagePath(32768, L'\0');
                        DWORD imagePathLength = static_cast<DWORD>(imagePath.size());
                        if (QueryFullProcessImageNameW(handle, 0, imagePath.data(), &imagePathLength)) {
                            imagePath.resize(imagePathLength);
                            process.executablePath = QString::fromWCharArray(
                                imagePath.data(), static_cast<qsizetype>(imagePath.size()));
                        }

                        PROCESS_MEMORY_COUNTERS_EX memoryCounters{};
                        memoryCounters.cb = sizeof(memoryCounters);
                        if (GetProcessMemoryInfo(handle,
                                reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memoryCounters),
                                sizeof(memoryCounters))) {
                            process.workingSetBytes = memoryCounters.WorkingSetSize;
                        }

                        FILETIME created{};
                        FILETIME exited{};
                        FILETIME kernel{};
                        FILETIME user{};
                        if (GetProcessTimes(handle, &created, &exited, &kernel, &user)) {
                            const quint64 creationTime = fileTimeToUInt64(created);
                            current.creationTime100ns = creationTime;
                            const bool sameProcessInstance = previous != previousProcessBaselines.end() &&
                                previous->second.creationTime100ns &&
                                *previous->second.creationTime100ns == creationTime;
                            const quint64 cpuTime = fileTimeToUInt64(kernel) + fileTimeToUInt64(user);
                            if (sameProcessInstance && previous->second.cpuTime100ns &&
                                cpuTime >= *previous->second.cpuTime100ns) {
                                const double elapsedSeconds = std::chrono::duration<double>(
                                    now - previous->second.capturedAt).count();
                                if (elapsedSeconds > 0.0 && snapshot.logicalProcessorCount > 0) {
                                    const double elapsed100ns = elapsedSeconds * 10'000'000.0;
                                    const double cpuDelta = static_cast<double>(cpuTime - *previous->second.cpuTime100ns);
                                    process.cpuPercent = std::clamp(
                                        100.0 * cpuDelta /
                                            (elapsed100ns * static_cast<double>(snapshot.logicalProcessorCount)),
                                        0.0, 100.0);
                                }
                            }
                            current.cpuTime100ns = cpuTime;
                        }

                        IO_COUNTERS ioCounters{};
                        if (GetProcessIoCounters(handle, &ioCounters)) {
                            const quint64 readBytes = ioCounters.ReadTransferCount;
                            const quint64 writeBytes = ioCounters.WriteTransferCount;
                            if (current.creationTime100ns && previous != previousProcessBaselines.end() &&
                                previous->second.creationTime100ns == current.creationTime100ns) {
                                const double elapsedSeconds = std::chrono::duration<double>(
                                    now - previous->second.capturedAt).count();
                                if (elapsedSeconds > 0.0 && previous->second.readIoTransferBytes &&
                                    readBytes >= *previous->second.readIoTransferBytes) {
                                    process.readIoBytesPerSecond = static_cast<double>(
                                        readBytes - *previous->second.readIoTransferBytes) / elapsedSeconds;
                                }
                                if (elapsedSeconds > 0.0 && previous->second.writeIoTransferBytes &&
                                    writeBytes >= *previous->second.writeIoTransferBytes) {
                                    process.writeIoBytesPerSecond = static_cast<double>(
                                        writeBytes - *previous->second.writeIoTransferBytes) / elapsedSeconds;
                                }
                            }
                            current.readIoTransferBytes = readBytes;
                            current.writeIoTransferBytes = writeBytes;
                        }
                        if (current.cpuTime100ns || current.readIoTransferBytes || current.writeIoTransferBytes)
                            nextProcessBaselines[entry.th32ProcessID] = current;
                        CloseHandle(handle);
                    }

                    processes.append(std::move(process));
                    SetLastError(ERROR_SUCCESS);
                    if (!Process32NextW(processSnapshot, &entry)) {
                        processEnumerationError = GetLastError();
                        processEnumerationSucceeded = processEnumerationError == ERROR_NO_MORE_FILES;
                        break;
                    }
                }
            } else {
                processEnumerationError = GetLastError();
            }
            CloseHandle(processSnapshot);
        } else {
            processEnumerationError = GetLastError();
        }

        if (processEnumerationSucceeded) {
            std::sort(processes.begin(), processes.end(), [](const ProcessSample& left, const ProcessSample& right) {
                return left.workingSetBytes.value_or(0) > right.workingSetBytes.value_or(0);
            });
            impl_->previousTopProcesses = std::move(processes);
            impl_->processSamplesCapturedAt = QDateTime::currentDateTime();
            impl_->hasProcessSnapshot = true;
            impl_->processBaselines.swap(nextProcessBaselines);
            impl_->processCollectionStatus = QStringLiteral("Process collection succeeded.");
        } else {
            impl_->processCollectionStatus = impl_->hasProcessSnapshot
                ? QStringLiteral("Windows could not complete process collection (error %1); showing the last successful sample.")
                    .arg(processEnumerationError)
                : QStringLiteral("Windows could not collect a process sample (error %1).")
                    .arg(processEnumerationError);
        }
        impl_->previousProcessCollectionAt = now;
    }
    snapshot.topProcesses = impl_->previousTopProcesses;
    snapshot.processSamplesCapturedAt = impl_->processSamplesCapturedAt;
    snapshot.processCollectionStatus = impl_->processCollectionStatus;
    DWORD foregroundProcessId = 0;
    if (const HWND foregroundWindow = GetForegroundWindow()) {
        GetWindowThreadProcessId(foregroundWindow, &foregroundProcessId);
    }
    snapshot.foregroundProcessId = static_cast<quint32>(foregroundProcessId);
    if (foregroundProcessId != 0) {
        const auto foregroundProcess = std::find_if(snapshot.topProcesses.cbegin(), snapshot.topProcesses.cend(),
            [foregroundProcessId](const ProcessSample& process) {
                return process.processId == foregroundProcessId;
            });
        if (foregroundProcess != snapshot.topProcesses.cend())
            snapshot.foregroundProcessName = foregroundProcess->name;
    }

    return snapshot;
}

} // namespace Ausyn
