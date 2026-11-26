// Disk Pro - MIT License - Copyright (c) Gonzalo Cruz Núñez
#include "updater.h"
#include <windows.h>
#include <winhttp.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <vector>
#include "tools.h"

namespace fs = std::filesystem;

namespace diskpro::win {
namespace {

constexpr char kLatestApi[] = "https://api.github.com/repos/GonzaloCN-Dev/disk-pro/releases/latest";
constexpr size_t kMaxBody = 4u << 20;
constexpr int64_t kMaxInstaller = 512ll << 20;

struct Handle {
    HINTERNET h;
    explicit Handle(HINTERNET v = nullptr) : h(v) {}
    ~Handle() { if (h) WinHttpCloseHandle(h); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};

std::wstring widen(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}
std::string err(const char* what) { return std::string(what) + " (error " + std::to_string(GetLastError()) + ")"; }

// Streams a GET to sink (returns false to abort). https only; redirects stay on https.
std::string httpGet(const std::string& url, bool json, const std::function<bool(const char*, size_t)>& sink,
                    const std::function<void(int64_t)>& onLength = nullptr) {
    std::wstring wurl = widen(url);
    URL_COMPONENTSW uc{};
    uc.dwStructSize = sizeof uc;
    uc.dwSchemeLength = uc.dwHostNameLength = uc.dwUrlPathLength = uc.dwExtraInfoLength = DWORD(-1);
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) return "invalid URL";
    if (uc.nScheme != INTERNET_SCHEME_HTTPS) return "only https connections are allowed";
    std::wstring host(uc.lpszHostName, uc.dwHostNameLength);
    std::wstring path(uc.lpszUrlPath, uc.dwUrlPathLength);
    path.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);

    Handle session(WinHttpOpen(L"DiskPro-updater", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session.h) return err("cannot start WinHTTP");
    WinHttpSetTimeouts(session.h, 10000, 10000, 20000, 30000);
    DWORD redirPolicy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    WinHttpSetOption(session.h, WINHTTP_OPTION_REDIRECT_POLICY, &redirPolicy, sizeof redirPolicy);
    Handle conn(WinHttpConnect(session.h, host.c_str(), uc.nPort, 0));
    if (!conn.h) return err("cannot connect");
    Handle req(WinHttpOpenRequest(conn.h, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (!req.h) return err("cannot create the request");
    if (json) WinHttpAddRequestHeaders(req.h, L"Accept: application/vnd.github+json", DWORD(-1), WINHTTP_ADDREQ_FLAG_ADD);
    if (!WinHttpSendRequest(req.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(req.h, nullptr))
        return err("no connection to the server");
    DWORD status = 0, sz = sizeof status;
    WinHttpQueryHeaders(req.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
    if (status != 200) return "the server answered " + std::to_string(status);
    if (onLength) {
        wchar_t len[32] = {};
        DWORD ls = sizeof len;
        if (WinHttpQueryHeaders(req.h, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, len, &ls, WINHTTP_NO_HEADER_INDEX))
            onLength(_wtoi64(len));
    }
    std::vector<char> buf(64 * 1024);
    for (;;) {
        DWORD got = 0;
        if (!WinHttpReadData(req.h, buf.data(), DWORD(buf.size()), &got)) return err("the download was interrupted");
        if (got == 0) break;
        if (!sink(buf.data(), got)) return "cancelled";
    }
    return "";
}

}  // namespace

update::Release fetchLatestRelease() {
    update::Release r;
    std::string body;
    std::string e = httpGet(kLatestApi, true, [&](const char* p, size_t n) {
        if (body.size() + n > kMaxBody) return false;
        body.append(p, n);
        return true;
    });
    if (!e.empty()) { r.error = e; return r; }
    return update::parseRelease(body, [](const std::string& url) {
        std::string text;
        httpGet(url, false, [&](const char* p, size_t n) {
            if (text.size() + n > 65536) return false;
            text.append(p, n);
            return true;
        });
        return text;
    });
}

std::string updateDir() {
    wchar_t tmp[MAX_PATH];
    DWORD n = GetTempPathW(MAX_PATH, tmp);
    fs::path p = (n && n < MAX_PATH) ? fs::path(tmp) : fs::temp_directory_path();
    auto u = (p / L"DiskPro-update").u8string();
    return std::string(u.begin(), u.end());
}

std::string downloadAsset(const std::string& url, const std::string& path, const std::function<void(int64_t, int64_t)>& progress,
                          const std::function<bool()>& cancelled) {
    if (url.rfind(update::kAssetPrefix, 0) != 0) return "download source not allowed";
    fs::path target = fs::u8path(path), part = target;
    part += L".part";
    std::error_code ec;
    fs::create_directories(target.parent_path(), ec);
    fs::remove(part, ec);
    std::string e;
    {
        std::ofstream out(part, std::ios::binary | std::ios::trunc);
        if (!out) return "cannot create the temporary file";
        int64_t total = 0, done = 0;
        e = httpGet(url, false,
                    [&](const char* p, size_t n) {
                        if (cancelled && cancelled()) return false;
                        done += int64_t(n);
                        if (done > kMaxInstaller) return false;
                        out.write(p, std::streamsize(n));
                        if (!out) return false;
                        if (progress) progress(done, total);
                        return true;
                    },
                    [&](int64_t len) { total = len; });
        out.close();
        if (e.empty() && !out) e = "cannot write to disk";
    }
    if (!e.empty()) { fs::remove(part, ec); return e; }
    fs::remove(target, ec);
    fs::rename(part, target, ec);
    if (ec) return "cannot save the installer: " + ec.message();
    return "";
}

bool verifyDownloaded(const std::string& path, const std::string& expectedHex) {
    std::ifstream in(fs::u8path(path), std::ios::binary);
    std::string got;
    if (in) {
        Sha256 h;
        std::vector<char> buf(256 * 1024);
        while (in) {
            in.read(buf.data(), std::streamsize(buf.size()));
            if (in.gcount() > 0) h.update(buf.data(), size_t(in.gcount()));
        }
        got = h.finalHex();
    }
    std::string want = expectedHex;
    std::transform(want.begin(), want.end(), want.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (!got.empty() && want.size() == 64 && got == want) return true;
    in.close();
    std::error_code ec;
    fs::remove(fs::u8path(path), ec);
    return false;
}

bool runInstaller(const std::string& path) {
    wchar_t self[MAX_PATH];
    bool allUsers = false;
    if (GetModuleFileNameW(nullptr, self, MAX_PATH)) {
        wchar_t pf[MAX_PATH];
        for (const wchar_t* var : {L"ProgramFiles", L"ProgramFiles(x86)"}) {
            DWORD n = GetEnvironmentVariableW(var, pf, MAX_PATH);
            if (n && n < MAX_PATH && _wcsnicmp(self, pf, wcslen(pf)) == 0) allUsers = true;
        }
    }
    std::wstring cmd = L"\"" + fs::u8path(path).wstring() + L"\" /SILENT /SUPPRESSMSGBOXES /NORESTART /SP- /CLOSEAPPLICATIONS /RELAUNCH=1 " +
                       (allUsers ? L"/ALLUSERS" : L"/CURRENTUSER");
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_BREAKAWAY_FROM_JOB | CREATE_NEW_PROCESS_GROUP, nullptr, nullptr, &si, &pi);
    if (!ok) ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NEW_PROCESS_GROUP, nullptr, nullptr, &si, &pi);
    if (!ok) return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

}  // namespace diskpro::win

