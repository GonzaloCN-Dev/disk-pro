#include <cstdio>
#include <string>
#include "update.h"
using namespace diskpro::update;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static const std::string H1(64, 'a'), H2(64, 'b');
static std::string rel(const std::string& tag, const std::string& assets, const std::string& extra = "") {
    return "{\"tag_name\":\"" + tag + "\",\"body\":\"Notas \\u00f1\\n- uno\"," + extra + "\"assets\":[" + assets + "]}";
}
static std::string asset(const std::string& name, const std::string& digest = "", const std::string& host = kAssetPrefix) {
    return "{\"name\":\"" + name + "\",\"size\":1234,\"browser_download_url\":\"" + host + "v0.3.0/" + name + "\"" +
           (digest.empty() ? "" : ",\"digest\":\"" + digest + "\"") + "}";
}
static std::string noFetch(const std::string&) { return ""; }

int main() {
    CHECK(isNewer("0.3.0", "0.2.1") && isNewer("v0.10.0", "0.9.2") && isNewer("1.0", "0.9.9") && isNewer("0.2.1.1", "0.2.1"));
    CHECK(!isNewer("0.2.1", "0.2.1") && !isNewer("0.2.0", "0.2.1") && !isNewer("", "0.2.1") && !isNewer("abc", "0.2.1") && !isNewer("..", "0.2.1"));
    CHECK(isNewer("0.3.0-beta", "0.2.1") && !isNewer("0.2.1-beta", "0.2.1"));

    auto sums = H1 + "  DiskPro-Setup-0.3.0.exe\r\n" + H2 + " *other.zip\n";
    CHECK(findHashInSums(sums, "DiskPro-Setup-0.3.0.exe") == H1 && findHashInSums(sums, "other.zip") == H2);
    CHECK(findHashInSums(sums, "missing.exe").empty() && findHashInSums("zzzz  x.exe", "x.exe").empty() && findHashInSums("", "x").empty());

    auto ok = parseRelease(rel("v0.3.0", asset("DiskPro-Setup-0.3.0.exe", "sha256:" + H1)), noFetch);
    CHECK(ok.ok && ok.version == "0.3.0" && ok.sha256 == H1 && ok.installerName == "DiskPro-Setup-0.3.0.exe" && ok.size == 1234);
    CHECK(ok.notes == "Notas \xC3\xB1\n- uno");

    // Hash taken from SHA256SUMS.txt when the asset has no digest
    auto viaSums = parseRelease(rel("v0.3.0", asset("DiskPro-Setup-0.3.0.exe") + "," + asset("SHA256SUMS.txt")),
                                [&](const std::string& u) { return u.find("SHA256SUMS.txt") != std::string::npos ? H2 + "  DiskPro-Setup-0.3.0.exe\n" : ""; });
    CHECK(viaSums.ok && viaSums.sha256 == H2);

    // Refusals
    CHECK(!parseRelease(rel("v0.3.0", asset("DiskPro-Setup-0.3.0.exe")), noFetch).ok);                           // no hash anywhere
    CHECK(!parseRelease(rel("v0.3.0", asset("DiskPro-Setup-0.3.0.exe", "sha256:1234")), noFetch).ok);           // malformed digest
    CHECK(!parseRelease(rel("v0.3.0", asset("DiskPro-Setup-0.3.0.exe", "sha256:" + H1, "https://evil.example/")), noFetch).ok);  // foreign host
    CHECK(!parseRelease(rel("v0.3.0", asset("DiskPro-Setup-0.3.0.exe", "sha256:" + H1, "http://github.com/GonzaloCN-Dev/disk-pro/releases/download/")), noFetch).ok);
    CHECK(!parseRelease(rel("v0.3.0", asset("DiskPro-Portable.zip", "sha256:" + H1)), noFetch).ok);             // no installer
    CHECK(!parseRelease(rel("v0.3.0", asset("DiskPro-Setup-0.3.0.exe", "sha256:" + H1), "\"prerelease\":true,"), noFetch).ok);
    CHECK(!parseRelease(rel("v0.3.0", asset("DiskPro-Setup-0.3.0.exe", "sha256:" + H1), "\"draft\":true,"), noFetch).ok);
    CHECK(!parseRelease(rel("vX", asset("DiskPro-Setup-0.3.0.exe", "sha256:" + H1)), noFetch).ok);
    CHECK(!parseRelease("", noFetch).ok && !parseRelease("[]", noFetch).ok && !parseRelease("{\"tag_name\":", noFetch).ok);
    CHECK(!parseRelease("{\"message\":\"Not Found\"}", noFetch).ok);
    CHECK(!parseRelease(std::string(5u << 20, ' ') + "{}", noFetch).ok);
    std::string deep(100, '['); CHECK(!parseRelease(deep, noFetch).ok);
    // A path separator in the asset name is never accepted as an installer
    CHECK(!parseRelease(rel("v0.3.0", asset("DiskPro-Setup-..\\\\x.exe", "sha256:" + H1)), noFetch).ok);

    std::printf(failures ? "update: %d failures\n" : "update: all passed\n", failures);
    return failures ? 1 : 0;
}
