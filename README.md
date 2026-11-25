# Disk Pro

**ES** – Gestor de discos y particiones libre por Gonzalo Cruz Núñez / [GCN Dev](https://dev.gonzalocn.com/). Versión 0.1.0: **solo lectura** (no modifica discos).
**EN** – Free disk and partition manager by Gonzalo Cruz Núñez / GCN Dev. Version 0.1.0: **read-only** (never modifies disks).

## Funciones / Features
- Inventario de discos físicos y virtuales, GPT/MBR, particiones, volúmenes, sistema de archivos, disco de sistema.
- Salud NVMe (SMART) cuando el dispositivo lo expone; si no, "n/d".
- Mapa gráfico de particiones y diagnóstico (solapes, rangos, alineación, CRC GPT).
- Herramientas (solo lectura): SHA-256, crear imagen de disco/archivo, análisis de superficie, visor hex (`diskpro-tools`, y pestaña Herramientas).
- Análisis de imágenes (`diskpro-cli imagen.vhd`), sectores 512/4096.
- Tema claro/oscuro/sistema, español/inglés, densidad, teclado (↑/↓ en la lista), sin telemetría.

## No incluido todavía / Not yet
Crear, redimensionar, mover, borrar particiones; clonar; restaurar imágenes; recuperación; firma de código del instalador; atributos SMART ATA.

## Compilar / Build
Visual Studio 2022 Build Tools (C++, CMake, Ninja) y Slint 1.19.1 C++ en `.tools\slint` (ver `docs/ARCHITECTURE.md`).
```
build.cmd      # compila y ejecuta pruebas
package.cmd    # genera dist\DiskPro-0.1.0-win64-portable.zip
powershell -File installer\build-installer.ps1   # genera dist\DiskPro-Setup-0.1.0.exe (requiere Inno Setup 6)
```
Pruebas con VHD (PowerShell **administrador**; usa solo un VHD desechable en %TEMP%): `powershell -ExecutionPolicy Bypass -File tests\vhdx\run_all.ps1`

## Licencia / License
Código propio MIT (`LICENSE`). Slint se usa bajo su licencia Royalty-free 2.0: ver `THIRD_PARTY_NOTICES.md`. El instalador incluye el runtime de VC++; el portable requiere el VC++ Redistributable x64.


