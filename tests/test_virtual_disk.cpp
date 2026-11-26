#include <cstdio>
#include <chrono>
#include <filesystem>
#include "virtual_disk.h"

using namespace diskpro::win;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

int main() {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() / ("diskpro-test-disposable-" + std::to_string(id) + ".vhdx");
    std::error_code ec;
    std::filesystem::remove(path, ec);
    const auto utf8 = path.u8string();
    const std::string name(utf8.begin(), utf8.end());

    CHECK(!createVirtualDisk(name + ".txt", 16ull << 20, true).ok);
    CHECK(!createVirtualDisk(name, 1ull << 20, true).ok);
    auto r = createVirtualDisk(name, 16ull << 20, true);
    CHECK(r.ok && std::filesystem::is_regular_file(path));
    CHECK(!createVirtualDisk(name, 16ull << 20, true).ok);  // never overwrite
    std::filesystem::remove(path, ec);
    CHECK(!std::filesystem::exists(path));

    std::printf(failures ? "virtual-disk: %d failures\n" : "virtual-disk: all passed\n", failures);
    return failures ? 1 : 0;
}
