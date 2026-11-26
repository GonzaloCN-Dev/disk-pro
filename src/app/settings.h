// Disk Pro - MIT License - Copyright (c) Gonzalo Cruz Núñez
// Minimal local settings file (no telemetry, no secrets). Stored in %APPDATA%\DiskPro\settings.ini.
#pragma once
#include <windows.h>

#include <cstdlib>
#include <fstream>
#include <string>

struct Settings {
    int theme = 1, density = 1, language = 0;
    bool autoUpdate = false;         // opt-in: off until the user turns it on
    long long lastUpdateCheck = 0;   // unix seconds

    static std::string path() {
        const char* a = std::getenv("APPDATA");
        return std::string(a ? a : ".") + "\\DiskPro\\settings.ini";
    }
    static Settings load() {
        Settings s;
        std::ifstream f(path());
        std::string line;
        while (std::getline(f, line)) {
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            auto k = line.substr(0, eq);
            long long big = std::atoll(line.c_str() + eq + 1);
            if (k == "lastupdatecheck") { if (big > 0) s.lastUpdateCheck = big; continue; }
            int v = int(big);
            if (v < 0 || v > 2) continue;  // validate input
            if (k == "theme") s.theme = v; else if (k == "density") s.density = v;
            else if (k == "language" && v <= 1) s.language = v;
            else if (k == "autoupdate" && v <= 1) s.autoUpdate = v == 1;
        }
        return s;
    }
    void save() const {
        std::string p = path();
        std::string dir = p.substr(0, p.find_last_of('\\'));
        CreateDirectoryA(dir.c_str(), nullptr);
        std::ofstream f(p, std::ios::trunc);
        f << "theme=" << theme << "\ndensity=" << density << "\nlanguage=" << language << "\nautoupdate=" << (autoUpdate ? 1 : 0)
          << "\nlastupdatecheck=" << lastUpdateCheck << "\n";
    }
};
