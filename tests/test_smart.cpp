// Disk Pro - MIT License - Copyright (c) Gonzalo Cruz Núñez
#include <array>
#include <cstdio>
#include "smart.h"

using namespace diskpro::win;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static void attr(std::array<uint8_t, 512>& d, int slot, uint8_t id, uint64_t raw) {
    const size_t off = 2 + size_t(slot) * 12;
    d[off] = id; d[off + 3] = 100; d[off + 4] = 100;
    for (int i = 0; i < 6; ++i) d[off + 5 + i] = uint8_t(raw >> (8 * i));
}

int main() {
    std::array<uint8_t, 512> d{};
    attr(d, 0, 5, 3); attr(d, 1, 9, 12345); attr(d, 2, 194, 42); attr(d, 3, 197, 2); attr(d, 4, 198, 1);
    HealthInfo h;
    CHECK(parseAtaSmart(d.data(), d.size(), h));
    CHECK(h.ataAvailable && h.reallocatedSectors == 3 && h.powerOnHours == 12345 && h.temperatureC == 42);
    CHECK(h.pendingSectors == 2 && h.uncorrectableSectors == 1);
    HealthInfo empty;
    std::array<uint8_t, 512> z{};
    CHECK(!parseAtaSmart(z.data(), z.size(), empty) && !parseAtaSmart(nullptr, 0, empty));
    if (!failures) std::printf("smart: all passed\n");
    return failures ? 1 : 0;
}
