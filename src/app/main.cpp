// Disk Pro GUI - MIT License - Copyright (c) Gonzalo Cruz Núñez
// Read-only. Disk queries run on a worker thread; results are posted back to the UI thread.
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "inventory.h"
#include "layout.h"
#include "main.h"
#include "map.h"
#include "settings.h"
#include "smart.h"
#include "device.h"
#include "tools.h"
#include <commdlg.h>
#include <cstdlib>
#include <chrono>

using namespace diskpro;

namespace {

struct Strings {
    const char *title, *disks, *parts, *diag, *filter, *refresh, *theme, *lang, *density, *about, *close, *loading, *empty,
        *err, *ro, *health, *nodiag, *cnum, *ctype, *cfs, *csize, *coff, *cvol, *free, *copy, *select;
    const char *themes[3], *densities[3];
};

const Strings kEs = {"Disk Pro", "Discos", "Particiones", "Diagnóstico (solo lectura)", "Filtrar discos…", "Actualizar", "Tema",
                     "Idioma", "Densidad", "Acerca de", "Cerrar", "Cargando discos…", "No se encontraron discos.", "Error: ",
                     "Modo solo lectura: esta versión no modifica discos.", "Salud", "Sin incidencias detectadas.", "N.º",
                     "Tipo", "Sistema", "Tamaño", "Desplazamiento", "Volumen", "Libre",
                     "© Gonzalo Cruz Núñez · GCN Dev. Software libre bajo licencia MIT.", "Selecciona un disco.",
                     {"Sistema", "Claro", "Oscuro"}, {"Compacta", "Normal", "Amplia"}};
const Strings kEn = {"Disk Pro", "Disks", "Partitions", "Diagnostics (read-only)", "Filter disks…", "Refresh", "Theme",
                     "Language", "Density", "About", "Close", "Loading disks…", "No disks found.", "Error: ",
                     "Read-only mode: this version does not modify disks.", "Health", "No issues found.", "No.",
                     "Type", "File system", "Size", "Offset", "Volume", "Free",
                     "© Gonzalo Cruz Núñez · GCN Dev. Free software under the MIT license.", "Select a disk.",
                     {"System", "Light", "Dark"}, {"Compact", "Normal", "Comfortable"}};

struct ToolStr {
    const char *tools, *hash, *image, *scan, *hex, *open, *cancel, *prev, *next, *offset, *back, *note, *srcFile, *noSource,
        *cantOpen, *cancelled, *done, *badBlocks, *outExists, *srcDisk, *speed;
};
const ToolStr kToolEs = {"Herramientas", "SHA-256", "Crear imagen…", "Analizar superficie", "Ver hex", "Abrir archivo…", "Cancelar",
                         "◀ Anterior", "Siguiente ▶", "Offset:", "Volver a discos",
                         "Solo lectura: el origen nunca se modifica. Los discos físicos requieren ejecutar Disk Pro como administrador; los archivos de imagen no.",
                         "Origen: archivo ", "Selecciona un disco o abre un archivo.",
                         "No se pudo abrir el origen (¿falta ejecutar como administrador?).", "Cancelado.", "Terminado.",
                         "bloques ilegibles: ", "El archivo de salida ya existe; elige otro nombre.", "Origen: disco", "MB/s"};
const ToolStr kToolEn = {"Tools", "SHA-256", "Create image…", "Surface scan", "View hex", "Open file…", "Cancel",
                         "◀ Previous", "Next ▶", "Offset:", "Back to disks",
                         "Read-only: the source is never modified. Physical disks need Disk Pro to run as administrator; image files do not.",
                         "Source: file ", "Select a disk or open a file.", "Cannot open the source (run as administrator?).",
                         "Cancelled.", "Finished.", "unreadable blocks: ", "Output file already exists; choose another name.",
                         "Source: disk", "MB/s"};

std::string pickFile(bool save, const wchar_t* ext = L"img") {
    wchar_t buf[MAX_PATH * 4] = L"";
    OPENFILENAMEW o{};
    o.lStructSize = sizeof o;
    o.lpstrFile = buf;
    o.nMaxFile = DWORD(std::size(buf));
    o.lpstrFilter = L"Disk images (*.img;*.vhd;*.bin;*.raw;*.dd)\0*.img;*.vhd;*.bin;*.raw;*.dd\0All files\0*.*\0";
    o.Flags = save ? (OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR) : (OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR);
    if (save) o.lpstrDefExt = ext;
    if (!(save ? GetSaveFileNameW(&o) : GetOpenFileNameW(&o))) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, buf, -1, nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n > 0 ? n - 1 : 0), 0);
    WideCharToMultiByte(CP_UTF8, 0, buf, -1, s.data(), n, nullptr, nullptr);
    return s;
}

template <class T> std::shared_ptr<slint::VectorModel<T>> vec() { return std::make_shared<slint::VectorModel<T>>(); }

slint::SharedString S(const std::string& s) { return slint::SharedString(s); }

struct State {
    std::vector<win::DiskInfo> disks;
    std::vector<std::string> health;
    std::vector<int> visible;  // indices into disks after filtering
    std::atomic<bool> busy{false};
    std::atomic<unsigned> generation{0};  // cancels stale results
    std::string error;
};

}  // namespace

int main(int argc, char** argv) {
    bool selfTest = argc > 1 && std::string(argv[1]) == "--selftest";
    auto ui = MainWindow::create();
    auto st = std::make_shared<State>();
    auto settings = std::make_shared<Settings>(Settings::load());
    int lang = settings->language, density = settings->density;

    auto applyStrings = [ui, &lang]() {
        const Strings& t = lang == 1 ? kEn : kEs;
        auto& g = ui->global<Tr>();
        g.set_app_title(S(t.title)); g.set_disks(S(t.disks)); g.set_partitions(S(t.parts)); g.set_diagnostics(S(t.diag));
        g.set_filter_hint(S(t.filter)); g.set_refresh(S(t.refresh)); g.set_theme(S(t.theme)); g.set_language(S(t.lang));
        g.set_density(S(t.density)); g.set_about(S(t.about)); g.set_close(S(t.close)); g.set_loading(S(t.loading));
        g.set_empty(S(t.empty)); g.set_error_prefix(S(t.err)); g.set_read_only(S(t.ro)); g.set_health(S(t.health));
        g.set_no_diag(S(t.nodiag)); g.set_col_num(S(t.cnum)); g.set_col_type(S(t.ctype)); g.set_col_fs(S(t.cfs));
        g.set_col_size(S(t.csize)); g.set_col_offset(S(t.coff)); g.set_col_volume(S(t.cvol)); g.set_free_space(S(t.free));
        g.set_copyright(S(t.copy)); g.set_select_disk(S(t.select));
        auto tm = vec<slint::SharedString>();
        auto dm = vec<slint::SharedString>();
        for (auto* s : t.themes) tm->push_back(S(s));
        for (auto* s : t.densities) dm->push_back(S(s));
        g.set_theme_options(tm); g.set_density_options(dm);
    };
    applyStrings();
    ui->set_language_index(lang); ui->set_density_index(density); ui->set_theme_index(settings->theme);

    auto filterText = std::make_shared<std::string>();
    auto selected = std::make_shared<int>(-1);

    auto showDisk = [ui, st, selected](int visIdx) {
        auto parts = vec<PartRow>();
        auto segs = vec<SegRow>();
        auto diags = vec<DiagRow>();
        *selected = visIdx;
        ui->set_selected(visIdx);
        if (visIdx >= 0 && visIdx < int(st->visible.size())) {
            int di = st->visible[visIdx];
            const auto& d = st->disks[di];
            std::vector<Extent> ex;
            for (auto& p : d.partitions) {
                parts->push_back({S(std::to_string(p.number)), S(p.typeName.empty() ? "-" : p.typeName), S(p.filesystem),
                                  S(humanSize(p.lengthBytes)), S(humanSize(p.offsetBytes)), S(p.volume)});
                ex.push_back({int(p.number), p.offsetBytes, p.lengthBytes});
            }
            auto map = buildMap(d.sizeBytes, d.physicalSector > 4096 ? 4096 : d.physicalSector, ex);
            float x = 0;
            const Strings& t = ui->get_language_index() == 1 ? kEn : kEs;
            for (auto& s : map.segments) {
                segs->push_back({S(s.free ? t.free : "#" + std::to_string(s.partitionIndex)), S(humanSize(s.length)), x,
                                 float(s.fraction), s.free});
                x += float(s.fraction);
            }
            for (auto& g : map.diagnostics) diags->push_back({int(g.severity), S(g.message)});
            if (!d.volumes.empty()) {
                std::string v;
                for (auto& s : d.volumes) v += s + " ";
                diags->push_back({0, S((ui->get_language_index() == 1 ? "Volumes: " : "Volúmenes: ") + v)});
            }
            if (d.isSystemDisk) diags->push_back({1, S(ui->get_language_index() == 1 ? "System disk: protected from any future write operation." : "Disco de sistema: protegido frente a futuras operaciones de escritura.")});
            if (!d.error.empty()) diags->push_back({1, S("Partial data: " + d.error)});
            ui->set_health_text(S(st->health[di]));
        }
        ui->set_parts(parts); ui->set_segments(segs); ui->set_diags(diags);
    };

    auto rebuildList = [ui, st, filterText, showDisk]() {
        auto rows = vec<DiskRow>();
        st->visible.clear();
        std::string f = *filterText;
        std::transform(f.begin(), f.end(), f.begin(), [](unsigned char c) { return std::tolower(c); });
        for (size_t i = 0; i < st->disks.size(); ++i) {
            auto& d = st->disks[i];
            std::string name = "Disk " + std::to_string(d.number) + " – " + d.vendor + " " + d.product;
            std::string hay = name + d.busType + d.style;
            for (auto& v : d.volumes) hay += v;
            std::transform(hay.begin(), hay.end(), hay.begin(), [](unsigned char c) { return std::tolower(c); });
            if (!f.empty() && hay.find(f) == std::string::npos) continue;
            st->visible.push_back(int(i));
            rows->push_back({S(name), S(d.busType), S(humanSize(d.sizeBytes)), S(d.style.empty() ? "RAW" : d.style),
                             S(d.isSystemDisk ? "SYSTEM" : d.isVirtual ? "virtual" : "")});
        }
        ui->set_disks(rows);
        showDisk(st->visible.empty() ? -1 : 0);
    };

    auto refresh = [ui, st, rebuildList, selfTest]() {
        if (st->busy.exchange(true)) return;
        unsigned gen = ++st->generation;
        ui->set_loading(true);
        std::thread([ui, st, rebuildList, gen, selfTest]() {
            auto disks = win::enumerateDisks();
            std::vector<std::string> health;
            for (auto& d : disks) {
                auto h = win::queryHealth(d.number);
                char b[160];
                if (h.nvmeAvailable)
                    std::snprintf(b, sizeof b, "NVMe %d °C, %u%% spare, %u%% used, %llu h, warn 0x%02X", h.temperatureC,
                                  h.availableSparePct, h.percentageUsed, (unsigned long long)h.powerOnHours, h.criticalWarning);
                else if (h.predictFailureAvailable) std::snprintf(b, sizeof b, "SMART: %s", h.predictFailure ? "FAILURE PREDICTED" : "OK");
                else std::snprintf(b, sizeof b, "n/a");
                health.push_back(b);
            }
            slint::invoke_from_event_loop([=]() {
                if (gen == st->generation.load()) {
                    st->disks = disks; st->health = health;
                    rebuildList();
                    ui->set_loading(false);
                }
                st->busy = false;
                if (selfTest) slint::quit_event_loop();
            });
        }).detach();
    };

    ui->on_refresh(refresh);
    ui->on_disk_selected(showDisk);
    ui->on_filter_changed([filterText, rebuildList](slint::SharedString t) { *filterText = std::string(t); rebuildList(); });
    ui->on_theme_changed([ui, settings](int i) { ui->set_theme_index(i); settings->theme = i; settings->save(); });
    ui->on_density_changed([ui, settings](int i) { ui->set_density_index(i); settings->density = i; settings->save(); });
    ui->on_language_changed([ui, settings, applyStrings, &lang, rebuildList](int i) {
        lang = i; settings->language = i; settings->save();
        ui->set_language_index(i); applyStrings(); rebuildList();
    });

    const ToolStr* ts = lang == 1 ? &kToolEn : &kToolEs;
    auto toolStrings = [ui, &lang]() {
        const ToolStr& s = lang == 1 ? kToolEn : kToolEs;
        auto& g = ui->global<Tr>();
        g.set_tools(S(s.tools)); g.set_t_hash(S(s.hash)); g.set_t_image(S(s.image)); g.set_t_scan(S(s.scan));
        g.set_t_hex(S(s.hex)); g.set_t_open(S(s.open)); g.set_t_cancel(S(s.cancel)); g.set_t_prev(S(s.prev));
        g.set_t_next(S(s.next)); g.set_t_offset(S(s.offset)); g.set_t_back(S(s.back)); g.set_t_ptb(S(lang == 1 ? "Back up partition table…" : "Copia de la tabla de particiones…")); g.set_t_note(S(s.note));
    };
    (void)ts;
    toolStrings();
    ui->on_language_changed([ui, settings, applyStrings, &lang, rebuildList, toolStrings](int i) {
        lang = i; settings->language = i; settings->save();
        ui->set_language_index(i); applyStrings(); toolStrings(); rebuildList();
    });

    auto filePath = std::make_shared<std::string>();
    auto cancelFlag = std::make_shared<std::atomic<bool>>(false);
    auto toolBusy = std::make_shared<std::atomic<bool>>(false);
    auto sourceLabel = [ui, st, selected, filePath, &lang]() {
        const ToolStr& s = lang == 1 ? kToolEn : kToolEs;
        if (!filePath->empty()) ui->set_source_label(S(std::string(s.srcFile) + *filePath));
        else if (*selected >= 0 && *selected < int(st->visible.size()))
            ui->set_source_label(S(std::string(s.srcDisk) + " " + std::to_string(st->disks[st->visible[*selected]].number)));
        else ui->set_source_label(S("-"));
    };
    ui->on_disk_selected([showDisk, filePath, sourceLabel](int i) { filePath->clear(); showDisk(i); sourceLabel(); });
    ui->on_tool_open_file([filePath, sourceLabel]() {
        auto p = pickFile(false);
        if (!p.empty()) { *filePath = p; sourceLabel(); }
    });
    ui->on_tool_cancel([cancelFlag]() { cancelFlag->store(true); });
    ui->on_tool_run([ui, st, selected, filePath, cancelFlag, toolBusy, &lang](slint::SharedString cmdS, slint::SharedString arg) {
        const ToolStr& s = lang == 1 ? kToolEn : kToolEs;
        std::string cmd(cmdS), a(arg);
        std::shared_ptr<ByteSource> src;
        if (!filePath->empty()) {
            auto f = std::make_shared<FileSource>(*filePath);
            if (f->ok()) src = f;
        } else if (*selected >= 0 && *selected < int(st->visible.size())) {
            auto d = std::make_shared<DeviceSource>(st->disks[st->visible[*selected]].number);
            if (d->ok()) src = d;
        } else { ui->set_tool_status(S(s.noSource)); return; }
        if (!src) { ui->set_tool_status(S(s.cantOpen)); return; }
        if (cmd.rfind("hex", 0) == 0) {
            uint64_t off = std::strtoull(a.c_str(), nullptr, 0);
            if (cmd == "hexnext") off += 512;
            if (cmd == "hexprev") off = off >= 512 ? off - 512 : 0;
            auto out = hexDump(*src, off, 512);
            if (out.empty()) { ui->set_tool_status(S("Read error / offset out of range")); return; }
            ui->set_hex_offset(S(std::to_string(off)));
            ui->set_tool_output(S(out));
            ui->set_tool_status(S("offset " + std::to_string(off)));
            return;
        }
        if (cmd == "ptbackup") {
            std::string p = pickFile(true, L"ptb");
            if (p.empty()) return;
            if (p.find('.', p.find_last_of("\\/") == std::string::npos ? 0 : p.find_last_of("\\/")) == std::string::npos) p += ".ptb";
            auto r = backupPartitionTable(*src, p);
            ui->set_tool_progress(r.ok ? 1.f : 0.f);
            if (r.ok) {
                ui->set_tool_status(S(s.done));
                ui->set_tool_output(S(std::string(s.done) + "\n" + p + "\nSHA-256: " + r.sha256 + "\n" + r.scheme + ", " + humanSize(r.headBytes + r.tailBytes)));
            } else {
                ui->set_tool_status(S(r.error == "output file already exists" ? s.outExists : "Error: " + r.error));
                ui->set_tool_output(S(""));
            }
            return;
        }
        std::string outPath;
        if (cmd == "image") {
            outPath = pickFile(true);
            if (outPath.empty()) return;
        }
        if (toolBusy->exchange(true)) return;
        cancelFlag->store(false);
        ui->set_tool_busy(true); ui->set_tool_progress(0); ui->set_tool_output(S(""));
        ui->set_tool_status(S("…"));
        std::thread([=]() {
            auto last = std::make_shared<std::atomic<long long>>(0);
            auto prog = [=](const Progress& p) {
                long long now = GetTickCount64();
                if (now - last->load() < 100 && p.done != p.total) return;
                last->store(now);
                float f = p.total ? float(double(p.done) / double(p.total)) : 0.f;
                std::string txt = humanSize(p.done) + " / " + humanSize(p.total) + "  " + s.badBlocks + std::to_string(p.badBlocks);
                slint::invoke_from_event_loop([=]() { ui->set_tool_progress(f); ui->set_tool_status(S(txt)); });
            };
            std::string result;
            if (cmd == "hash" || cmd == "image") {
                ImageResult r = cmd == "image" ? createImage(*src, outPath, *cancelFlag, prog) : hashSource(*src, *cancelFlag, prog);
                if (r.ok) result = std::string(s.done) + "\nSHA-256: " + r.sha256 + "\n" + humanSize(r.bytes) + "\n" + s.badBlocks + std::to_string(r.badBlocks) +
                                   (cmd == "image" ? "\n" + outPath : "");
                else if (r.cancelled) result = s.cancelled;
                else result = r.error == "output file already exists" ? s.outExists : "Error: " + r.error;
            } else {
                ScanResult r = surfaceScan(*src, *cancelFlag, prog);
                if (r.cancelled) result = s.cancelled;
                else {
                    result = std::string(s.done) + "\n" + std::to_string(r.blocks) + " blocks (1 MiB)\n" + s.badBlocks + std::to_string(r.badBlocks) +
                             "\n" + std::to_string(int(r.mbPerSec)) + " " + s.speed;
                    for (auto o : r.badOffsets) result += "\nbad offset " + std::to_string(o);
                }
            }
            slint::invoke_from_event_loop([=]() {
                ui->set_tool_busy(false); ui->set_tool_progress(result == s.cancelled ? 0.f : 1.f);
                ui->set_tool_status(S(result.substr(0, result.find('\n'))));
                ui->set_tool_output(S(result));
                toolBusy->store(false);
            });
        }).detach();
    });

    for (int i = 1; i + 1 < argc; ++i)
        if (std::string(argv[i]) == "--tools-file") { *filePath = argv[i + 1]; ui->set_show_tools(true); sourceLabel(); }
    for (int i = 1; i + 1 < argc; ++i)
        if (std::string(argv[i]) == "--tools-run") {
            std::string c = argv[i + 1];
            slint::Timer::single_shot(std::chrono::milliseconds(800), [ui, c]() { ui->invoke_tool_run(S(c), S("512")); });
        }
    refresh();
    ui->run();
    return 0;
}









