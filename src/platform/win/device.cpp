// Disk Pro - MIT License - Copyright (c) Gonzalo Cruz Núñez
#include "device.h"
#include "inventory.h"
#include <windows.h>
#include <winioctl.h>
#include <string>
#include <vector>
#include <filesystem>

namespace diskpro {

bool pathOnPhysicalDisk(const std::string& path, int diskNumber, std::string& error) {
    if (diskNumber < 0) return true;
    const auto wide = std::filesystem::path(path).wstring();
    wchar_t volumePath[MAX_PATH]{};
    if (!GetVolumePathNameW(wide.c_str(), volumePath, MAX_PATH)) {
        error = "cannot determine the output volume";
        return false;
    }
    wchar_t volumeName[MAX_PATH]{};
    if (!GetVolumeNameForVolumeMountPointW(volumePath, volumeName, MAX_PATH)) {
        error = "cannot identify the output volume";
        return false;
    }
    std::wstring devicePath = volumeName;
    if (!devicePath.empty() && devicePath.back() == L'\\') devicePath.pop_back();
    HANDLE h = CreateFileW(devicePath.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        error = "cannot inspect the output volume";
        return false;
    }
    std::vector<BYTE> buffer(sizeof(VOLUME_DISK_EXTENTS) + 64 * sizeof(DISK_EXTENT));
    DWORD returned = 0;
    const BOOL ok = DeviceIoControl(h, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, nullptr, 0,
                                    buffer.data(), DWORD(buffer.size()), &returned, nullptr);
    CloseHandle(h);
    if (!ok || returned < sizeof(VOLUME_DISK_EXTENTS)) {
        error = "cannot determine which physical disk contains the output";
        return false;
    }
    const auto* extents = reinterpret_cast<const VOLUME_DISK_EXTENTS*>(buffer.data());
    for (DWORD i = 0; i < extents->NumberOfDiskExtents; ++i) {
        if (extents->Extents[i].DiskNumber == DWORD(diskNumber)) {
            error = "output is on the same physical disk as the recovery source";
            return false;
        }
    }
    return true;
}

DeviceSource::DeviceSource(int n) {
    disk_ = n;
    if (n < 0 || n > 4095) return;
    std::wstring path = L"\\\\.\\PhysicalDrive" + std::to_wstring(n);
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DISK_GEOMETRY_EX g{};
    DWORD got = 0;
    if (!DeviceIoControl(h, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, nullptr, 0, &g, sizeof g, &got, nullptr) ||
        g.DiskSize.QuadPart <= 0 || g.Geometry.BytesPerSector == 0) {
        CloseHandle(h);
        return;
    }
    size_ = uint64_t(g.DiskSize.QuadPart);
    sector_ = g.Geometry.BytesPerSector;
    h_ = h;
}

DeviceSource::~DeviceSource() { if (h_) CloseHandle(h_); }

bool DeviceSource::read(uint64_t off, void* buf, size_t len) const {
    if (!h_ || len == 0) return h_ != nullptr;
    if (off > size_ || len > size_ - off) return false;
    uint64_t start = off / sector_ * sector_;
    uint64_t end = (off + len + sector_ - 1) / sector_ * sector_;
    if (end > size_) end = size_;
    std::vector<uint8_t> tmp(size_t(end - start));
    size_t done = 0;
    while (done < tmp.size()) {
        LARGE_INTEGER p; p.QuadPart = LONGLONG(start + done);
        if (!SetFilePointerEx(h_, p, nullptr, FILE_BEGIN)) return false;
        DWORD chunk = DWORD(std::min<size_t>(tmp.size() - done, 1u << 20)), r = 0;
        if (!ReadFile(h_, tmp.data() + done, chunk, &r, nullptr) || r == 0) return false;
        done += r;
    }
    std::memcpy(buf, tmp.data() + (off - start), len);
    return true;
}

DeviceSink::DeviceSink(int n, const TargetFacts& expected) {
    if (n < 0 || n > 4095) return;
    if (expected.number != uint32_t(n) || expected.isSystemDisk || !expected.isVirtual ||
        expected.size == 0 || expected.size > kMaxWritableDisk) return;
    const win::DiskInfo* current = nullptr;
    auto disks = win::enumerateDisks();
    for (const auto& d : disks) if (d.number == uint32_t(n)) current = &d;
    if (!current || current->isSystemDisk || !current->isVirtual || current->sizeBytes != expected.size) return;
    const uint32_t expectedSector = expected.sectorSize ? expected.sectorSize : 512;
    if (current->logicalSector && current->logicalSector != expectedSector) return;
    std::wstring path = L"\\\\.\\PhysicalDrive" + std::to_wstring(n);
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                           FILE_FLAG_WRITE_THROUGH, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    STORAGE_PROPERTY_QUERY q{};
    q.PropertyId = StorageDeviceProperty;
    q.QueryType = PropertyStandardQuery;
    alignas(8) BYTE property[1024]{};
    DWORD propertyBytes = 0;
    if (!DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof q, property, sizeof property, &propertyBytes, nullptr)) {
        CloseHandle(h); return;
    }
    const auto bus = reinterpret_cast<STORAGE_DEVICE_DESCRIPTOR*>(property)->BusType;
    if (bus != BusTypeVirtual && bus != BusTypeFileBackedVirtual) { CloseHandle(h); return; }
    DISK_GEOMETRY_EX g{};
    DWORD got = 0;
    if (!DeviceIoControl(h, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, nullptr, 0, &g, sizeof g, &got, nullptr) || g.DiskSize.QuadPart <= 0 ||
        g.Geometry.BytesPerSector == 0 || uint64_t(g.DiskSize.QuadPart) != expected.size ||
        g.Geometry.BytesPerSector != expectedSector) {
        CloseHandle(h);
        return;
    }
    size_ = uint64_t(g.DiskSize.QuadPart);
    sector_ = g.Geometry.BytesPerSector;
    h_ = h;
    disk_ = n;
}
DeviceSink::~DeviceSink() {
    for (void* v : locked_) CloseHandle(v);
    if (h_) CloseHandle(h_);
}

// Locks and dismounts every volume that lives (even partly) in [offset, offset+length) of this disk, so that Windows
// does not block the raw write or overwrite the new file system with its cached copy. Handles stay open until destruction.
bool DeviceSink::prepare(uint64_t offset, uint64_t length) {
    if (!h_ || disk_ < 0 || offset > size_ || length > size_ - offset) return false;
    const uint64_t rangeEnd = offset + length;
    wchar_t name[MAX_PATH];
    HANDLE find = FindFirstVolumeW(name, MAX_PATH);
    if (find == INVALID_HANDLE_VALUE) return false;
    bool ok = true;
    do {
        std::wstring path = name;
        if (!path.empty() && path.back() == L'\\') path.pop_back();
        HANDLE v = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (v == INVALID_HANDLE_VALUE) {
            v = CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);   // query only
            if (v == INVALID_HANDLE_VALUE) { ok = false; continue; }
        }
        std::vector<uint8_t> buf(4096);
        DWORD got = 0;
        bool mine = false;
        if (DeviceIoControl(v, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, nullptr, 0, buf.data(), DWORD(buf.size()), &got, nullptr)) {
            auto* e = reinterpret_cast<VOLUME_DISK_EXTENTS*>(buf.data());
            for (DWORD i = 0; i < e->NumberOfDiskExtents; ++i) {
                const auto& x = e->Extents[i];
                if (int(x.DiskNumber) != disk_) continue;
                uint64_t s = uint64_t(x.StartingOffset.QuadPart), l = uint64_t(x.ExtentLength.QuadPart);
                const uint64_t extentEnd = l > UINT64_MAX - s ? UINT64_MAX : s + l;
                if (s < rangeEnd && offset < extentEnd) mine = true;
            }
        } else { ok = false; CloseHandle(v); continue; }
        if (!mine) { CloseHandle(v); continue; }
        if (!DeviceIoControl(v, FSCTL_LOCK_VOLUME, nullptr, 0, nullptr, 0, &got, nullptr) ||
            !DeviceIoControl(v, FSCTL_DISMOUNT_VOLUME, nullptr, 0, nullptr, 0, &got, nullptr)) {
            ok = false;
            CloseHandle(v);
            continue;
        }
        locked_.push_back(v);
    } while (FindNextVolumeW(find, name, MAX_PATH));
    if (GetLastError() != ERROR_NO_MORE_FILES) ok = false;
    FindVolumeClose(find);
    return ok;
}

bool DeviceSink::write(uint64_t off, const void* buf, size_t len) {
    if (!h_ || len == 0) return false;
    if (off % sector_ || len % sector_ || off > size_ || len > size_ - off) return false;
    auto* p = static_cast<const uint8_t*>(buf);
    size_t done = 0;
    while (done < len) {
        LARGE_INTEGER pos; pos.QuadPart = LONGLONG(off + done);
        if (!SetFilePointerEx(h_, pos, nullptr, FILE_BEGIN)) return false;
        DWORD chunk = DWORD(std::min<size_t>(len - done, 1u << 20)), w = 0;
        if (!WriteFile(h_, p + done, chunk, &w, nullptr) || w != chunk) return false;
        done += w;
    }
    return true;
}
bool DeviceSink::flush() { return h_ && FlushFileBuffers(h_) != 0; }
void DeviceSink::refreshLayout() { DWORD got = 0; if (h_) DeviceIoControl(h_, IOCTL_DISK_UPDATE_PROPERTIES, nullptr, 0, nullptr, 0, &got, nullptr); }

}  // namespace diskpro
