@echo off
REM ---------------------------------------------------------------------------
REM  Emulador FPGA 6502 - lanzador
REM
REM  Uso:
REM    run.bat                       -> monitor + tarjeta.img, UART en 4000
REM    run.bat 4001                  -> otro puerto de UART
REM    run.bat 4000 otra.img         -> otra imagen de SD
REM
REM  Para depurar (ver la pantalla y el estado por HTTP), usa run_debug.bat
REM
REM  Variables de entorno:
REM    EMU_AUDIO_DEVICE=...  -> forzar un dispositivo de audio por nombre
REM    EMU_WAV=grabacion.wav -> grabar la salida del SID a WAV
REM    EMU_TRACE=1           -> mostrar diagnostico de rendimiento y audio
REM ---------------------------------------------------------------------------
setlocal
cd /d "%~dp0"

set PORT=%~1
if "%PORT%"=="" set PORT=4000

set IMAGE=%~2
if "%IMAGE%"=="" set IMAGE=tarjeta.img

REM Argumentos extra (p. ej. --debug) a partir del tercero.
set EXTRA=%3 %4 %5 %6 %7 %8 %9

if not exist "emulador.exe" (
  echo Compilando...
  call make || goto :error
)
if not exist "rom\monitor.bin" (
  echo [!] Falta rom\monitor.bin
  goto :error
)

echo ============================================================
echo   Emulador FPGA 6502
echo   ROM    : rom\monitor.bin
echo   SD     : %IMAGE%
echo   Puerto : %PORT%   (PuTTY: Raw, localhost:%PORT%)
echo   Debug  : no (usa run_debug.bat para activarlo)
echo ============================================================

"emulador.exe" "rom\monitor.bin" %PORT% "%IMAGE%" %EXTRA%
goto :eof

:error
echo.
echo Error al arrancar el emulador.
pause
