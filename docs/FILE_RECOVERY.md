# Recuperación de archivos

Disk Pro incorpora una primera recuperación **solo de lectura sobre el origen** para FAT32. Busca entradas borradas de archivos regulares dentro de directorios que todavía son accesibles y permite copiar un candidato a un archivo nuevo.

## Uso

```text
diskpro-tools files imagen.raw 1 512
diskpro-tools extract imagen.raw 1 2 D:\Rescate\archivo.bin 512
```

Los números indican la partición y el candidato mostrados por `files`. El origen también puede ser `disk:N`; leer un disco físico suele requerir administrador. La salida:

- debe estar en otro destino;
- se crea con semántica exclusiva y nunca se sobrescribe;
- se elimina si la lectura, escritura o vaciado falla;
- incluye el SHA-256 de los bytes recuperados.

Antes de extraer, Disk Pro vuelve a leer la entrada de directorio y la FAT. Rechaza el candidato si cambió, si sus clústeres fueron reutilizados o si ahora los referencia un archivo accesible.

## Puntuación y límites

La puntuación es estructural, no una probabilidad:

- **85/100:** la entrada está borrada y conserva una cadena FAT completa no referenciada por archivos accesibles.
- **65/100:** la entrada está borrada y el rango contiguo requerido sigue marcado como libre.

En FAT32 se pierde el primer carácter del nombre corto al borrar; Disk Pro lo sustituye por `_`. Esta versión no reconstruye nombres largos LFN, directorios borrados ni archivos fragmentados cuya cadena FAT ya se eliminó. No admite todavía recuperación de archivos NTFS/exFAT. Aunque la estructura parezca válida, el contenido puede haber sido sobrescrito: se debe revisar el archivo recuperado y su hash.
