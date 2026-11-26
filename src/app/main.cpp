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
#include "restore.h"
#include "partops.h"
#include "format.h"
#include "recover.h"
#include "clone.h"
#include "resizeplan.h"
#include "updater.h"
#include "report.h"
#include "virtual_disk.h"
#include <fstream>
#include <filesystem>
#include <ctime>
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
                      "Solo lectura, salvo restaurar, clonar, editar tabla y formatear; solo escriben en VHD permitidos.", "Salud", "Sin incidencias detectadas.", "N.º",
                     "Tipo", "Sistema", "Tamaño", "Inicio en el disco", "Volumen", "Libre",
                     "© Gonzalo Cruz Núñez · GCN Dev. Software libre bajo licencia MIT.", "Selecciona un disco.",
                     {"Sistema", "Claro", "Oscuro"}, {"Compacta", "Normal", "Amplia"}};
const Strings kEn = {"Disk Pro", "Disks", "Partitions", "Diagnostics (read-only)", "Filter disks…", "Refresh", "Theme",
                     "Language", "Density", "About", "Close", "Loading disks…", "No disks found.", "Error: ",
                      "Read-only, except restore, clone, partition-table edits and formatting; writes are limited to eligible VHDs.", "Health", "No issues found.", "No.",
                     "Type", "File system", "Size", "Start on disk", "Volume", "Free",
                     "© Gonzalo Cruz Núñez · GCN Dev. Free software under the MIT license.", "Select a disk.",
                     {"System", "Light", "Dark"}, {"Compact", "Normal", "Comfortable"}};

struct ToolStr {
    const char *tools, *hash, *image, *scan, *hex, *open, *cancel, *prev, *next, *offset, *back, *note, *srcFile, *noSource,
        *cantOpen, *cancelled, *done, *badBlocks, *outExists, *srcDisk, *speed;
};
const ToolStr kToolEs = {"Herramientas", "SHA-256", "Crear imagen…", "Analizar superficie", "Ver hex", "Abrir archivo…", "Cancelar",
                         "◀ Anterior", "Siguiente ▶", "Offset:", "Volver a discos",
                          "Solo lectura salvo operaciones explícitas sobre VHD permitidos (restaurar, clonar, editar tabla o formatear). Los discos físicos requieren ejecutar Disk Pro como administrador; los archivos de imagen no.",
                         "Origen: archivo ", "Selecciona un disco o abre un archivo.",
                         "No se pudo abrir el origen (¿falta ejecutar como administrador?).", "Cancelado.", "Terminado.",
                         "bloques ilegibles: ", "El archivo de salida ya existe; elige otro nombre.", "Origen: disco", "MB/s"};
const ToolStr kToolEn = {"Tools", "SHA-256", "Create image…", "Surface scan", "View hex", "Open file…", "Cancel",
                         "◀ Previous", "Next ▶", "Offset:", "Back to disks",
                          "Read-only except explicit operations on eligible VHDs (restore, clone, edit table or format). Physical disks need Disk Pro to run as administrator; image files do not.",
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
    std::vector<int> healthSeverity;  // 0 normal/unavailable, 1 caution, 2 critical
    std::vector<int> visible;  // indices into disks after filtering
    std::atomic<bool> busy{false};
    std::atomic<unsigned> generation{0};  // cancels stale results
    std::string error;
};

}  // namespace

int main(int argc, char** argv) {
    bool selfTest = argc > 1 && std::string(argv[1]) == "--selftest";
    bool uiSelfTest = argc > 1 && std::string(argv[1]) == "--ui-selftest";
    auto selfTestFailed = std::make_shared<std::atomic<bool>>(false);
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

    // ---- Interactive partition bar (planning only: nothing is ever written from here) ----
    struct PlanState {
        std::vector<PlanPart> orig, cur;
        uint64_t diskSize = 0;
        bool dragging = false;
        int index = 0;
        DragEdge edge = DragEdge::Move;
        uint64_t grab = 0;
        std::vector<std::string> warnings;
    };
    auto plan = std::make_shared<PlanState>();
    auto refreshPlan = [ui, plan, &lang]() {
        auto segs = vec<PlanSeg>();
        const bool en = lang == 1;
        if (plan->diskSize == 0) { ui->set_plan_segs(segs); ui->set_plan_text(S("")); return; }
        auto sorted = plan->cur;
        std::sort(sorted.begin(), sorted.end(), [](auto& a, auto& b) { return a.offset < b.offset; });
        double D = double(plan->diskSize);
        uint64_t pos = 0;
        auto changedOf = [&](int idx) {
            for (auto& o : plan->orig) if (o.index == idx)
                for (auto& c : plan->cur) if (c.index == idx) return o.offset != c.offset || o.length != c.length;
            return false;
        };
        for (auto& p : sorted) {
            if (p.offset > pos) segs->push_back({S(""), S(humanSize(p.offset - pos)), float(pos / D), float((p.offset - pos) / D), true, false});
            segs->push_back({S("#" + std::to_string(p.index)), S(humanSize(p.length)), float(p.offset / D), float(p.length / D), false, changedOf(p.index)});
            pos = p.offset + p.length;
        }
        if (pos < plan->diskSize) segs->push_back({S(""), S(humanSize(plan->diskSize - pos)), float(pos / D), float((plan->diskSize - pos) / D), true, false});
        ui->set_plan_segs(segs);
        std::string txt;
        bool any = false;
        for (auto& p : plan->cur) if (changedOf(p.index)) {
            any = true;
            txt += "#" + std::to_string(p.index) + ": " + humanSize(p.offset) + " + " + humanSize(p.length) + "   ";
        }
        if (!any) txt = en ? "Drag an edge to resize or the body to move. Preview only: nothing is written." : "Arrastra un borde para cambiar el tamaño o el cuerpo para mover. Solo vista previa: no se escribe nada.";
        else txt += en ? "(PREVIEW ONLY, this version does not apply it)" : "(SOLO VISTA PREVIA: esta versión no lo aplica)";
        ui->set_plan_text(S(txt));
    };
    ui->on_plan_hit([plan](float x, float tolFrac) -> int {
        if (plan->diskSize == 0) return 0;
        auto h = hitTest(plan->cur, uint64_t(double(x) * double(plan->diskSize)), uint64_t(double(tolFrac) * double(plan->diskSize)));
        return !h.found ? 0 : h.edge == DragEdge::Move ? 2 : 1;
    });
    ui->on_plan_press([plan](float x, float tolFrac) {
        if (plan->diskSize == 0) return;
        uint64_t pos = uint64_t(double(x) * double(plan->diskSize));
        auto h = hitTest(plan->cur, pos, uint64_t(double(tolFrac) * double(plan->diskSize)));
        if (!h.found) return;
        plan->dragging = true; plan->index = h.index; plan->edge = h.edge;
        plan->grab = 0;
        for (auto& p : plan->cur) if (p.index == h.index) plan->grab = pos > p.offset ? pos - p.offset : 0;
    });
    ui->on_plan_drag([plan, refreshPlan](float x) {
        if (!plan->dragging) return;
        double fx = std::min(1.0, std::max(0.0, double(x)));
        uint64_t pos = uint64_t(fx * double(plan->diskSize));
        // always plan from the original geometry so the result does not depend on the path of the mouse
        auto out = planResize(plan->diskSize, plan->orig, plan->index, plan->edge, pos, plan->grab);
        if (!out.valid) return;
        plan->cur = plan->orig;
        for (auto& p : plan->cur) if (p.index == plan->index) { p.offset = out.offset; p.length = out.length; }
        plan->warnings = out.warnings;
        refreshPlan();
    });
    ui->on_plan_release([plan]() { plan->dragging = false; });
    ui->on_plan_reset([plan, refreshPlan]() { plan->cur = plan->orig; plan->dragging = false; plan->warnings.clear(); refreshPlan(); });
    auto showDisk = [ui, st, selected, plan, refreshPlan](int visIdx) {
        auto parts = vec<PartRow>();
        auto segs = vec<SegRow>();
        auto diags = vec<DiagRow>();
        *selected = visIdx;
        ui->set_selected(visIdx);
        ui->set_part_sel(-1); // never carry a partition action onto another selected disk
        if (visIdx >= 0 && visIdx < int(st->visible.size())) {
            int di = st->visible[visIdx];
            const auto& d = st->disks[di];
            std::vector<Extent> ex;
            for (auto& p : d.partitions) {
                std::string volumeText = p.volume;
                if (!p.volumeLabel.empty()) volumeText = p.volumeLabel + " (" + p.volume + ")";
                if (!p.volume.empty() && p.volumeTotalBytes) {
                    volumeText += " (" + (ui->get_language_index() == 1 ? std::string("free ") : std::string("libre ")) +
                                  humanSize(p.volumeFreeBytes) + " / " + humanSize(p.volumeTotalBytes) + ")";
                }
                parts->push_back({S(std::to_string(p.number)), S(p.typeName.empty() ? "-" : p.typeName), S(p.filesystem),
                                  S(humanSize(p.lengthBytes)), S(humanSize(p.offsetBytes)), S(volumeText)});
                ex.push_back({int(p.number), p.offsetBytes, p.lengthBytes});
            }
            plan->orig.clear(); plan->diskSize = d.sizeBytes; plan->dragging = false; plan->warnings.clear();
            for (auto& p : d.partitions) plan->orig.push_back({int(p.number), p.offsetBytes, p.lengthBytes});
            plan->cur = plan->orig;
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
            if (di < int(st->healthSeverity.size()) && st->healthSeverity[di] > 0) {
                const bool critical = st->healthSeverity[di] == 2;
                diags->push_back({st->healthSeverity[di], S(ui->get_language_index() == 1
                    ? (critical ? "Health alert: back up important data now and review the device." : "Health caution: monitor the device and keep a current backup.")
                    : (critical ? "Alerta de salud: copia ahora los datos importantes y revisa el dispositivo." : "Aviso de salud: vigila el dispositivo y mantén una copia actualizada."))});
            }
            ui->set_health_text(S(st->health[di]));
        }
        if (visIdx < 0 || visIdx >= int(st->visible.size())) { plan->diskSize = 0; plan->orig.clear(); plan->cur.clear(); }
        refreshPlan();
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
            std::string hay = name + d.busType + d.style + d.serialMasked + d.error;
            for (auto& v : d.volumes) hay += v;
            for (auto& p : d.partitions) hay += p.typeName + p.filesystem + p.volume + p.volumeLabel;
            std::transform(hay.begin(), hay.end(), hay.begin(), [](unsigned char c) { return std::tolower(c); });
            if (!f.empty() && hay.find(f) == std::string::npos) continue;
            st->visible.push_back(int(i));
            rows->push_back({S(name), S(d.busType), S(humanSize(d.sizeBytes)), S(d.style.empty() ? "RAW" : d.style),
                             S(d.isSystemDisk ? "SYSTEM" : d.isVirtual ? "virtual" : "")});
        }
        ui->set_disks(rows);
        showDisk(st->visible.empty() ? -1 : 0);
    };

    auto refresh = [ui, st, rebuildList, selfTest, uiSelfTest, selfTestFailed, initialLanguage = lang]() {
        if (st->busy.exchange(true)) return;
        unsigned gen = ++st->generation;
        ui->set_loading(true);
        std::thread([ui, st, rebuildList, gen, selfTest, uiSelfTest, selfTestFailed, initialLanguage]() {
            auto disks = win::enumerateDisks();
            std::vector<std::string> health;
            std::vector<int> healthSeverity;
            for (auto& d : disks) {
                auto h = win::queryHealth(d.number);
                char b[256];
                if (h.nvmeAvailable)
                    std::snprintf(b, sizeof b, "NVMe %d °C, %u%% spare, %u%% used, %llu h, unsafe shutdowns %llu, media errors %llu, warn 0x%02X", h.temperatureC,
                                  h.availableSparePct, h.percentageUsed, (unsigned long long)h.powerOnHours,
                                  (unsigned long long)h.unsafeShutdowns, (unsigned long long)h.mediaErrors, h.criticalWarning);
                else if (h.ataAvailable)
                    std::snprintf(b, sizeof b, "ATA SMART %d °C, %llu h, reallocated %llu, pending %llu, uncorrectable %llu%s", h.temperatureC,
                                  (unsigned long long)h.powerOnHours, (unsigned long long)h.reallocatedSectors,
                                  (unsigned long long)h.pendingSectors, (unsigned long long)h.uncorrectableSectors,
                                  h.predictFailure ? ", FAILURE PREDICTED" : "");
                else if (h.predictFailureAvailable) std::snprintf(b, sizeof b, "SMART: %s", h.predictFailure ? "FAILURE PREDICTED" : "OK");
                else std::snprintf(b, sizeof b, "n/a: %s", h.note.c_str());
                health.push_back(b);
                int severity = 0;
                if (h.predictFailure || h.criticalWarning) severity = 2;
                else if (h.mediaErrors || h.reallocatedSectors || h.pendingSectors || h.uncorrectableSectors ||
                         h.temperatureC >= 70 || h.percentageUsed >= 90 || (h.nvmeAvailable && h.availableSparePct <= 10)) severity = 1;
                healthSeverity.push_back(severity);
            }
            slint::invoke_from_event_loop([=]() {
                if (gen == st->generation.load()) {
                    st->disks = disks; st->health = health; st->healthSeverity = healthSeverity;
                    rebuildList();
                    ui->set_loading(false);
                }
                st->busy = false;
                if (uiSelfTest) {
                    const int alternate = initialLanguage == 0 ? 1 : 0;
                    ui->invoke_language_changed(alternate);
                    ui->invoke_filter_changed(S("__diskpro_no_match__"));
                    ui->invoke_plan_reset();
                    ui->set_vhd_size(S("invalid"));
                    ui->invoke_vhd_create();
                    if (ui->get_language_index() != alternate || std::string(ui->get_tool_status()).empty()) selfTestFailed->store(true);
                    ui->invoke_language_changed(initialLanguage);
                    slint::quit_event_loop();
                } else if (selfTest) slint::quit_event_loop();
            });
        }).detach();
    };

    ui->on_refresh(refresh);
    ui->on_filter_changed([filterText, rebuildList](slint::SharedString t) { *filterText = std::string(t); rebuildList(); });
    auto partRange = std::make_shared<std::pair<uint64_t, uint64_t>>(0, 0);   // length 0 = whole disk
    ui->on_quit([]() { slint::quit_event_loop(); });
    ui->on_part_selected([ui](int i) { ui->set_part_sel(i); });
    ui->on_ctx_action([ui, st, selected, partRange](slint::SharedString a, int pi) {
        std::string act(a);
        ui->set_show_tools(true);
        const int taskMode = act == "scan" ? 1 : act == "recover" ? 2 : act == "files" ? 3 :
                             (act == "p:format" || act == "p:format-exfat") ? 4 :
                             act == "p:create" ? 5 : 0;
        ui->set_task_mode(taskMode);
        if (act.rfind("p:", 0) == 0) { ui->invoke_part_preview(slint::SharedString(act.substr(2)), pi); return; }
        if (act == "clone") return;
        if (act == "restore") { ui->invoke_restore_preview(); return; }
        if (act == "report") { ui->invoke_export_report(); return; }
        if (act == "planreset") { ui->invoke_plan_reset(); ui->set_show_tools(false); return; }
        *partRange = {0, 0};
        if (pi >= 0 && *selected >= 0 && *selected < int(st->visible.size())) {
            const auto& d = st->disks[st->visible[*selected]];
            if (pi < int(d.partitions.size())) *partRange = {d.partitions[pi].offsetBytes, d.partitions[pi].lengthBytes};
        }
        ui->invoke_tool_run(slint::SharedString(act), slint::SharedString(act == "hex" ? "0" : ""));
        *partRange = {0, 0};
    });
    ui->on_theme_changed([ui, settings](int i) { ui->set_theme_index(i); settings->theme = i; settings->save(); });
    ui->on_density_changed([ui, settings](int i) { ui->set_density_index(i); settings->density = i; settings->save(); });
    const ToolStr* ts = lang == 1 ? &kToolEn : &kToolEs;
    auto toolStrings = [ui, &lang]() {
        const ToolStr& s = lang == 1 ? kToolEn : kToolEs;
        auto& g = ui->global<Tr>();
        g.set_tools(S(s.tools)); g.set_t_hash(S(s.hash)); g.set_t_image(S(s.image)); g.set_t_scan(S(s.scan)); g.set_t_recover(S(lang == 1 ? "Find lost partitions" : "Buscar particiones perdidas"));
        g.set_t_files(S(lang == 1 ? "Find deleted FAT32 files" : "Buscar archivos FAT32 borrados"));
        g.set_t_extract(S(lang == 1 ? "Extract candidate…" : "Extraer candidato…"));
        g.set_t_hex(S(s.hex)); g.set_t_open(S(s.open)); g.set_t_cancel(S(s.cancel)); g.set_t_prev(S(s.prev));
        g.set_t_next(S(s.next)); g.set_t_offset(S(s.offset)); g.set_t_back(S(s.back)); g.set_t_ptb(S(lang == 1 ? "Back up partition table…" : "Copia de la tabla de particiones…")); g.set_t_note(S(s.note));
        g.set_t_restore(S(lang == 1 ? "Restore table (VHD only)…" : "Restaurar tabla (solo VHD)…"));
        g.set_act_analyze(S(lang == 1 ? "Analyze" : "Analizar"));
        g.set_act_lost(S(lang == 1 ? "Find partitions" : "Particiones perdidas"));
        g.set_act_files(S(lang == 1 ? "Recover files" : "Recuperar archivos"));
        g.set_act_format(S(lang == 1 ? "Format" : "Formatear"));
        g.set_act_partition(S(lang == 1 ? "Partition" : "Particionar"));
        g.set_advanced(S(lang == 1 ? "Advanced tools" : "Herramientas avanzadas"));
        g.set_t_apply(S(lang == 1 ? "Apply restore" : "Aplicar restauración"));
        g.set_m_file(S(lang == 1 ? "File" : "Archivo")); g.set_m_disk(S(lang == 1 ? "Disk" : "Disco")); g.set_m_view(S(lang == 1 ? "View" : "Ver"));
        g.set_m_help(S(lang == 1 ? "Help" : "Ayuda")); g.set_m_exit(S(lang == 1 ? "Exit" : "Salir"));
        g.set_tb_clone(S(lang == 1 ? "Clone" : "Clonar")); g.set_tb_restore(S(lang == 1 ? "Restore" : "Restaurar"));
        g.set_c_part(S(lang == 1 ? "partition" : "partición")); g.set_c_disk(S(lang == 1 ? "Whole disk" : "Disco completo")); g.set_c_plan(S(lang == 1 ? "Reset plan" : "Restablecer plan"));
        { std::vector<slint::SharedString> fu; for (const char* x : (lang == 1 ? std::vector<const char*>{"Format as FAT32", "Format as exFAT"} : std::vector<const char*>{"Formatear como FAT32", "Formatear como exFAT"})) fu.push_back(S(x)); g.set_c_future(std::make_shared<slint::VectorModel<slint::SharedString>>(fu)); }
        g.set_plan_title(S(lang == 1 ? "Plan (drag edges to resize, the body to move; preview only)" : "Plan (arrastra los bordes para redimensionar, el cuerpo para mover; solo vista previa)"));
        g.set_plan_reset(S(lang == 1 ? "Reset plan" : "Restablecer plan"));
        g.set_t_clone(S(lang == 1 ? "Clone preview" : "Vista previa de clonado"));
        g.set_t_clone_apply(S(lang == 1 ? "Clone" : "Clonar"));
        g.set_t_clone_target(S(lang == 1 ? "Clone to disk #" : "Clonar al disco n.º"));
        g.set_t_vhd_create(S(lang == 1 ? "Create VHDX…" : "Crear VHDX…"));
        g.set_t_vhd_attach(S(lang == 1 ? "Attach read-only…" : "Conectar solo lectura…"));
        g.set_t_vhd_detach(S(lang == 1 ? "Detach…" : "Desconectar…"));
        g.set_t_vhd_size(S(lang == 1 ? "VHDX size (MiB)" : "Tamaño VHDX (MiB)"));
        g.set_t_confirm_hint(S(lang == 1 ? "Confirmation text" : "Texto de confirmación"));
    };
    (void)ts;
    toolStrings();

    auto filePath = std::make_shared<std::string>();
    auto cancelFlag = std::make_shared<std::atomic<bool>>(false);
    auto toolBusy = std::make_shared<std::atomic<bool>>(false);
    struct PendingRecovery { std::shared_ptr<ByteSource> source; std::vector<RecoverableFile> files; };
    auto pendingRecovery = std::make_shared<PendingRecovery>();
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

    struct Pending { BackupData bak; RestorePlan plan; TargetFacts target; int disk = -1; };
    auto pending = std::make_shared<Pending>();
    auto clearPending = [ui, pending]() { *pending = Pending{}; ui->set_restore_ready(false); ui->set_restore_confirm(S("")); };
    ui->on_restore_preview([ui, st, selected, filePath, pending, clearPending, &lang]() {
        const bool en = lang == 1;
        clearPending();
        if (!filePath->empty() || *selected < 0 || *selected >= int(st->visible.size())) {
            ui->set_tool_status(S(en ? "Select a disk in the list first (not a file)." : "Selecciona primero un disco de la lista (no un archivo)."));
            return;
        }
        std::string p = pickFile(false, L"ptb");
        if (p.empty()) return;
        const win::DiskInfo& disk = st->disks[st->visible[*selected]];
        auto bak = loadPartitionBackup(p);
        DeviceSource src{static_cast<int>(disk.number)};
        if (!src.ok()) { ui->set_tool_status(S(en ? "Cannot open the disk (run as administrator)." : "No se puede abrir el disco (ejecuta como administrador).")); return; }
        auto cur = analyze(src);
        TargetFacts t;
        t.number = disk.number; t.size = src.size(); t.sectorSize = disk.logicalSector ? disk.logicalSector : 512;
        t.isSystemDisk = disk.isSystemDisk; t.isVirtual = disk.isVirtual;
        t.currentScheme = schemeName(cur.scheme); t.currentDiskId = cur.diskId;
        auto plan = planRestore(bak, t);
        if (!disk.volumes.empty()) { plan.blockers.push_back("The disk has mounted volumes (drive letters): unmount/clear them first."); plan.allowed = false; plan.ranges.clear(); }
        std::string o = "=== PREVIEW ===\nDisk " + std::to_string(disk.number) + "  " + disk.vendor + " " + disk.product + "  " + disk.busType + "  " + humanSize(t.size) +
                        "\nCurrent: " + t.currentScheme + " (" + std::to_string(disk.partitions.size()) + ")\nBackup: " + bak.scheme + " " + bak.diskId + "\n";
        for (auto& r : plan.ranges) o += "WILL WRITE: offset " + std::to_string(r.offset) + ", " + std::to_string(r.length) + " bytes\n";
        for (auto& w : plan.warnings) o += "WARNING: " + w + "\n";
        for (auto& b : plan.blockers) o += "BLOCKED: " + b + "\n";
        ui->set_tool_output(S(o));
        if (!plan.allowed) { ui->set_tool_status(S(en ? "Not allowed. Nothing will be written." : "No permitido. No se escribirá nada.")); return; }
        pending->bak = std::move(bak); pending->plan = plan; pending->target = t; pending->disk = int(disk.number);
        ui->set_restore_ready(true);
        ui->set_tool_status(S((en ? "Type exactly: " : "Escribe exactamente: ") + plan.token));
    });
    ui->on_restore_apply([ui, st, selected, filePath, pending, clearPending, &lang](slint::SharedString typed) {
        const bool en = lang == 1;
        if (pending->disk < 0) return;
        DeviceSource rb{pending->disk};
        DeviceSink sink{pending->disk, pending->target};
        if (!rb.ok() || !sink.ok()) { ui->set_tool_status(S(en ? "Cannot open the disk for writing (administrator?)." : "No se puede abrir el disco para escribir (¿administrador?).")); return; }
        auto res = applyRestore(pending->bak, pending->plan, std::string(typed), sink, rb);
        sink.refreshLayout();
        std::string msg = res.ok ? (en ? "Restored, flushed and verified by reading back." : "Restaurado, vaciado y verificado releyendo.")
                                 : (en ? "FAILED: " : "ERROR: ") + res.error + (res.written ? (en ? " (the disk may be partially written)" : " (el disco puede haber quedado a medias)") : "");
        ui->set_tool_status(S(msg));
        if (res.ok || res.written) clearPending();
    });
    struct PendingPart { PartOpPlan plan; FormatPlan fplan; TargetFacts target; bool isFormat = false; int disk = -1; uint32_t ss = 512; };
    auto pendingPart = std::make_shared<PendingPart>();
    auto clearPart = [ui, pendingPart]() { *pendingPart = PendingPart{}; ui->set_part_ready(false); ui->set_part_confirm(S("")); };
    ui->on_part_preview([ui, st, selected, filePath, pendingPart, clearPart, &lang](slint::SharedString opS, int pi) {
        const bool en = lang == 1;
        clearPart();
        auto say = [&](const std::string& es, const std::string& e) { ui->set_tool_status(S(en ? e : es)); };
        if (!filePath->empty() || *selected < 0 || *selected >= int(st->visible.size())) { say("Selecciona primero un disco de la lista.", "Select a disk in the list first."); return; }
        const win::DiskInfo& disk = st->disks[st->visible[*selected]];
        DeviceSource src{static_cast<int>(disk.number)};
        if (!src.ok()) { say("No se puede abrir el disco (ejecuta como administrador).", "Cannot open the disk (run as administrator)."); return; }
        TargetFacts t;
        t.number = disk.number; t.size = src.size(); t.sectorSize = disk.logicalSector ? disk.logicalSector : 512;
        t.isSystemDisk = disk.isSystemDisk; t.isVirtual = disk.isVirtual;
        const uint64_t ss = t.sectorSize;
        auto layout = analyze(src, t.sectorSize);
        // GUI row -> table index, matched by start offset (Windows numbering can differ from table order)
        auto tableIndex = [&](int row) {
            if (row < 0 || row >= int(disk.partitions.size())) return 0;
            for (auto& p : layout.partitions) if (p.firstLba * ss == disk.partitions[size_t(row)].offsetBytes) return p.index;
            return 0;
        };
        auto mib = [](const std::string& s, uint64_t& out) { char* e = nullptr; unsigned long long v = std::strtoull(s.c_str(), &e, 10); if (s.empty() || *e) return false; out = v; return true; };
        std::string op(opS);
        const bool isFormat = op == "format" || op == "format-exfat";
        PartOp p;
        using K = PartOp::Kind;
        FormatOp fo;
        if (isFormat) {
            fo.index = tableIndex(pi);
            fo.fs = op == "format-exfat" ? "exfat" : "fat32";
            if (!fo.index) { say("Selecciona una partición de la tabla (clic en una fila).", "Select a partition row first."); return; }
        } else if (op == "initmbr") p.kind = K::InitMbr;
        else if (op == "initgpt") p.kind = K::InitGpt;
        else if (op == "delete" || op == "setactive" || op == "settype" || op == "resize" || op == "merge") {
            p.index = tableIndex(pi);
            if (!p.index) { say("Selecciona una partición de la tabla (clic en una fila).", "Select a partition row first."); return; }
            p.kind = op == "delete" ? K::Delete : op == "setactive" ? K::SetActive : op == "settype" ? K::SetType : op == "resize" ? K::Resize : K::Merge;
            if (op == "merge") {
                p.index2 = tableIndex(pi + 1);
                if (!p.index2) { say("No hay una partición siguiente para fusionar.", "There is no next partition to merge."); return; }
            }
        } else if (op == "create") p.kind = K::Create;
        else return;
        if (p.kind == K::Create || p.kind == K::Resize) {
            uint64_t a = 0, b = 0;
            if (!mib(std::string(ui->get_edit_start()), a) || !mib(std::string(ui->get_edit_size()), b) || b == 0) { say("Rellena Inicio y Tamaño en MiB (números enteros).", "Fill in Start and Size in MiB (whole numbers)."); return; }
            if (a > (1ull << 40) || b > (1ull << 40)) { say("Valores demasiado grandes.", "Values too large."); return; }
            p.firstLba = a * 1048576ull / ss; p.sectorCount = b * 1048576ull / ss;
        }
        if (p.kind == K::Create || p.kind == K::SetType) p.type = std::string(ui->get_edit_type());
        PartOpPlan plan; FormatPlan fplan;
        if (isFormat) {
            fplan = planFormat(src, t, fo);
            plan.allowed = fplan.allowed; plan.blockers = fplan.blockers; plan.warnings = fplan.warnings; plan.writes = fplan.writes; plan.token = fplan.token; plan.summary = fplan.summary;
        } else plan = planPartOp(src, t, p);
        if (!isFormat && !disk.volumes.empty()) { plan.blockers.push_back("The disk has mounted volumes (drive letters): unmount/clear them first."); plan.allowed = false; }
        std::string o = "=== PREVIEW ===\nDisk " + std::to_string(disk.number) + "  " + disk.vendor + " " + disk.product + "  " + disk.busType + "  " + humanSize(t.size) + "\n";
        if (plan.allowed) { o += "OPERATION: " + plan.summary + "\n"; for (auto& w : plan.writes) o += "WILL WRITE: offset " + std::to_string(w.offset) + ", " + std::to_string(w.data.size()) + " bytes\n"; }
        for (auto& w : plan.warnings) o += "WARNING: " + w + "\n";
        for (auto& b : plan.blockers) o += "BLOCKED: " + b + "\n";
        ui->set_tool_output(S(o));
        if (!plan.allowed) { say("No permitido. No se escribirá nada.", "Not allowed. Nothing will be written."); return; }
        pendingPart->plan = plan; pendingPart->fplan = fplan; pendingPart->target = t; pendingPart->isFormat = isFormat; pendingPart->disk = int(disk.number); pendingPart->ss = t.sectorSize;
        ui->set_part_ready(true);
        ui->set_tool_status(S((en ? "Type exactly: " : "Escribe exactamente: ") + plan.token));
    });
    ui->on_part_apply([ui, pendingPart, clearPart, &lang](slint::SharedString typed) {
        const bool en = lang == 1;
        if (pendingPart->disk < 0) return;
        DeviceSource rb{pendingPart->disk};
        DeviceSink sink{pendingPart->disk, pendingPart->target};
        if (!rb.ok() || !sink.ok()) { ui->set_tool_status(S(en ? "Cannot open the disk for writing (administrator?)." : "No se puede abrir el disco para escribir (¿administrador?).")); return; }
        auto res = pendingPart->isFormat ? applyFormat(pendingPart->fplan, std::string(typed), sink, rb) : applyPartOp(pendingPart->plan, std::string(typed), sink, rb, pendingPart->ss);
        sink.refreshLayout();
        std::string msg = res.ok ? (en ? "Done: written, flushed and verified by reading back. Press Refresh." : "Hecho: escrito, vaciado y verificado releyendo. Pulsa Actualizar.")
                                 : (en ? "FAILED: " : "ERROR: ") + res.error + (res.written ? (en ? " (the table may be partially written)" : " (la tabla puede haber quedado a medias)") : "");
        ui->set_tool_status(S(msg));
        if (res.ok || res.written) clearPart();
    });
    struct PendingClone { ClonePlan plan; TargetFacts targetFacts; std::shared_ptr<ByteSource> src; int target = -1; };
    auto pendingClone = std::make_shared<PendingClone>();
    auto clearClone = [ui, pendingClone]() { *pendingClone = PendingClone{}; ui->set_clone_ready(false); ui->set_clone_confirm(S("")); };
    ui->on_clone_preview([ui, st, selected, filePath, pendingClone, clearClone, toolBusy, &lang](slint::SharedString targetS) {
        const bool en = lang == 1;
        clearClone();
        if (toolBusy->load()) return;
        std::string ts(targetS);
        char* e = nullptr;
        long tn = std::strtol(ts.c_str(), &e, 10);
        if (ts.empty() || *e || tn < 0) { ui->set_tool_status(S(en ? "Type the target disk number." : "Escribe el número del disco de destino.")); return; }
        const win::DiskInfo* target = nullptr;
        for (auto& d : st->disks) if (d.number == uint32_t(tn)) target = &d;
        if (!target) { ui->set_tool_status(S(en ? "Target disk not found." : "No se encuentra el disco de destino.")); return; }
        std::shared_ptr<ByteSource> src;
        CloneSourceFacts sf;
        if (!filePath->empty()) {
            auto f = std::make_shared<FileSource>(*filePath);
            if (!f->ok()) { ui->set_tool_status(S(en ? "Cannot open the image." : "No se puede abrir la imagen.")); return; }
            sf.label = *filePath; sf.sectorSize = target->logicalSector ? target->logicalSector : 512; src = f;
        } else if (*selected >= 0 && *selected < int(st->visible.size())) {
            const win::DiskInfo& sd = st->disks[st->visible[*selected]];
            auto d = std::make_shared<DeviceSource>(sd.number);
            if (!d->ok()) { ui->set_tool_status(S(en ? "Cannot open the source disk (administrator?)." : "No se puede abrir el disco de origen (¿administrador?).")); return; }
            sf.label = "Disk " + std::to_string(sd.number); sf.sectorSize = sd.logicalSector ? sd.logicalSector : 512;
            sf.isTargetItself = sd.number == target->number; src = d;
        } else { ui->set_tool_status(S(en ? "Select a source disk in the list (or open an image)." : "Selecciona un disco de origen en la lista (o abre una imagen).")); return; }
        sf.size = src->size();
        DeviceSource back{static_cast<int>(target->number)};
        if (!back.ok()) { ui->set_tool_status(S(en ? "Cannot open the target disk (administrator?)." : "No se puede abrir el disco de destino (¿administrador?).")); return; }
        TargetFacts t;
        t.number = target->number; t.size = back.size(); t.sectorSize = target->logicalSector ? target->logicalSector : 512;
        t.isSystemDisk = target->isSystemDisk; t.isVirtual = target->isVirtual;
        auto plan = planClone(sf, t);
        if (!target->volumes.empty()) { plan.blockers.push_back("The target has mounted volumes (drive letters): unmount/clear them first."); plan.allowed = false; plan.bytes = 0; }
        std::string o = "=== PREVIEW ===\nSource: " + sf.label + "  " + humanSize(sf.size) + "\nTarget: disk " + std::to_string(target->number) + "  " + target->vendor + " " +
                        target->product + "  " + target->busType + "  " + humanSize(t.size) + "\n";
        if (plan.allowed) o += "WILL WRITE: offset 0, " + std::to_string(plan.bytes) + " bytes\n";
        for (auto& w : plan.warnings) o += "WARNING: " + w + "\n";
        for (auto& b : plan.blockers) o += "BLOCKED: " + b + "\n";
        ui->set_tool_output(S(o));
        if (!plan.allowed) { ui->set_tool_status(S(en ? "Not allowed. Nothing will be written." : "No permitido. No se escribirá nada.")); return; }
        pendingClone->plan = plan; pendingClone->targetFacts = t; pendingClone->src = src; pendingClone->target = int(target->number);
        ui->set_clone_ready(true);
        ui->set_tool_status(S((en ? "Type exactly: " : "Escribe exactamente: ") + plan.token));
    });
    ui->on_clone_apply([ui, pendingClone, clearClone, cancelFlag, toolBusy, &lang](slint::SharedString typed) {
        const bool en = lang == 1;
        if (pendingClone->target < 0 || toolBusy->load()) return;
        if (std::string(typed) != pendingClone->plan.token) { ui->set_tool_status(S(en ? "The confirmation text does not match. Nothing written." : "El texto de confirmación no coincide. No se ha escrito nada.")); return; }
        auto pc = std::make_shared<PendingClone>(*pendingClone);
        std::string tok(typed);
        if (toolBusy->exchange(true)) return;
        cancelFlag->store(false);
        clearClone();
        ui->set_tool_busy(true); ui->set_tool_progress(0); ui->set_tool_output(S(""));
        ui->set_tool_status(S("."));
        std::thread([=]() {
            auto last = std::make_shared<std::atomic<long long>>(0);
            DeviceSource back{pc->target};
            DeviceSink sink{pc->target, pc->targetFacts};
            CloneResult res;
            if (!back.ok() || !sink.ok()) res.error = en ? "cannot open the target for writing (administrator?)" : "no se puede abrir el destino para escribir (¿administrador?)";
            else res = applyClone(*pc->src, sink, back, pc->plan, tok, [&](int ph, uint64_t d, uint64_t tot) {
                long long now = GetTickCount64();
                if (now - last->load() >= 100 || d == tot) {
                    last->store(now);
                    float f = tot ? float(double(d) / double(tot)) : 0.f;
                    std::string txt = std::string(ph == 0 ? (en ? "Copying " : "Copiando ") : (en ? "Verifying " : "Verificando ")) + humanSize(d) + " / " + humanSize(tot) + "  (" + std::to_string(int(f * 100)) + " %)";
                    slint::invoke_from_event_loop([=]() { ui->set_tool_progress(f); ui->set_tool_status(S(txt)); });
                }
                return !cancelFlag->load();
            });
            if (back.ok() && sink.ok()) sink.refreshLayout();
            std::string msg = res.ok ? (en ? "Cloned, flushed and verified by reading back: " : "Clonado, vaciado y verificado releyendo: ") + humanSize(res.copied)
                              : res.cancelled ? (en ? "Cancelled. The target may be partially written." : "Cancelado. El destino puede haber quedado a medias.")
                              : (en ? "FAILED: " : "ERROR: ") + res.error + (res.written ? (en ? " (the target may be partially written)" : " (el destino puede haber quedado a medias)") : "");
            slint::invoke_from_event_loop([=]() {
                ui->set_tool_busy(false); ui->set_tool_progress(res.ok ? 1.f : 0.f);
                ui->set_tool_status(S(msg)); ui->set_tool_output(S(msg));
                toolBusy->store(false);
            });
        }).detach();
    });
    auto runVirtualDisk = [ui, toolBusy, refresh, &lang](const std::string& op, const std::string& path, uint64_t size) {
        const bool en = lang == 1;
        if (toolBusy->exchange(true)) return;
        ui->set_tool_busy(true);
        ui->set_tool_status(S(en ? "Working…" : "Procesando…"));
        std::thread([=]() {
            win::VirtualDiskResult result;
            if (op == "create") result = win::createVirtualDisk(path, size, true);
            else if (op == "attach") result = win::attachVirtualDisk(path, true);
            else result = win::detachVirtualDisk(path);
            const std::string message = result.ok ? (en ? "Virtual-disk operation completed." : "Operación de disco virtual completada.")
                                                  : (en ? "FAILED: " : "ERROR: ") + result.error;
            slint::invoke_from_event_loop([=]() {
                ui->set_tool_busy(false);
                ui->set_tool_status(S(message));
                ui->set_tool_output(S(message));
                toolBusy->store(false);
                if (result.ok && op != "create") refresh();
            });
        }).detach();
    };
    ui->on_vhd_create([ui, runVirtualDisk, &lang]() {
        const std::string text(ui->get_vhd_size());
        char* end = nullptr;
        const unsigned long long mib = std::strtoull(text.c_str(), &end, 10);
        if (text.empty() || !end || *end || mib < 3 || mib > (2ull << 20)) {
            ui->set_tool_status(S(lang == 1 ? "Enter a size from 3 to 2097152 MiB." : "Introduce un tamaño de 3 a 2097152 MiB."));
            return;
        }
        const auto path = pickFile(true, L"vhdx");
        if (!path.empty()) runVirtualDisk("create", path, mib << 20);
    });
    ui->on_vhd_attach([runVirtualDisk]() {
        const auto path = pickFile(false);
        if (!path.empty()) runVirtualDisk("attach", path, 0);
    });
    ui->on_vhd_detach([runVirtualDisk]() {
        const auto path = pickFile(false);
        if (!path.empty()) runVirtualDisk("detach", path, 0);
    });
    ui->on_tool_run([ui, st, selected, filePath, cancelFlag, toolBusy, partRange, pendingRecovery, &lang](slint::SharedString cmdS, slint::SharedString arg) {
        const ToolStr& s = lang == 1 ? kToolEn : kToolEs;
        std::string cmd(cmdS), a(arg);
        std::shared_ptr<ByteSource> src;
        size_t recoveryIndex = 0;
        std::string outPath;
        if (cmd == "extract") {
            char* end = nullptr;
            unsigned long long n = std::strtoull(a.c_str(), &end, 10);
            if (a.empty() || *end || n < 1 || n > pendingRecovery->files.size() || !pendingRecovery->source) {
                ui->set_tool_status(S(lang == 1 ? "Run deleted-file search, then enter a valid candidate number." : "Busca archivos borrados y escribe un número de candidato válido."));
                return;
            }
            recoveryIndex = size_t(n - 1);
            outPath = pickFile(true);
            if (outPath.empty()) return;
            src = pendingRecovery->source;
        } else if (!filePath->empty()) {
            auto f = std::make_shared<FileSource>(*filePath);
            if (f->ok()) {
                src = f;
                if (cmd == "files") {
                    auto layout = analyze(*f);
                    std::vector<Partition> fat32;
                    for (const auto& p : layout.partitions) if (p.filesystem == "FAT32") fat32.push_back(p);
                    if (fat32.size() == 1)
                        src = std::make_shared<RangeSource>(f, fat32[0].firstLba * layout.sectorSize,
                                                            fat32[0].sectorCount * layout.sectorSize);
                }
            }
        } else if (*selected >= 0 && *selected < int(st->visible.size())) {
            const auto& disk = st->disks[st->visible[*selected]];
            auto d = std::make_shared<DeviceSource>(disk.number);
            if (d->ok()) {
                auto range = *partRange;
                const int row = ui->get_part_sel();
                if (cmd == "files" && !range.second && row >= 0 && row < int(disk.partitions.size()))
                    range = {disk.partitions[size_t(row)].offsetBytes, disk.partitions[size_t(row)].lengthBytes};
                if (range.second) src = std::make_shared<RangeSource>(d, range.first, range.second);
                else src = d;
            }
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
        if (cmd == "image") {
            outPath = pickFile(true);
            if (outPath.empty()) return;
        }
        uint32_t recSs = 512;
        if (filePath->empty() && *selected >= 0 && *selected < int(st->visible.size())) { auto ls = st->disks[st->visible[*selected]].logicalSector; if (ls == 4096) recSs = 4096; }
        if (toolBusy->exchange(true)) return;
        if (cmd == "files") *pendingRecovery = PendingRecovery{};
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
            } else if (cmd == "recover") {
                RecoverResult r = findLostPartitions(*src, recSs, *cancelFlag, prog);
                if (r.cancelled) result = s.cancelled;
                else if (!r.ok) result = "Error: " + r.error;
                else {
                    result = std::string(s.done) + "\nFound: " + std::to_string(r.found.size()) + "  (read-only; nothing was written)";
                    for (auto& f : r.found)
                        result += "\n" + f.filesystem + "  start LBA " + std::to_string(f.firstLba) + "  sectors " + std::to_string(f.sectorCount) + "  " +
                                  humanSize(f.sectorCount * recSs) + "  score " + std::to_string(f.confidence) + "/100 [" + f.evidence + "]" +
                                   (f.overlapsExisting ? "  (already in the table)" : "  (LOST: create it with Create partition using these values)");
                }
            } else if (cmd == "files") {
                FileScanResult r = findDeletedFat32Files(*src, *cancelFlag, prog);
                if (r.cancelled) result = s.cancelled;
                else if (!r.ok) result = "Error: " + r.error;
                else {
                    pendingRecovery->source = src;
                    pendingRecovery->files = r.files;
                    result = std::string(s.done) + "\nFound: " + std::to_string(r.files.size()) + "  (read-only; source was not modified)";
                    for (size_t i = 0; i < r.files.size(); ++i) {
                        const auto& f = r.files[i];
                        result += "\n" + std::to_string(i + 1) + "  " + f.path + "  " + humanSize(f.size) +
                                  "  score " + std::to_string(f.confidence) + "/100 [" + f.evidence + "]";
                    }
                }
            } else if (cmd == "extract") {
                const auto file = pendingRecovery->files[recoveryIndex];
                std::string outputGuardError;
                if (src->backingDiskNumber() >= 0 &&
                    !pathOnPhysicalDisk(outPath, src->backingDiskNumber(), outputGuardError)) {
                    result = "Error: " + outputGuardError;
                } else {
                    FileRecoverResult r = recoverFat32FileToNewFile(*src, file, outPath, *cancelFlag, prog);
                    if (r.cancelled) result = s.cancelled;
                    else if (!r.ok) result = "Error: " + r.error;
                    else result = std::string(s.done) + "\n" + file.path + "\n" + outPath + "\nSHA-256: " + r.sha256 + "\n" + humanSize(r.bytes);
                }
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

    struct UpdStr { const char *settings, *check, *install, *autoCk, *privacy, *notes, *checking, *upToDate, *available, *downloading, *verifying, *starting, *failed, *hashBad, *instFail, *checkFail; };
    static const UpdStr kUpdEs = {"Ajustes", "Buscar actualizaciones", "Descargar e instalar", "Buscar actualizaciones al iniciar (una vez al día)",
                                  "Desactivado por defecto. Solo se hace una petición HTTPS a github.com; no se envía ningún dato tuyo. El instalador descargado se verifica con SHA-256 antes de ejecutarse.",
                                  "Novedades", "Buscando…", "Estás al día (versión ", "Hay una versión nueva: ", "Descargando ", "Verificando SHA-256…",
                                  "Iniciando el instalador; Disk Pro se cerrará.", "Error de descarga: ", "La descarga no coincide con el SHA-256 publicado y se ha borrado.",
                                  "No se pudo iniciar el instalador.", "No se pudo comprobar: "};
    static const UpdStr kUpdEn = {"Settings", "Check for updates", "Download and install", "Check for updates on start (once a day)",
                                  "Off by default. Only one HTTPS request to github.com is made; none of your data is sent. The downloaded installer is verified with SHA-256 before it runs.",
                                  "What's new", "Checking…", "You are up to date (version ", "A new version is available: ", "Downloading ", "Verifying SHA-256…",
                                  "Starting the installer; Disk Pro will close.", "Download error: ", "The download does not match the published SHA-256 and was deleted.",
                                  "Could not start the installer.", "Could not check: "};
    auto updStrings = [ui, &lang]() {
        const UpdStr& u = lang == 1 ? kUpdEn : kUpdEs;
        auto& g = ui->global<Tr>();
        g.set_version(S(DISKPRO_VERSION)); g.set_settings(S(u.settings)); g.set_s_check(S(u.check)); g.set_s_install(S(u.install));
        g.set_s_auto(S(u.autoCk)); g.set_s_privacy(S(u.privacy)); g.set_s_notes(S(u.notes)); g.set_report(S(lang == 1 ? "Export report…" : "Exportar informe…"));
    };
    updStrings();
    ui->set_auto_update(settings->autoUpdate);

    struct Upd { update::Release rel; int state = 0; };  // 0 idle, 1 checking, 2 up to date, 3 available, 4 downloading, 5 starting, 6 error
    auto upd = std::make_shared<Upd>();
    auto setUpd = [ui, upd](int state, const std::string& text) {
        upd->state = state;
        ui->set_upd_text(S(text));
        ui->set_upd_busy(state == 1 || state == 4 || state == 5);
        ui->set_upd_can_install(state == 3);
    };
    auto checkUpdates = [ui, settings, upd, setUpd, &lang](bool silent) {
        if (upd->state == 1 || upd->state == 4 || upd->state == 5) return;
        const UpdStr& u = lang == 1 ? kUpdEn : kUpdEs;
        if (!silent) setUpd(1, u.checking);
        settings->lastUpdateCheck = (long long)std::time(nullptr);
        settings->save();
        std::thread([ui, upd, setUpd, silent, &lang]() {
            auto r = win::fetchLatestRelease();
            slint::invoke_from_event_loop([=, &lang]() {
                const UpdStr& u = lang == 1 ? kUpdEn : kUpdEs;
                if (upd->state == 4 || upd->state == 5) return;
                if (!r.ok) { if (!silent) setUpd(6, std::string(u.checkFail) + r.error); return; }
                if (update::isNewer(r.version, DISKPRO_VERSION)) {
                    upd->rel = r;
                    ui->set_upd_notes(S(r.notes));
                    setUpd(3, std::string(u.available) + r.version);
                } else if (!silent) setUpd(2, std::string(u.upToDate) + DISKPRO_VERSION + ")");
            });
        }).detach();
    };
    ui->on_check_updates([checkUpdates]() { checkUpdates(false); });
    ui->on_auto_update_changed([ui, settings](bool on) { settings->autoUpdate = on; settings->save(); ui->set_auto_update(on); });
    ui->on_start_update([ui, upd, setUpd, toolBusy, &lang]() {
        if (upd->state != 3 || upd->rel.installerUrl.empty()) return;
        const UpdStr& u = lang == 1 ? kUpdEn : kUpdEs;
        if (toolBusy->load()) { setUpd(6, lang == 1 ? "A tool is running. Wait for it to finish or cancel it first." : "Hay una herramienta en marcha. Espera a que termine o cancélala."); return; }
        auto rel = upd->rel;
        setUpd(4, std::string(u.downloading) + rel.version + " 0 %");
        ui->set_upd_progress(0);
        std::thread([=, &lang]() {
            std::string file = win::updateDir() + "\\" + rel.installerName;
            auto lastTick = std::make_shared<std::atomic<long long>>(0);
            std::string e = win::downloadAsset(rel.installerUrl, file, [=, &lang](int64_t done, int64_t total) {
                long long now = (long long)GetTickCount64();
                if (now - lastTick->load() < 150) return;
                lastTick->store(now);
                float f = total > 0 ? float(double(done) / double(total)) : 0.f;
                std::string pct = total > 0 ? std::to_string(int(f * 100)) + " %" : humanSize(uint64_t(done));
                slint::invoke_from_event_loop([=, &lang]() {
                    if (upd->state != 4) return;
                    ui->set_upd_progress(f);
                    setUpd(4, std::string((lang == 1 ? kUpdEn : kUpdEs).downloading) + rel.version + " " + pct);
                });
            }, nullptr);
            bool bad = false;
            if (e.empty()) {
                slint::invoke_from_event_loop([=, &lang]() { setUpd(4, (lang == 1 ? kUpdEn : kUpdEs).verifying); });
                bad = !win::verifyDownloaded(file, rel.sha256);
            }
            slint::invoke_from_event_loop([=, &lang]() {
                const UpdStr& u = lang == 1 ? kUpdEn : kUpdEs;
                if (!e.empty()) { setUpd(6, std::string(u.failed) + e); return; }
                if (bad) { setUpd(6, u.hashBad); return; }
                setUpd(5, u.starting);
                if (!win::runInstaller(file)) { setUpd(6, u.instFail); return; }
                slint::Timer::single_shot(std::chrono::milliseconds(600), []() { slint::quit_event_loop(); });
            });
        }).detach();
    });
    ui->on_export_report([ui, st, &lang]() {
        const bool en = lang == 1;
        std::string p = pickFile(true, L"txt");
        if (p.empty()) return;
        if (p.find('.', p.find_last_of("\\/") == std::string::npos ? 0 : p.find_last_of("\\/")) == std::string::npos) p += ".txt";
        std::error_code ec;
        if (std::filesystem::exists(std::filesystem::u8path(p), ec)) { ui->set_report_status(S(en ? "The file already exists; nothing was overwritten." : "El archivo ya existe; no se ha sobrescrito nada.")); return; }
        std::string text = win::buildReport(st->disks, st->health, DISKPRO_VERSION);
        std::ofstream f(std::filesystem::u8path(p), std::ios::binary | std::ios::trunc);
        f.write(text.data(), std::streamsize(text.size()));
        f.close();
        ui->set_report_status(S(f ? (en ? "Report saved: " : "Informe guardado: ") + p : std::string(en ? "Could not write the report." : "No se pudo escribir el informe.")));
    });
    ui->on_language_changed([ui, settings, applyStrings, toolStrings, updStrings, &lang, rebuildList](int i) {
        lang = i; settings->language = i; settings->save();
        ui->set_language_index(i);
        applyStrings();
        toolStrings();
        updStrings();
        rebuildList();
    });
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--settings-check") {
            ui->set_show_settings(true);
            slint::Timer::single_shot(std::chrono::milliseconds(800), [ui]() { ui->invoke_check_updates(); });
        }
    if (settings->autoUpdate && !selfTest && !uiSelfTest) {
        long long now = (long long)std::time(nullptr);
        if (now < settings->lastUpdateCheck || now - settings->lastUpdateCheck >= 24 * 3600)
            slint::Timer::single_shot(std::chrono::milliseconds(1500), [checkUpdates]() { checkUpdates(true); });
    }
    for (int i = 1; i + 1 < argc; ++i)
        if (std::string(argv[i]) == "--tools-file") { *filePath = argv[i + 1]; ui->set_show_tools(true); sourceLabel(); }
    for (int i = 1; i + 1 < argc; ++i)
        if (std::string(argv[i]) == "--tools-run") {
            std::string c = argv[i + 1];
            slint::Timer::single_shot(std::chrono::milliseconds(800), [ui, c]() { ui->invoke_tool_run(S(c), S("512")); });
        }
    for (int i = 1; i + 1 < argc; ++i)
        if (std::string(argv[i]) == "--export-report") {
            std::string out = argv[i + 1];
            slint::Timer::single_shot(std::chrono::milliseconds(4000), [st, out]() {
                std::string text = win::buildReport(st->disks, st->health, DISKPRO_VERSION);
                std::ofstream f(std::filesystem::u8path(out), std::ios::binary | std::ios::trunc);
                f.write(text.data(), std::streamsize(text.size()));
                slint::quit_event_loop();
            });
        }
    refresh();
    ui->run();
    return selfTestFailed->load() ? 1 : 0;
}
