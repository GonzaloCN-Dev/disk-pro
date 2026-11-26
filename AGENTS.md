# Disk Pro – decisiones y restricciones

- Autor: Gonzalo Cruz Núñez (GCN Dev, https://dev.gonzalocn.com/). Código propio MIT.
- Clean-room: sin copiar código/recursos de DiskGenius, MiniTool u otros.
- C++20, CMake + Ninja, MSVC (VS 2022 Build Tools). Build: `build.cmd`.
- `src/core`: lógica portable y motores de operación basados en interfaces; APIs Win32 solo en `src/platform/win`.
- Lectura y escritura separadas. Escritura disponible en restauración, clonación, edición de tabla y formato; limitada a VHD <=4 GiB, nunca disco de sistema. No ampliar sin autorización y pruebas específicas.
- Pruebas únicamente con imágenes sintéticas/VHD desechables. Nunca discos reales sin autorización.
- Sin telemetría, sin secretos en logs, sin admin salvo necesidad.
- Publicar solo con autorización del usuario.
- UI: Slint nativo. No Electron/web.

- UI: Slint 1.19.1 (licencia Royalty-free 2.0, atribución AboutSlint obligatoria) en `.tools/slint` (no versionado).
- Pruebas VHD: `tests/vhdx/run_all.ps1`, solo administrador, solo VHD propio en %TEMP%, con guardas (nunca disco de sistema/no virtual).
- Windows en español: no usar nombres de grupo para comprobar admin (usar WindowsBuiltInRole).

- Instalador: `installer/DiskPro.iss` + `installer/build-installer.ps1` (Inno Setup 6.7.x, mismo patrón que db-pro/rclone-lite). Sin firmar.

## Estado
0.7.0: interfaz clara por defecto con accesos a tareas habituales y utilidades técnicas en Herramientas avanzadas. Lectura, inventario Win32 y SMART; restauración, clonación, edición de tabla y formato FAT32/exFAT limitados a VHD <=4 GiB. Búsqueda de particiones perdidas y recuperación limitada de borrados FAT32, ambas sin escribir en el origen; redimensionado/movimiento interactivo solo vista previa. Plan de producto en ROADMAP.md.



