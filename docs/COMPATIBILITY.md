# Matriz de compatibilidad (0.7.0)

La matriz describe el alcance documentado; **“No verificado” no significa compatible**. La elegibilidad concreta de escritura se vuelve a validar en tiempo de ejecución y puede ser más restrictiva.

| Capacidad | Archivos de imagen | VHD <=4 GiB | VHDX | Disco físico |
| --- | --- | --- | --- | --- |
| Analizar MBR/GPT | Sí, sectores 512/4096; VHD fijo documentado | Sí | No verificado | Lectura; 4096 en hardware real no verificado |
| Inventario / salud | N/A | Sí | Según inventario de Windows | Lectura; SMART depende del controlador |
| Copia de tabla (`.ptb`) / verificar | Sí | Sí | No verificado | Lectura |
| Restaurar tabla | No aplica como destino | Sí, tamaño/sector coincidente y otras guardas | No verificado | Bloqueado |
| Clonar | Origen permitido | Sí, sujeto a guardas y destino virtual | No verificado | Bloqueado como destino |
| Editar tabla MBR/GPT | No aplica como destino | Sí; MBR solo primarias | No verificado | Bloqueado |
| Formato rápido FAT32 / exFAT | No aplica | Sí; operación sobre una partición y sujeta a guardas | No verificado | Bloqueado |
| Formato NTFS | No | No | No | No |
| Buscar particiones perdidas | Sí, según formato reconocido | Sí, solo lectura | No verificado | Lectura; uso sobre medios reales no validado |
| Redimensionar/mover particiones | Vista previa solamente | Vista previa solamente; no ajusta datos | Vista previa solamente | Vista previa solamente |
| Restaurar imagen completa | Origen permitido mediante clonación | Destino permitido con guardas | No verificado | Bloqueado como destino |
| Recuperar archivos | FAT32 limitado: entradas borradas en directorios accesibles, salida nueva | Igual, solo lectura del origen | No implementado | FAT32 limitado, solo lectura del origen |
| Crear/conectar/desconectar VHD/VHDX | Archivo nuevo/existente; conexión solo lectura por defecto | Sí | Sí | N/A |

## Guardas de escritura conocidas

- Solo discos virtuales identificados como tales y de hasta 4 GiB; el disco del sistema queda bloqueado.
- Operaciones destructivas requieren vista previa y confirmación escrita específica; la aplicación relee/verifica el resultado cuando corresponde.
- Las operaciones que afectan volúmenes montados pueden requerir bloqueo y desmontaje del volumen afectado.
- Los requisitos exactos dependen de la operación; consultar su ayuda y los bloqueos mostrados por Disk Pro.
- Pruebas de escritura solo en imágenes sintéticas o VHD/VHDX desechables. Esta matriz no autoriza pruebas sobre discos físicos.

Actualizar esta tabla junto con README y changelog en cada release. Antes de marcar soporte, añadir pruebas de éxito, límites y fallos para el caso correspondiente.
