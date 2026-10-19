@echo off
REM ---------------------------------------------------------------------------
REM  Emulador FPGA 6502 - lanzador CON depuracion HTTP
REM
REM  Igual que run.bat, pero activa el servidor HTTP para poder ver la pantalla
REM  y el estado del emulador desde el navegador o desde un agente de IA.
REM
REM  Uso:
REM    run_debug.bat                       -> UART 4000, debug en 8080
REM    run_debug.bat 4001                  -> otro puerto de UART
REM    run_debug.bat 4000 otra.img         -> otra imagen de SD
REM    run_debug.bat 4000 tarjeta.img 9000 -> debug en el puerto 9000
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

set DEBUG_PORT=%~3
if "%DEBUG_PORT%"=="" set DEBUG_PORT=8080

if not exist "emulador.exe" (
  echo Compilando...
  call make || goto :error
)
if not exist "rom\monitor.bin" (
  echo [!] Falta rom\monitor.bin
  goto :error
)

echo ============================================================
echo   Emulador FPGA 6502   [DEPURACION HTTP ACTIVA]
echo   ROM    : rom\monitor.bin
echo   SD     : %IMAGE%
echo   Puerto : %PORT%   (PuTTY: Raw, localhost:%PORT%)
echo   Debug  : http://localhost:%DEBUG_PORT%
echo            /            vista previa + estado
echo            /screen.png  captura de lo que saldria por HDMI
echo            /vram.json   tilemap, atributos y sprites
echo            /state.txt   GPIO, SID, video, UART
echo ============================================================

"emulador.exe" "rom\monitor.bin" %PORT% "%IMAGE%" --debug=%DEBUG_PORT%
goto :eof

:error
echo.
echo Error al arrancar el emulador.
pause
