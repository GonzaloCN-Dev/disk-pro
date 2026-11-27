// Disk Pro - MIT License - Copyright (c) Gonzalo Cruz Núñez
#include <cstdio>
#include "inventory.h"

using namespace diskpro::win;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

int main() {
    CHECK(classifyBitLockerStatus(0, 0, true, true) == BitLockerStatus::NotEncrypted);
    CHECK(classifyBitLockerStatus(0, 1, true, true) == BitLockerStatus::Encrypted);
    CHECK(classifyBitLockerStatus(1, 0, true, true) == BitLockerStatus::Encrypted);
    CHECK(classifyBitLockerStatus(2, 0, true, true) == BitLockerStatus::Encrypting);
    CHECK(classifyBitLockerStatus(3, 1, true, true) == BitLockerStatus::Decrypting);
    CHECK(classifyBitLockerStatus(4, 0, true, true) == BitLockerStatus::Paused);
    CHECK(classifyBitLockerStatus(5, 0, true, true) == BitLockerStatus::Paused);
    CHECK(classifyBitLockerStatus(0, 2, true, false) == BitLockerStatus::NotEncrypted);
    CHECK(classifyBitLockerStatus(0, 2, false, true) == BitLockerStatus::Unknown);
    CHECK(classifyBitLockerStatus(9, 2, true, true) == BitLockerStatus::Unknown);
    std::printf(failures ? "inventory: %d failures\n" : "inventory: all passed\n", failures);
    return failures ? 1 : 0;
}
