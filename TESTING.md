# Pruebas

## Suite automática

Comando:

```bat
build.cmd
```

Configura Release con MSVC/Ninja, compila y ejecuta CTest. Suite actual:

- `gui-smoke`: inicia la GUI, completa inventario y cierra.
- `layout`, `tools`, `restore`, `clone`, `partops`, `format`, `recover`, `resize`, `update`, `report`.

Último resultado local (2026-11-26): **17/17 correctas**, incluidas regresiones de cambio de destino, bloqueo previo de volúmenes, creación de un VHDX desechable, interacción básica GUI, validación de argumentos CLI, recuperación FAT32 sin sobrescritura y análisis ATA SMART sintético.

## Análisis estático

CI configura una compilación adicional sin GUI con MSVC `/analyze`. Localmente:

```powershell
cmake -S . -B build-analyze -G Ninja -DCMAKE_BUILD_TYPE=Release -DDISKPRO_BUILD_GUI=OFF -DCMAKE_CXX_FLAGS=/analyze
cmake --build build-analyze --parallel
```

Debe ejecutarse desde un entorno de herramientas de Visual Studio.

## Integración destructiva controlada

Los scripts de `tests/vhdx` requieren elevación y solo crean VHD desechables en `%TEMP%`. Sus guardas rechazan el disco del sistema y dispositivos no virtuales. No se deben ejecutar pruebas destructivas sobre discos físicos.

Pendiente de verificación: conexión/desconexión elevada de VHDX (Windows devolvió `ERROR_PRIVILEGE_NOT_HELD` en sesión no elevada), sectores físicos de 4096 bytes, instalador, actualización completa y automatización real de clics en GUI.

## Distribución

- `package.cmd`: verificado; genera zip portable y `SHA256SUMS.txt`, incluyendo `diskpro-vhd.exe`.
- `installer/build-installer.ps1`: compilación verificada con Inno Setup 6.7.4; genera `DiskPro-Setup-0.6.0.exe`.
- No se instaló ni firmó ningún artefacto durante estas pruebas.
