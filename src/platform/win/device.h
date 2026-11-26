// Disk Pro - MIT License - Copyright (c) Gonzalo Cruz Núñez
// Read-only access to a physical drive (GENERIC_READ only, sector-aligned internally).
#pragma once
#include <vector>
#include "layout.h"
#include <vector>
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

// Raw write access. Construction revalidates the preview facts and only opens an eligible virtual, non-system disk.
class DeviceSink final : public ByteSink {
public:
    DeviceSink(int diskNumber, const TargetFacts& expected);
    ~DeviceSink() override;
    bool ok() const { return h_ != nullptr; }
    bool write(uint64_t off, const void* buf, size_t len) override;
    bool flush() override;
    bool prepare(uint64_t offset, uint64_t length) override;
    void refreshLayout();
private:
    int disk_ = -1;
    std::vector<void*> locked_;   // volume handles kept open (locked + dismounted) until the sink is destroyed
    void* h_ = nullptr;
    uint64_t size_ = 0;
    uint32_t sector_ = 512;
};

}  // namespace diskpro
