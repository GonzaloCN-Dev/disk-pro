# Hoja de ruta: Disk Pro

## Objetivo

Convertir Disk Pro en una herramienta clara y fiable para inspeccionar, diagnosticar y administrar discos y volúmenes en Windows. “Gestión completa” significa cubrir los flujos habituales de principio a fin —desde entender el estado hasta ejecutar una operación y comprobar el resultado—, no prometer que cualquier operación sea posible en cualquier sistema de archivos o dispositivo.

La prioridad es **no perder datos**. Una función que no pueda explicar y verificar sus efectos debe permanecer en vista previa o no ofrecerse. No se simularán capacidades que no estén implementadas.

## Principios de producto

- **Seguro por defecto:** inspección y diagnóstico no escriben. Las operaciones destructivas muestran origen, destino, cambios y riesgos antes de habilitar Aplicar.
- **Permisos mínimos:** pedir elevación solo al ejecutar la acción que la necesita; no mantener toda la aplicación elevada.
- **Separación estricta:** análisis portable en `src/core`; acceso a dispositivos en `src/platform/win`; escritura en motores separados, con validación independiente.
- **Sin sorpresas:** detectar y explicar bloqueos por disco del sistema, volúmenes montados, BitLocker, espacio insuficiente, errores de E/S o medios extraíbles.
- **Verificar cada operación:** releer y validar el resultado; si falla, conservar un informe que explique qué se completó y qué no. No prometer rollback cuando no sea técnicamente posible.
- **Pruebas inocuas:** imágenes sintéticas y VHD/VHDX desechables. Nunca ejecutar pruebas de escritura sobre discos físicos.
- **Interfaz guiada:** términos comprensibles, valores con unidades, valores por defecto prudentes, deshacer disponible cuando sea real y accesible, navegación por teclado y accesibilidad.

## Alcance funcional deseado

1. **Inventario:** discos físicos, virtuales y extraíbles; conexión, modelo, bus, capacidad, sector lógico/físico, volúmenes, letras, sistema de archivos, cifrado detectable y disco de sistema.
2. **Mapa y diagnóstico:** particiones, espacio libre, alineación, salud GPT/MBR, capacidad usada/libre cuando Windows la exponga y alertas accionables.
3. **Mantenimiento:** SMART/NVMe y ATA cuando el controlador lo permita; análisis de superficie de solo lectura; avisos tempranos y exportación de informes.
4. **Particiones y volúmenes:** crear, borrar, cambiar tipo/etiqueta/letra, formatear, redimensionar y mover, con soporte explícito por sistema de archivos y límites claros.
5. **Copias y recuperación:** imágenes, verificación, restauración/clonado, copia de tabla, búsqueda de particiones perdidas y recuperación de archivos hacia otro destino.
6. **Discos virtuales:** crear, montar, desmontar, inspeccionar y convertir formatos compatibles, con límites de tamaño y espacio previsible.
7. **Experiencia:** asistente de operaciones, cola de tareas cancelable cuando sea seguro, progreso real, registro local sin datos sensibles y rescate/documentación ante fallos.

## Fases y criterios de salida

### Estado de ejecución

- **Fase 0 en curso:** matriz inicial añadida en `COMPATIBILITY.md`; CI de Windows compila y ejecuta pruebas y también compila con MSVC `/analyze`.
- Se corrigió la ayuda de `diskpro-part`: el token de formato es distinto al de edición de tabla. Se redujo uso de pila en buffers de lectura y se atendieron hallazgos del análisis estático.
- Añadida prueba de humo `gui-smoke` (`--selftest`): construye e inicia la GUI, espera el inventario y cierra. No automatiza todavía clics ni operaciones desde la interfaz.
- Pendiente de fase 0: auditoría completa de coherencia GUI/CLI, pruebas automatizadas de interacción UI y completar la cobertura de análisis estático sin advertencias propias.
- **Fase 1 iniciada:** búsqueda de discos ampliada a serie enmascarada, particiones, sistemas de archivos, etiquetas y errores. El inventario muestra etiqueta y espacio libre/total del volumen cuando Windows lo facilita; el informe exportado incluye esos datos.
- El diagnóstico de salud muestra datos NVMe ampliados y atributos ATA SMART habituales cuando el controlador entrega el bloque estándar; los contadores degradados generan una recomendación accionable.
- **Fase 5 iniciada:** `diskpro-vhd` y la GUI crean VHD/VHDX sin sobrescribir, conectan en solo lectura por defecto y desconectan mediante la API nativa de Windows. La creación desechable está automatizada; conectar/desconectar requiere privilegio de Windows y queda pendiente de prueba elevada controlada.
- Empaquetado portable e instalador compilan con `diskpro-vhd`; no se han instalado ni publicado. La prueba GUI de interacción detectó y permitió corregir callbacks duplicados de idioma/selección.
- **Fase 3 iniciada:** recuperación FAT32 acotada enumera en CLI/GUI entradas borradas de directorios accesibles y extrae a un archivo nuevo tras revalidar la asignación; faltan LFN, directorios borrados y NTFS/exFAT.

### Fase 0 — Consolidar la base y la confianza

- Mantener README, changelog y arquitectura sincronizados con el producto.
- Añadir una matriz de funciones por operación, sistema de archivos, GPT/MBR, sector y tipo de disco.
- Revisar consistencia de confirmaciones, límites y bloqueos en GUI y CLI.
- Automatizar build, pruebas unitarias, análisis estático y empaquetado reproducible en CI.
- Añadir pruebas de UI para flujos críticos y pruebas de regresión con imágenes sintéticas.

**Salida:** documentación verificable; CI reproducible; ninguna discrepancia conocida sobre qué escribe cada comando.

### Fase 1 — Inventario y diagnóstico de nivel diario

- Mejorar refresco, agrupación y selección de discos y volúmenes; mostrar claramente disco físico frente a virtual.
- Añadir filtros, búsqueda, copiar/exportar detalles y diagnósticos con explicación y acción recomendada.
- Completar datos de capacidad de volumen y salud ATA cuando estén disponibles; distinguir “no compatible” de “error”.
- Mejorar alertas de disco del sistema, BitLocker/cifrado, volúmenes montados y estado degradado.

**Salida:** el usuario puede identificar el dispositivo correcto y entender advertencias sin depender de códigos técnicos.

### Fase 2 — Operaciones seguras en discos virtuales

- Endurecer operaciones existentes (tabla, formato, clonación y restauración): plan previo común, validación antes/después, relectura y registro de resultado.
- Aplicar redimensionado/movimiento solo mediante motores conscientes del sistema de archivos y con soporte probado. Mantener la vista previa si no existe soporte seguro.
- Añadir creación/eliminación/etiquetado de volúmenes y asignación de letras con previsualización y confirmación contextual.
- Expandir las pruebas desechables a VHDX y tamaños de sector compatibles, sin relajar las guardas actuales.

**Salida:** cada acción soportada tiene matriz de compatibilidad, pruebas de éxito y fallo, y una explicación de sus límites.

### Fase 3 — Imágenes y continuidad ante fallos

- Crear/restaurar imágenes en archivos y discos virtuales: selección clara de origen/destino, capacidad requerida, checksum, reanudación donde sea fiable y verificación.
- Definir formato de imagen documentado, metadatos, integridad y compatibilidad futura.
- Mejorar recuperación de particiones: mostrar candidatos, confianza, solapes y vista previa; no escribir durante el análisis.
- Diseñar recuperación de archivos hacia **otro destino**, conservando nombres cuando sea posible y reportando archivos incompletos.

**Salida:** fallos de lectura, cancelación, falta de espacio y corrupción quedan reportados; el origen nunca se sobrescribe accidentalmente.

### Fase 4 — Administración física, solo tras una puerta de seguridad

- No habilitar escritura en discos físicos por defecto. Primero completar revisión de amenazas, pruebas automatizadas, confirmación explícita del usuario y documentación de riesgos.
- Implementar cada operación física como capacidad separada y desactivada hasta superar su matriz de pruebas. Excluir siempre el disco del sistema y destinos ambiguos; volver a identificar el dispositivo justo antes de escribir.
- Mantener vista previa obligatoria con modelo, número de serie enmascarado, tamaño, bus y volúmenes afectados; exigir confirmación inequívoca.
- Nunca ejecutar operaciones de particionado/formato sobre un disco físico como parte de pruebas automatizadas.

**Salida:** autorización explícita del propietario del producto para habilitar cada tipo de escritura física; pruebas manuales controladas documentadas y mecanismo de parada de emergencia.

### Fase 5 — Discos virtuales y pulido

- Flujos para crear, montar/desmontar y convertir formatos de disco virtual compatibles.
- Accesibilidad, escalado DPI, localización ES/EN completa, manejo de errores coherente y rendimiento con muchos discos.
- Instalador, actualización, firma y publicación solo después de pruebas de instalación/actualización y autorización de publicación.

**Salida:** operaciones virtuales seguras y reversibles cuando aplique; instalación y actualización verificadas en Windows soportado.

## Interfaz de una operación destructiva

1. Elegir explícitamente disco/volumen y operación.
2. Mostrar estado actual y resultado previsto con tamaños/unidades.
3. Validar compatibilidad, espacio, bloqueos y riesgos antes de pedir confirmación.
4. Solicitar elevación solo si hace falta; volver a validar identidad y estado al elevar.
5. Ejecutar con progreso y cancelación solo en puntos seguros.
6. Releer, verificar y presentar resultado, advertencias y próximos pasos.

Una confirmación nunca sustituye las guardas técnicas. Para operaciones irreversibles, el texto de confirmación debe identificar el destino, no ser una casilla genérica.

## Métricas de calidad (sin telemetría)

- Cobertura de pruebas por motor y casos de fallo; cero pruebas de escritura sobre discos reales.
- Toda operación de escritura tiene plan previo, validación de destino, límites, cancelación definida y verificación posterior.
- Cero bloqueos de UI durante E/S; progreso basado en trabajo real y errores recuperables explicados.
- Ningún envío de datos de uso. Los diagnósticos exportados son voluntarios y revisables antes de guardarse.

## Fuera de alcance / decisiones pendientes

- No prometer soporte universal para todos los sistemas de archivos, controladores RAID/USB ni dispositivos dañados.
- El redimensionado de sistemas de archivos requiere un motor compatible y pruebas específicas; cambiar solo los límites de partición no equivale a redimensionar los datos.
- La recuperación no debe guardar resultados en el mismo origen.
- La política vigente limita escritura a VHD de hasta 4 GiB. Cualquier ampliación requiere decisión explícita, revisión de seguridad y pruebas; esta hoja de ruta no la amplía por sí sola.
- La hoja de ruta no autoriza cambios, pruebas sobre discos físicos, firma ni publicación.
