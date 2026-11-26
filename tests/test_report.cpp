#include <cstdio>
#include "report.h"
using namespace diskpro::win;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)
int main() {
    DiskInfo d; d.number = 3; d.vendor = "Msft"; d.product = "Virtual Disk"; d.sizeBytes = 128ull << 20; d.busType = "FileBackedVirtual";
    d.style = "GPT"; d.logicalSector = 512; d.physicalSector = 512; d.serialMasked = "****1234"; d.isVirtual = true;
    PartitionInfo p; p.number = 1; p.offsetBytes = 65536; p.lengthBytes = 40ull << 20; p.typeName = "Basic"; p.filesystem = "NTFS"; p.volume = "E:\\";
    d.partitions.push_back(p);
    DiskInfo s; s.number = 0; s.isSystemDisk = true; s.sizeBytes = 1ull << 40;
    auto r = buildReport({d, s}, {"NVMe 30 C"}, "9.9.9");
    CHECK(r.find("Disk Pro 9.9.9") != std::string::npos);
    CHECK(r.find("Disk 3: Msft Virtual Disk") != std::string::npos && r.find("128.00 MiB") != std::string::npos);
    CHECK(r.find("Serial: ****1234") != std::string::npos && r.find("Health: NVMe 30 C") != std::string::npos);
    CHECK(r.find("Partition 1: offset 65536, 40.00 MiB, Basic, NTFS, E:\\") != std::string::npos);
    CHECK(r.find("[SYSTEM]") != std::string::npos && r.find("1024.00 GiB") != std::string::npos);
    CHECK(buildReport({}, {}, "1").find("No disks found.") != std::string::npos);
    std::printf(failures ? "report: %d failures\n" : "report: all passed\n", failures);
    return failures ? 1 : 0;
}
