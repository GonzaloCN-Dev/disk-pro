# Disk Pro – decisiones y restricciones

- Autor: Gonzalo Cruz Núñez (GCN Dev, https://dev.gonzalocn.com/). Código propio MIT.
- Clean-room: sin copiar código/recursos de DiskGenius, MiniTool u otros.
- C++20, CMake + Ninja, MSVC (VS 2022 Build Tools). Build: `build.cmd`.
- `src/core`: lógica portable de solo lectura (sin APIs de plataforma). Plataforma Win32 irá aparte (`src/platform/win`).
- Lectura y escritura separadas. Escritura solo en restore.* y solo VHD <=4 GiB, nunca disco de sistema.
- Pruebas únicamente con imágenes sintéticas/VHD desechables. Nunca discos reales sin autorización.
- Sin telemetría, sin secretos en logs, sin admin salvo necesidad.
- Publicar solo con autorización del usuario.
- UI: Slint nativo (pendiente). No Electron/web.

- UI: Slint 1.19.1 (licencia Royalty-free 2.0, atribución AboutSlint obligatoria) en `.tools/slint` (no versionado).
- Pruebas VHD: `tests/vhdx/run_all.ps1`, solo administrador, solo VHD propio en %TEMP%, con guardas (nunca disco de sistema/no virtual).
- Windows en español: no usar nombres de grupo para comprobar admin (usar WindowsBuiltInRole).

- Instalador: `installer/DiskPro.iss` + `installer/build-installer.ps1` (Inno Setup 6.7.x, mismo patrón que db-pro/rclone-lite). Sin firmar.

## Estado
0.2.1: lectura + restaurar tabla de particiones solo en VHD (src/core/restore.*, diskpro-restore, tests/vhdx/run_restore_all.ps1). Además: analizador, inventario Win32, SMART NVMe, GUI Slint, CLI, pruebas, portable. Pendiente: ver README/CHANGELOG.



