# Imágenes de disco

Disk Pro crea imágenes **raw**: una secuencia byte a byte del origen, sin cabecera propietaria y con el mismo tamaño lógico. Esto permite analizarlas con `diskpro-cli` y usarlas como origen de `diskpro-clone`.

## Integridad

`diskpro-tools image` calcula SHA-256 mientras escribe e informa:

- bytes producidos;
- bloques ilegibles;
- SHA-256 del archivo resultante.

Los bloques que no pueden leerse se escriben como ceros y se contabilizan; por ello una imagen con bloques ilegibles no debe considerarse una copia íntegra aunque tenga un hash reproducible.

Para verificar posteriormente:

```text
diskpro-tools verify imagen.raw SHA256_ESPERADO
```

El comando devuelve error si el hash no coincide o si hubo bloques ilegibles. Disk Pro nunca sobrescribe una imagen existente.

## Restauración

Una imagen raw se restaura sobre un VHD permitido mediante:

```text
diskpro-clone --image imagen.raw --to N
diskpro-clone --image imagen.raw --to N --confirm "CLONE TO DISK N"
```

La primera ejecución es una vista previa. El destino debe cumplir todas las guardas de escritura y tener capacidad suficiente y el mismo tamaño de sector.

VHD fijo se puede analizar como archivo ocultando automáticamente su pie de 512 bytes. VHDX se gestiona mediante la API de discos virtuales de Windows y no se interpreta directamente como contenedor raw.
