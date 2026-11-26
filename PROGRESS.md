# Progreso de desarrollo

Estado actualizado: 2026-11-26.

## Completado y verificado

- Analizador MBR/GPT, mapa, diagnóstico, inventario Win32 y salud NVMe/predicción genérica.
- GUI Slint ES/EN, herramientas de lectura, informe, ajustes y actualización.
- Restauración de tabla, clonación, edición MBR/GPT y formato FAT32/exFAT con escritura limitada a VHD elegibles de hasta 4 GiB.
- Búsqueda de particiones perdidas NTFS/FAT32/exFAT, solo lectura.
- Matriz de compatibilidad, workflow CI de Windows, compilación con `/analyze` y prueba de humo de la GUI.
- Inventario enriquecido con etiqueta y capacidad libre/total de volúmenes montados.
- Revalidación de tamaño y sector entre vista previa y aplicación. `DeviceSink` vuelve a comprobar identidad lógica, tamaño, sector, bus virtual y exclusión del disco del sistema antes de abrir escritura.
- Restauración, clonación y edición de tabla bloquean y desmontan todos los volúmenes afectados antes de escribir, incluso si no tienen letra.
- Creación de VHD/VHDX y conexión/desconexión mediante API nativa, disponibles en CLI y GUI; conexión solo lectura por defecto.
- Los candidatos de particiones perdidas muestran una puntuación estructural reproducible y sus evidencias.
- La GUI ofrece FAT32 y exFAT de forma coherente, incorpora gestión VHD/VHDX y muestra alertas de salud accionables.
- Auditoría de callbacks: eliminados registros duplicados que anulaban el cambio de idioma y la selección de disco; añadida prueba de interacción GUI.
- Recuperación limitada de archivos borrados FAT32 hacia un archivo nuevo: revalida entrada/FAT, rechaza clústeres reutilizados y calcula SHA-256.
- Diagnóstico ATA SMART para temperatura, horas y contadores críticos habituales, además de NVMe y fallo previsto.
- La extracción FAT32 vuelve a escanear referencias vivas justo antes de devolver datos; rechaza la imagen origen y, si el origen es un disco físico, cualquier destino en ese mismo disco (o falla de forma segura si no puede identificarlo).
- La interfaz 0.7.0 pone en primer plano acciones de disco frecuentes con iconos de color; las utilidades de bajo nivel se agrupan en Herramientas avanzadas y el tema claro es el predeterminado en instalaciones nuevas.

## En curso

- Fase 0: continuar auditoría GUI/CLI; ya existen pruebas de humo, interacción de idioma/filtro/validación y rechazo de argumentos CLI desconocidos.
- Fase 1: diagnóstico accionable, estados de salud más claros y datos adicionales expuestos por Windows.

## Pendiente

- Operaciones seguras de volumen y redimensionado consciente del sistema de archivos.
- Ampliar recuperación de archivos a LFN, directorios borrados y NTFS/exFAT; la base FAT32 ya funciona por CLI y GUI.
- Conversión de formatos de discos virtuales (no existe una API `ConvertVirtualDisk` en el SDK disponible; requiere un motor propio o dependencia compatible).
- Revisión de amenazas y diseño de capacidades físicas. Permanecen desactivadas hasta autorización específica y verificación.
- Pruebas VHDX/4096, instalador y actualización completos en entornos controlados.

Las limitaciones de `docs/COMPATIBILITY.md` son normativas. No se marca una capacidad como terminada sin prueba e integración.
