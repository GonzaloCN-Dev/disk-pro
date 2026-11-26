# Disk Pro – Plan de trabajo

Reglas fijas: clean-room, escritura solo en VHD ≤4 GiB (nunca disco de sistema), pruebas solo con imágenes sintéticas o VHD desechables, nada se marca como hecho sin compilar y probar, publicar solo con autorización.

Cada fase termina con: compila, 100 % de tests, prueba de integración en VHD cuando escriba, README/CHANGELOG actualizados y lista honesta de «no verificado».

## Fase 0 – Cerrar 0.4.0
- [ ] Probar arrastre del plan y menú contextual (lo hace el usuario).
- [ ] Regenerar zip e instalador, SHA256SUMS.
- [ ] Publicar v0.4.0 (requiere autorización).

## Fase 1 – Motor de escritura de particiones (src/core/partops.*)
Una sola base común: plan → vista previa → confirmación escrita → aplicar → verificar leyendo de vuelta. Mismas guardas que restore/clone (virtual, ≤4 GiB, no sistema, sin volúmenes montados).
- [x] 1.1 Crear tabla nueva (MBR / GPT) en disco vacío.
- [x] 1.2 Crear partición (en espacio libre, alineada a 1 MiB).
- [x] 1.3 Borrar partición.
- [x] 1.4 Cambiar tipo/etiqueta/activa.
- [x] 1.5 Redimensionar/mover solo la entrada de tabla (sin tocar el sistema de archivos; aviso explícito).
- [x] 1.6 Fusionar particiones contiguas (solo tabla, con aviso de pérdida del sistema de archivos).
- Hecho y probado: `src/core/partops.*`, CLI `diskpro-part`, `tests/test_partops.cpp` (MBR/GPT, 512/4096, en memoria) y `tests/vhdx/run_part_all.ps1` (VHD real, Windows valida la geometría: PASS 14/14). Solo particiones primarias MBR.

## Fase 2 – Formatear
- [x] 2.1 Formateador FAT32 propio (`src/core/format.*`; probado en memoria 512/4096 y en VHD: Windows lo monta, escribe/lee un archivo y `chkdsk` sin problemas).
- [x] 2.2 exFAT (`src/core/format.*`; Windows lo monta, escribe/lee y `chkdsk` sin problemas).
- [ ] 2.3 NTFS: pendiente de decidir (escribir NTFS propio es grande; alternativa documentada: no ofrecerlo).
- Solo VHD; confirmación escrita.

## Fase 3 – Integración en la GUI
- [x] Menú contextual y campos de Herramientas activan las operaciones de tabla (compila; la GUI aún sin probar con ratón/VHD). «Formatear» sigue desactivado.
- [ ] Vista previa del plan, confirmación escrita, barra de progreso, cancelar.
- [ ] Barra de «Guardar cambios» con cola de operaciones pendientes (aplicar/descartar).
- [ ] Prueba de GUI sobre VHD.

## Fase 4 – Recuperación y copias
- [ ] Restaurar imagen a VHD.
- [ ] Recuperación de particiones perdidas (búsqueda de firmas MBR/GPT/NTFS/FAT en la superficie).
- [ ] Recuperación de archivos FAT/NTFS borrados (solo lectura, destino en otra unidad).

## Fase 5 – Discos virtuales y diagnóstico
- [ ] Crear/montar/desmontar VHD/VHDX con la API de Windows.
- [ ] Centro SMART: NVMe ya hecho; ATA SMART solo si puede probarse.
- [ ] Editor hexadecimal con edición (solo en imagen/VHD).

## Fase 6 – Calidad y documentación
- [ ] SECURITY.md, SECURITY_AUDIT.md, FEATURE_MATRIX.md, BENCHMARKS.md, PROGRESS.md, THIRD_PARTY y SBOM.
- [ ] ASan, fuzzing de parsers, clang-tidy.
- [ ] Pruebas con sectores de 4096 y VHDX.
- [ ] CI en GitHub (requiere `gh auth refresh -s workflow`).
- [ ] Probar el instalador (instalar/desinstalar) y la actualización 0.3.0 → 0.3.1.

## Versiones previstas
- 0.4.0: lo actual.
- 0.5.0: Fases 1 y 3 (crear/borrar/tipo/redimensionar tabla).
- 0.6.0: Fase 2 (formatear).
- 0.7.0: Fase 4.
- 0.8.0: Fase 5.
- 1.0.0: Fase 6 completa.

## Fuera de alcance (declarado)
Escribir en discos físicos, mover datos al redimensionar un sistema de archivos, convertir MBR↔GPT con datos. Solo si el usuario lo autoriza, y nunca sin pruebas.
