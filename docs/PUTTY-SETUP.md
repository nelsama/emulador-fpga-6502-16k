# Configuración de PuTTY para el emulador

## Síntoma: caracteres perdidos, comandos raros, `>` repetidos

Si al teclear `ls` en PuTTY sale algo como:

```
>ls
s
Cmd deshabilitado p/ XMODEM
```

o aparecen líneas `>` vacías de más, la configuración de PuTTY no es la
adecuada. El emulador está correcto (verificado con 20 ms entre caracteres).

## Configuración correcta

### Session

| Campo | Valor |
|---|---|
| Connection type | **Raw** (no Telnet, no Serial) |
| Host Name | `localhost` |
| Port | el mismo que uses al lanzar el emulador (por defecto `4000`) |

> En la barra inferior de PuTTY debe verse `RAW/4000`.
> **Telnet** envía negociaciones (IAC) que el monitor interpreta como comandos.

### Terminal → Line discipline options

| Opción | Valor |
|---|---|
| Local echo | **Auto** (nunca "Force on") |
| Local line editing | **Auto** (nunca "Force on") |

Con "Force on" verás cada carácter dos veces y el monitor recibirá texto
mezclado.

### Terminal → Keyboard

| Opción | Valor |
|---|---|
| The Backspace key | **Control-H** |
| The Return key | **Control-M** (CR) |

El monitor espera `CR` (0x0D). Si PuTTY envía `LF` (0x0A), no reconoce el fin
de línea.

### Window → Translation

| Opción | Valor |
|---|---|
| Remote character set | **CP437** (o *Use font encoding* con una fuente que la incluya) |

Los programas de esta plataforma (Tetris, editor, monitor) dibujan marcos y
bloques con la tabla **CP437**: `│ ─ ┌ ┐ █ ▓ ▒`. Si PuTTY está en UTF-8 se ven
como huecos o caracteres raros. El terminal interno del emulador usa CP437, así
que con esta opción se ve **lo mismo en los dos sitios**.

> No es UTF-8: el UART mueve bytes sueltos, no texto Unicode.

### Terminal → Implicit CR in every LF

| Opción | Valor |
|---|---|
| Implicit CR in every LF | **Desmarcado** |

## Verificación

En MSYS2 UCRT64:

```bash
cd /d/Proyectos/EmuladorFpga6502
make
./emulador.exe rom/monitor.bin 4000 tarjeta.img
```

Debe aparecer:

```
UART emulado escuchando en localhost:4000 (PuTTY modo Raw)
Monitor arrancando...
Para conectar: PuTTY -> Raw -> localhost:4000
Audio: 48000 Hz, SDL2
```

En PuTTY, al conectar:

```
Tang Nano 9K - 6502 @ 3.375 MHz

--- Monitor 6502 v2.6.4 ---
H=ayuda, SD=SD Card, Q=reset
Inicializando SD Card...
  SD OK, tipo: HD
Montando MicroFS...
  FS montado OK

>
```

Teclea `LS` + Enter:

```
>LS
Archivos:
  (vacio)
Total: 0/16 archivos

>
```

## Compatibilidad de mayúsculas y minúsculas

El monitor compara los comandos en **mayúsculas** (`H`, `LS`, `D`, `SAVE`...).
En el hardware real con un terminal serie normal se teclea en mayúsculas o el
monitor convierte la entrada. Usa **mayúsculas** en PuTTY.
