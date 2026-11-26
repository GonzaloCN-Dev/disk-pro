// Disk Pro - MIT License - Copyright (c) Gonzalo Cruz Núñez
#include "smart.h"

#include <windows.h>
#include <winioctl.h>
#include <nvme.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace diskpro::win {

bool parseAtaSmart(const uint8_t* data, size_t size, HealthInfo& h) {
    if (!data || size < 362) return false;
    bool any = false;
    for (size_t off = 2; off + 12 <= std::min<size_t>(size, 362); off += 12) {
        const uint8_t id = data[off];
        if (!id || id == 0xFF) continue;
        uint64_t raw = 0;
        for (int i = 0; i < 6; ++i) raw |= uint64_t(data[off + 5 + i]) << (8 * i);
        switch (id) {
            case 5: h.reallocatedSectors = raw; any = true; break;
            case 9: if (!h.nvmeAvailable) h.powerOnHours = raw; any = true; break;
            case 194:
                if (!h.nvmeAvailable) h.temperatureC = int(data[off + 5]);
                any = true;
                break;
            case 197: h.pendingSectors = raw; any = true; break;
            case 198: h.uncorrectableSectors = raw; any = true; break;
            default: break;
        }
    }
    h.ataAvailable = any;
    return any;
}

HealthInfo queryHealth(uint32_t n) {
    HealthInfo h;
    std::string path = "\\\\.\\PhysicalDrive" + std::to_string(n);
    HANDLE dev = CreateFileA(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (dev == INVALID_HANDLE_VALUE) { h.note = "cannot open device (error " + std::to_string(GetLastError()) + ")"; return h; }

    // NVMe SMART / Health Information log (page 0x02), read via standard storage query.
    {
        const DWORD size = FIELD_OFFSET(STORAGE_PROPERTY_QUERY, AdditionalParameters) +
                           sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA) + sizeof(NVME_HEALTH_INFO_LOG);
        std::vector<BYTE> buf(size, 0);
        auto* q = reinterpret_cast<STORAGE_PROPERTY_QUERY*>(buf.data());
        q->PropertyId = StorageDeviceProtocolSpecificProperty;
        q->QueryType = PropertyStandardQuery;
        auto* ps = reinterpret_cast<STORAGE_PROTOCOL_SPECIFIC_DATA*>(q->AdditionalParameters);
        ps->ProtocolType = ProtocolTypeNvme;
        ps->DataType = NVMeDataTypeLogPage;
        ps->ProtocolDataRequestValue = NVME_LOG_PAGE_HEALTH_INFO;
        ps->ProtocolDataRequestSubValue = 0;
        ps->ProtocolDataOffset = sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA);
        ps->ProtocolDataLength = sizeof(NVME_HEALTH_INFO_LOG);
        DWORD ret = 0;
        if (DeviceIoControl(dev, IOCTL_STORAGE_QUERY_PROPERTY, buf.data(), size, buf.data(), size, &ret, nullptr)) {
            auto* desc = reinterpret_cast<STORAGE_PROTOCOL_DATA_DESCRIPTOR*>(buf.data());
            auto& spd = desc->ProtocolSpecificData;
            auto* log = reinterpret_cast<NVME_HEALTH_INFO_LOG*>(reinterpret_cast<BYTE*>(&spd) + spd.ProtocolDataOffset);
            h.nvmeAvailable = true;
            h.criticalWarning = log->CriticalWarning.AsUchar;
            unsigned k = unsigned(log->Temperature[1]) << 8 | log->Temperature[0];
            h.temperatureC = k > 0 ? int(k) - 273 : -1;
            h.availableSparePct = log->AvailableSpare;
            h.percentageUsed = log->PercentageUsed;
            auto le = [](const UCHAR* p, int len) { uint64_t v = 0; for (int i = len - 1; i >= 0; --i) v = v << 8 | p[i]; return v; };
            h.powerOnHours = le(log->PowerOnHours, 8);  // low 64 bits of the 128-bit counter
            h.unsafeShutdowns = le(log->UnsafeShutdowns, 8);
            h.mediaErrors = le(log->MediaErrors, 8);
        }
    }

    // Generic predictive-failure flag (ATA SMART status exposed by the driver stack).
    {
        STORAGE_PREDICT_FAILURE pf{};
        DWORD ret = 0;
        if (DeviceIoControl(dev, IOCTL_STORAGE_PREDICT_FAILURE, nullptr, 0, &pf, sizeof pf, &ret, nullptr)) {
            h.predictFailureAvailable = true;
            h.predictFailure = pf.PredictFailure != 0;
            parseAtaSmart(pf.VendorSpecific, sizeof pf.VendorSpecific, h);
        }
    }
    CloseHandle(dev);
    if (!h.nvmeAvailable && !h.ataAvailable && !h.predictFailureAvailable)
        h.note = "SMART not supported or not exposed by this device/driver (common for USB bridges and virtual disks)";
    return h;
}

}  // namespace diskpro::win

