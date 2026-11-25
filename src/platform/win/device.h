// Disk Pro - MIT License - Copyright (c) Gonzalo Cruz Núñez
// Read-only access to a physical drive (GENERIC_READ only, sector-aligned internally).
#pragma once
#include "layout.h"
#include "restore.h"

namespace diskpro {

class DeviceSource final : public ByteSource {
public:
    explicit DeviceSource(int diskNumber);
    ~DeviceSource() override;
    bool ok() const { return h_ != nullptr; }
    uint64_t size() const override { return size_; }
    bool read(uint64_t off, void* buf, size_t len) const override;
private:
    void* h_ = nullptr;
    uint64_t size_ = 0;
    uint32_t sector_ = 512;
};

// Write access to a physical drive. Only created by the restore tool after planRestore() allowed it.
class DeviceSink final : public ByteSink {
public:
    explicit DeviceSink(int diskNumber);
    ~DeviceSink() override;
    bool ok() const { return h_ != nullptr; }
    bool write(uint64_t off, const void* buf, size_t len) override;
    bool flush() override;
    void refreshLayout();
private:
    void* h_ = nullptr;
    uint64_t size_ = 0;
    uint32_t sector_ = 512;
};

}  // namespace diskpro

