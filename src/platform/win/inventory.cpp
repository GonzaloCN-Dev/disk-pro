// Disk Pro - MIT License - Copyright (c) Gonzalo Cruz Núñez
#include "inventory.h"

#include <windows.h>
#include <winioctl.h>
#include <wbemidl.h>

#include <algorithm>
#include <cwctype>
#include <memory>
#include <string>
#include <unordered_map>

namespace diskpro::win {
namespace {

struct Handle {
    HANDLE h = INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE x) : h(x) {}
    ~Handle() { if (h != INVALID_HANDLE_VALUE) CloseHandle(h); }
    bool ok() const { return h != INVALID_HANDLE_VALUE; }
};

template <class T> struct ComPtr {
    T* p = nullptr;
    ~ComPtr() { if (p) p->Release(); }
    T** put() { if (p) { p->Release(); p = nullptr; } return &p; }
    T* operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

HRESULT invokeVolumeMethod(IWbemServices* services, BSTR path, const wchar_t* method,
                           const wchar_t* outputName, uint32_t& value) {
    BSTR methodName = SysAllocString(method);
    if (!methodName) return E_OUTOFMEMORY;
    ComPtr<IWbemClassObject> result;
    const HRESULT hr = services->ExecMethod(path, methodName, 0, nullptr, nullptr, result.put(), nullptr);
    SysFreeString(methodName);
    if (FAILED(hr) || !result) return hr;
    VARIANT returnValue;
    VariantInit(&returnValue);
    const HRESULT returnHr = result->Get(L"ReturnValue", 0, &returnValue, nullptr, nullptr);
    if (FAILED(returnHr) || (returnValue.vt != VT_UI4 && returnValue.vt != VT_I4) || returnValue.ulVal != 0) {
        VariantClear(&returnValue);
        return FAILED(returnHr) ? returnHr : WBEM_E_FAILED;
    }
    VariantClear(&returnValue);
    VARIANT v;
    VariantInit(&v);
    const HRESULT propertyHr = result->Get(outputName, 0, &v, nullptr, nullptr);
    if (SUCCEEDED(propertyHr) && (v.vt == VT_UI4 || v.vt == VT_I4)) value = v.ulVal;
    else if (SUCCEEDED(propertyHr)) {
        VariantClear(&v);
        return WBEM_E_TYPE_MISMATCH;
    }
    VariantClear(&v);
    return propertyHr;
}

std::unordered_map<char, BitLockerStatus> queryBitLockerByDriveLetter() {
    std::unordered_map<char, BitLockerStatus> result;
    const HRESULT comHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool uninitialize = SUCCEEDED(comHr);
    if (FAILED(comHr) && comHr != RPC_E_CHANGED_MODE) return result;
    const HRESULT securityHr = CoInitializeSecurity(nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_DEFAULT,
                                                     RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE, nullptr);
    if (FAILED(securityHr) && securityHr != RPC_E_TOO_LATE) {
        if (uninitialize) CoUninitialize();
        return result;
    }

    ComPtr<IWbemLocator> locator;
    HRESULT hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_IWbemLocator, reinterpret_cast<void**>(locator.put()));
    if (FAILED(hr) || !locator) { if (uninitialize) CoUninitialize(); return result; }
    BSTR nameSpace = SysAllocString(L"ROOT\\CIMV2\\Security\\MicrosoftVolumeEncryption");
    if (!nameSpace) { if (uninitialize) CoUninitialize(); return result; }
    ComPtr<IWbemServices> services;
    hr = locator->ConnectServer(nameSpace, nullptr, nullptr, nullptr, 0, nullptr, nullptr, services.put());
    SysFreeString(nameSpace);
    if (FAILED(hr) || !services ||
        FAILED(CoSetProxyBlanket(services.p, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                                 RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE))) {
        if (uninitialize) CoUninitialize();
        return result;
    }

    BSTR language = SysAllocString(L"WQL");
    BSTR query = SysAllocString(L"SELECT DeviceID, DriveLetter FROM Win32_EncryptableVolume");
    if (!language || !query) {
        if (language) SysFreeString(language);
        if (query) SysFreeString(query);
        if (uninitialize) CoUninitialize();
        return result;
    }
    ComPtr<IEnumWbemClassObject> enumerator;
    hr = services->ExecQuery(language, query, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                             nullptr, enumerator.put());
    SysFreeString(language);
    SysFreeString(query);
    if (FAILED(hr) || !enumerator) { if (uninitialize) CoUninitialize(); return result; }

    for (;;) {
        ComPtr<IWbemClassObject> volume;
        ULONG count = 0;
        hr = enumerator->Next(WBEM_INFINITE, 1, volume.put(), &count);
        if (FAILED(hr) || count == 0 || !volume) break;
        VARIANT drive, path;
        VariantInit(&drive); VariantInit(&path);
        const HRESULT driveHr = volume->Get(L"DriveLetter", 0, &drive, nullptr, nullptr);
        const HRESULT pathHr = volume->Get(L"__PATH", 0, &path, nullptr, nullptr);
        if (SUCCEEDED(driveHr) && SUCCEEDED(pathHr) && drive.vt == VT_BSTR && drive.bstrVal &&
            SysStringLen(drive.bstrVal) >= 1 && path.vt == VT_BSTR && path.bstrVal) {
            uint32_t conversion = 0, protection = 2;
            const HRESULT conversionHr = invokeVolumeMethod(services.p, path.bstrVal, L"GetConversionStatus",
                                                              L"ConversionStatus", conversion);
            const HRESULT protectionHr = invokeVolumeMethod(services.p, path.bstrVal, L"GetProtectionStatus",
                                                              L"ProtectionStatus", protection);
            const auto status = classifyBitLockerStatus(conversion, protection, SUCCEEDED(conversionHr),
                                                        SUCCEEDED(protectionHr));
            result[char(std::towupper(drive.bstrVal[0]))] = status;
        }
        VariantClear(&drive);
        VariantClear(&path);
    }
    if (uninitialize) CoUninitialize();
    return result;
}

std::string trim(std::string s) {
    auto sp = [](unsigned char c) { return std::isspace(c); };
    while (!s.empty() && sp(s.back())) s.pop_back();
    while (!s.empty() && sp(s.front())) s.erase(s.begin());
    return s;
}

const char* busName(STORAGE_BUS_TYPE t) {
    switch (t) {
        case BusTypeScsi: return "SCSI"; case BusTypeAtapi: return "ATAPI"; case BusTypeAta: return "ATA";
        case BusType1394: return "1394"; case BusTypeUsb: return "USB"; case BusTypeRAID: return "RAID";
        case BusTypeiScsi: return "iSCSI"; case BusTypeSas: return "SAS"; case BusTypeSata: return "SATA";
        case BusTypeSd: return "SD"; case BusTypeMmc: return "MMC"; case BusTypeVirtual: return "Virtual";
        case BusTypeFileBackedVirtual: return "FileBackedVirtual"; case BusTypeNvme: return "NVMe";
        default: return "Unknown";
    }
}

std::string mask(const std::string& s) {  // never expose full serials in logs/output
    if (s.size() <= 4) return s.empty() ? "" : "****";
    return "****" + s.substr(s.size() - 4);
}

const char* mbrType(BYTE t) {
    switch (t) {
        case 0x07: return "NTFS/exFAT"; case 0x0B: case 0x0C: return "FAT32"; case 0x06: case 0x0E: return "FAT16";
        case 0x05: case 0x0F: return "Extended"; case 0xEE: return "GPT protective"; case 0xEF: return "EFI System";
        case 0x27: return "Windows RE"; default: return "";
    }
}

const char* gptType(const GUID& g) {
    static const GUID efi = {0xC12A7328, 0xF81F, 0x11D2, {0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B}};
    static const GUID msr = {0xE3C9E316, 0x0B5C, 0x4DB8, {0x81, 0x7D, 0xF9, 0x2D, 0xF0, 0x02, 0x15, 0xAE}};
    static const GUID basic = {0xEBD0A0A2, 0xB9E5, 0x4433, {0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7}};
    static const GUID re = {0xDE94BBA4, 0x06D1, 0x4D40, {0xA1, 0x6A, 0xBF, 0xD5, 0x01, 0x79, 0xD6, 0xAC}};
    if (g == efi) return "EFI System"; if (g == msr) return "Microsoft Reserved";
    if (g == basic) return "Basic data"; if (g == re) return "Windows RE";
    return "";
}

void queryProperty(HANDLE h, DiskInfo& d) {
    STORAGE_PROPERTY_QUERY q{};
    q.PropertyId = StorageDeviceProperty; q.QueryType = PropertyStandardQuery;
    alignas(8) BYTE buf[2048]{};
    DWORD n = 0;
    if (DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof q, buf, sizeof buf, &n, nullptr)) {
        auto* desc = reinterpret_cast<STORAGE_DEVICE_DESCRIPTOR*>(buf);
        auto str = [&](DWORD off) { return off && off < n ? trim(reinterpret_cast<char*>(buf) + off) : std::string(); };
        d.vendor = str(desc->VendorIdOffset); d.product = str(desc->ProductIdOffset);
        d.serialMasked = mask(str(desc->SerialNumberOffset));
        d.busType = busName(desc->BusType);
        d.isVirtual = desc->BusType == BusTypeVirtual || desc->BusType == BusTypeFileBackedVirtual;
    } else d.error += "device-property;";

    q.PropertyId = StorageAccessAlignmentProperty;
    STORAGE_ACCESS_ALIGNMENT_DESCRIPTOR al{};
    if (DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof q, &al, sizeof al, &n, nullptr)) {
        d.logicalSector = al.BytesPerLogicalSector; d.physicalSector = al.BytesPerPhysicalSector;
    }
}

void queryLayout(HANDLE h, DiskInfo& d) {
    std::vector<BYTE> buf(64 * 1024);
    DWORD n = 0;
    if (!DeviceIoControl(h, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, nullptr, 0, buf.data(), DWORD(buf.size()), &n, nullptr)) {
        d.error += "layout(" + std::to_string(GetLastError()) + ");";
        return;
    }
    auto* l = reinterpret_cast<DRIVE_LAYOUT_INFORMATION_EX*>(buf.data());
    d.style = l->PartitionStyle == PARTITION_STYLE_GPT ? "GPT" : l->PartitionStyle == PARTITION_STYLE_MBR ? "MBR" : "RAW";
    for (DWORD i = 0; i < l->PartitionCount && i < 128; ++i) {
        auto& p = l->PartitionEntry[i];
        if (p.PartitionLength.QuadPart == 0) continue;
        PartitionInfo pi;
        pi.number = p.PartitionNumber; pi.style = d.style;
        pi.offsetBytes = uint64_t(p.StartingOffset.QuadPart); pi.lengthBytes = uint64_t(p.PartitionLength.QuadPart);
        pi.typeName = p.PartitionStyle == PARTITION_STYLE_GPT ? gptType(p.Gpt.PartitionType) : mbrType(p.Mbr.PartitionType);
        d.partitions.push_back(pi);
    }
}

}  // namespace

BitLockerStatus classifyBitLockerStatus(uint32_t conversionStatus, uint32_t protectionStatus,
                                       bool conversionQuerySucceeded, bool protectionQuerySucceeded) {
    // Win32_EncryptableVolume.GetConversionStatus values: fully decrypted/encrypted,
    // encryption/decryption in progress, and their paused states.
    if (conversionQuerySucceeded) {
        switch (conversionStatus) {
            case 0: return protectionQuerySucceeded && protectionStatus == 1
                                   ? BitLockerStatus::Encrypted : BitLockerStatus::NotEncrypted;
            case 1: return BitLockerStatus::Encrypted;
            case 2: return BitLockerStatus::Encrypting;
            case 3: return BitLockerStatus::Decrypting;
            case 4: case 5: return BitLockerStatus::Paused;
            default: return BitLockerStatus::Unknown;
        }
    }
    if (protectionQuerySucceeded) {
        if (protectionStatus == 1) return BitLockerStatus::Encrypted;
        if (protectionStatus == 0) return BitLockerStatus::NotEncrypted;
    }
    return BitLockerStatus::Unknown;
}

std::vector<DiskInfo> enumerateDisks() {
    std::vector<DiskInfo> out;
    char sysDir[MAX_PATH]{};
    const UINT sysDirLength = GetWindowsDirectoryA(sysDir, MAX_PATH);
    const std::string sysVol = sysDirLength > 0 && sysDirLength < MAX_PATH ? std::string(1, sysDir[0]) : std::string();
    for (uint32_t i = 0; i < 64; ++i) {
        DiskInfo d;
        d.number = i;
        d.path = "\\\\.\\PhysicalDrive" + std::to_string(i);
        Handle h(CreateFileA(d.path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
        if (!h.ok()) {
            if (GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND) { if (i > 8) break; continue; }
            d.error = "open(" + std::to_string(GetLastError()) + ")";
            out.push_back(d); continue;
        }
        queryProperty(h.h, d);
        alignas(8) BYTE gb[512]{};
        DWORD n = 0;
        if (DeviceIoControl(h.h, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, nullptr, 0, gb, sizeof gb, &n, nullptr))
            d.sizeBytes = uint64_t(reinterpret_cast<DISK_GEOMETRY_EX*>(gb)->DiskSize.QuadPart);
        else d.error += "size(" + std::to_string(GetLastError()) + ");";
        queryLayout(h.h, d);
        out.push_back(std::move(d));
    }
    // Map drive letters to disk numbers (volume handles opened with access 0).
    const auto bitLockerByDrive = queryBitLockerByDriveLetter();
    DWORD mask = GetLogicalDrives();
    for (int c = 0; c < 26; ++c) {
        if (!(mask & (1u << c))) continue;
        char root[] = {char('A' + c), ':', '\\', 0};
        if (GetDriveTypeA(root) != DRIVE_FIXED && GetDriveTypeA(root) != DRIVE_REMOVABLE) continue;
        std::string vp = std::string("\\\\.\\") + char('A' + c) + ":";
        Handle v(CreateFileA(vp.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
        if (!v.ok()) continue;
        alignas(8) BYTE b[1024]{};
        DWORD n = 0;
        if (!DeviceIoControl(v.h, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, nullptr, 0, b, sizeof b, &n, nullptr)) continue;
        auto* ex = reinterpret_cast<VOLUME_DISK_EXTENTS*>(b);
        char fsName[32]{}, volumeLabel[MAX_PATH]{};
        const bool hasVolumeInfo = GetVolumeInformationA(root, volumeLabel, DWORD(std::size(volumeLabel)), nullptr, nullptr, nullptr,
                                                         fsName, DWORD(std::size(fsName))) != FALSE;
        ULARGE_INTEGER freeAvailable{}, totalBytes{}, totalFreeBytes{};
        const bool hasCapacity = GetDiskFreeSpaceExA(root, &freeAvailable, &totalBytes, &totalFreeBytes) != FALSE;
        for (DWORD e = 0; e < ex->NumberOfDiskExtents; ++e)
            for (auto& d : out)
                if (d.number == ex->Extents[e].DiskNumber) {
                    for (auto& p : d.partitions)
                        if (p.offsetBytes == uint64_t(ex->Extents[e].StartingOffset.QuadPart)) {
                            p.volume = root;
                            if (hasVolumeInfo) { p.filesystem = fsName; p.volumeLabel = volumeLabel; }
                            const auto bitLocker = bitLockerByDrive.find(char(std::toupper(static_cast<unsigned char>(root[0]))));
                            if (bitLocker != bitLockerByDrive.end()) p.bitLocker = bitLocker->second;
                            if (hasCapacity) {
                                p.volumeTotalBytes = totalBytes.QuadPart;
                                p.volumeFreeBytes = totalFreeBytes.QuadPart;
                            }
                        }
                    if (std::find(d.volumes.begin(), d.volumes.end(), root) == d.volumes.end()) d.volumes.push_back(root);
                    if (!sysVol.empty() && root[0] == sysVol[0]) d.isSystemDisk = true;
                }
    }
    return out;
}

}  // namespace diskpro::win

