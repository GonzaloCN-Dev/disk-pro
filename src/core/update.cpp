// Disk Pro - MIT License - Copyright (c) Gonzalo Cruz Núñez
#include "update.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <memory>
#include <utility>
#include <vector>

namespace diskpro::update {
namespace {

struct Json {
    enum Type { Null, Bool, Num, Str, Arr, Obj } type = Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Json> arr;
    std::vector<std::pair<std::string, Json>> obj;
    const Json* get(const char* k) const {
        for (auto& kv : obj) if (kv.first == k) return &kv.second;
        return nullptr;
    }
    std::string str(const char* k) const { auto* v = get(k); return v && v->type == Str ? v->s : std::string(); }
};

class Parser {
public:
    explicit Parser(const std::string& t) : t_(t) {}
    bool parse(Json& out) { ws(); if (!value(out, 0)) return false; ws(); return p_ == t_.size(); }
private:
    const std::string& t_;
    size_t p_ = 0;
    void ws() { while (p_ < t_.size() && std::isspace(static_cast<unsigned char>(t_[p_]))) ++p_; }
    bool lit(const char* w) {
        size_t n = std::char_traits<char>::length(w);
        if (t_.compare(p_, n, w) != 0) return false;
        p_ += n;
        return true;
    }
    static void utf8(std::string& o, unsigned c) {
        if (c < 0x80) o += char(c);
        else if (c < 0x800) { o += char(0xC0 | (c >> 6)); o += char(0x80 | (c & 0x3F)); }
        else if (c < 0x10000) { o += char(0xE0 | (c >> 12)); o += char(0x80 | ((c >> 6) & 0x3F)); o += char(0x80 | (c & 0x3F)); }
        else { o += char(0xF0 | (c >> 18)); o += char(0x80 | ((c >> 12) & 0x3F)); o += char(0x80 | ((c >> 6) & 0x3F)); o += char(0x80 | (c & 0x3F)); }
    }
    bool hex4(unsigned& v) {
        if (p_ + 4 > t_.size()) return false;
        v = 0;
        for (int i = 0; i < 4; ++i) {
            char c = t_[p_++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= unsigned(c - '0');
            else if (c >= 'a' && c <= 'f') v |= unsigned(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= unsigned(c - 'A' + 10);
            else return false;
        }
        return true;
    }
    bool string(std::string& o) {
        if (p_ >= t_.size() || t_[p_] != '"') return false;
        ++p_;
        while (p_ < t_.size()) {
            char c = t_[p_++];
            if (c == '"') return true;
            if (static_cast<unsigned char>(c) < 0x20) return false;
            if (c != '\\') { o += c; continue; }
            if (p_ >= t_.size()) return false;
            char e = t_[p_++];
            switch (e) {
                case '"': o += '"'; break; case '\\': o += '\\'; break; case '/': o += '/'; break;
                case 'b': o += '\b'; break; case 'f': o += '\f'; break; case 'n': o += '\n'; break;
                case 'r': o += '\r'; break; case 't': o += '\t'; break;
                case 'u': {
                    unsigned u;
                    if (!hex4(u)) return false;
                    if (u >= 0xD800 && u < 0xDC00 && p_ + 1 < t_.size() && t_[p_] == '\\' && t_[p_ + 1] == 'u') {
                        p_ += 2;
                        unsigned lo;
                        if (!hex4(lo) || lo < 0xDC00 || lo > 0xDFFF) return false;
                        u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    utf8(o, u);
                    break;
                }
                default: return false;
            }
        }
        return false;
    }
    bool value(Json& j, int depth) {
        if (depth > 32 || p_ >= t_.size()) return false;
        char c = t_[p_];
        if (c == '{') {
            ++p_; j.type = Json::Obj; ws();
            if (p_ < t_.size() && t_[p_] == '}') { ++p_; return true; }
            for (;;) {
                ws();
                std::string k;
                if (!string(k)) return false;
                ws();
                if (p_ >= t_.size() || t_[p_++] != ':') return false;
                ws();
                Json v;
                if (!value(v, depth + 1)) return false;
                j.obj.emplace_back(std::move(k), std::move(v));
                ws();
                if (p_ >= t_.size()) return false;
                char d = t_[p_++];
                if (d == '}') return true;
                if (d != ',') return false;
            }
        }
        if (c == '[') {
            ++p_; j.type = Json::Arr; ws();
            if (p_ < t_.size() && t_[p_] == ']') { ++p_; return true; }
            for (;;) {
                ws();
                Json v;
                if (!value(v, depth + 1)) return false;
                j.arr.push_back(std::move(v));
                ws();
                if (p_ >= t_.size()) return false;
                char d = t_[p_++];
                if (d == ']') return true;
                if (d != ',') return false;
            }
        }
        if (c == '"') { j.type = Json::Str; return string(j.s); }
        if (lit("true")) { j.type = Json::Bool; j.b = true; return true; }
        if (lit("false")) { j.type = Json::Bool; return true; }
        if (lit("null")) { j.type = Json::Null; return true; }
        char* end = nullptr;
        j.n = std::strtod(t_.c_str() + p_, &end);
        if (end == t_.c_str() + p_) return false;
        p_ = size_t(end - t_.c_str());
        j.type = Json::Num;
        return true;
    }
};

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}
bool isHex64(const std::string& s) {
    return s.size() == 64 && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isxdigit(c) != 0; });
}
std::vector<int> parseVersion(const std::string& v) {
    std::vector<int> out;
    size_t i = (!v.empty() && (v[0] == 'v' || v[0] == 'V')) ? 1 : 0;
    int cur = 0;
    bool any = false;
    for (; i < v.size(); ++i) {
        if (std::isdigit(static_cast<unsigned char>(v[i]))) {
            if (cur > 100000) return {};
            cur = cur * 10 + (v[i] - '0');
            any = true;
        } else if (v[i] == '.') {
            if (!any) return {};
            out.push_back(cur); cur = 0; any = false;
        } else break;
    }
    if (any) out.push_back(cur);
    return out;
}
bool endsWith(const std::string& s, const char* suf) {
    size_t n = std::char_traits<char>::length(suf);
    return s.size() >= n && s.compare(s.size() - n, n, suf) == 0;
}

}  // namespace

bool isNewer(const std::string& remote, const std::string& local) {
    auto a = parseVersion(remote), b = parseVersion(local);
    if (a.empty()) return false;
    size_t n = std::max(a.size(), b.size());
    a.resize(n, 0); b.resize(n, 0);
    return std::lexicographical_compare(b.begin(), b.end(), a.begin(), a.end());
}

std::string findHashInSums(const std::string& sums, const std::string& fileName) {
    size_t pos = 0;
    while (pos < sums.size()) {
        size_t e = sums.find('\n', pos);
        std::string line = sums.substr(pos, e == std::string::npos ? std::string::npos : e - pos);
        pos = e == std::string::npos ? sums.size() : e + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.size() < 66) continue;
        std::string h = lower(line.substr(0, 64));
        std::string name = line.substr(64);
        name.erase(0, name.find_first_not_of(" *"));
        if (isHex64(h) && name == fileName) return h;
    }
    return "";
}

Release parseRelease(const std::string& body, const std::function<std::string(const std::string&)>& fetchText) {
    Release r;
    Json j;
    if (body.size() > (4u << 20) || !Parser(body).parse(j) || j.type != Json::Obj) { r.error = "invalid answer from GitHub"; return r; }
    auto flag = [&](const char* k) { auto* v = j.get(k); return v && v->type == Json::Bool && v->b; };
    if (flag("draft") || flag("prerelease")) { r.error = "no stable release is published"; return r; }
    r.tag = j.str("tag_name");
    r.version = (!r.tag.empty() && (r.tag[0] == 'v' || r.tag[0] == 'V')) ? r.tag.substr(1) : r.tag;
    r.notes = j.str("body");
    if (parseVersion(r.version).empty()) { r.error = "the published version is not valid"; return r; }

    std::string sumsUrl;
    auto* assets = j.get("assets");
    if (assets && assets->type == Json::Arr) {
        for (auto& a : assets->arr) {
            std::string name = a.str("name"), url = a.str("browser_download_url");
            if (url.rfind(kAssetPrefix, 0) != 0) continue;
            std::string ln = lower(name);
            if (ln.rfind("diskpro-setup-", 0) == 0 && endsWith(ln, ".exe") && name.find_first_of("\\/") == std::string::npos) {
                r.installerName = name; r.installerUrl = url;
                auto* sz = a.get("size");
                if (sz && sz->type == Json::Num) r.size = int64_t(sz->n);
                std::string d = lower(a.str("digest"));
                if (d.rfind("sha256:", 0) == 0 && isHex64(d.substr(7))) r.sha256 = d.substr(7);
            } else if (ln == "sha256sums.txt") sumsUrl = url;
        }
    }
    if (r.installerUrl.empty()) { r.error = "release " + r.tag + " has no installer"; return r; }
    if (r.sha256.empty() && !sumsUrl.empty() && fetchText) r.sha256 = findHashInSums(fetchText(sumsUrl), r.installerName);
    if (r.sha256.empty()) { r.error = "release " + r.tag + " publishes no SHA-256 to verify the download"; return r; }
    r.ok = true;
    return r;
}

}  // namespace diskpro::update
