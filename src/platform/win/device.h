// Disk Pro - MIT License - Copyright (c) Gonzalo Cruz Núñez
// Read-only access to a physical drive (GENERIC_READ only, sector-aligned internally).
#pragma once
#include "layout.h"

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

}  // namespace diskpro
