# Makefile para EmuladorFpga6502
# Compila con g++ (UCRT64). Uso:
#   make          -> compila el emulador (emulador.exe)
#   make run      -> compila y ejecuta con rom/monitor.bin
#   make clean    -> borra objetos y ejecutable
#   make rebuild  -> clean + all

CXX       := g++
CXXFLAGS  := -std=c++17 -O2 -Wall -MMD -MP -Isrc \
             -Ithird_party/residfp -Ithird_party/residfp/resample
LDFLAGS   := -lws2_32 -lwinmm -lcomdlg32

# SDL2 es opcional: si está instalado se habilita el audio (y más adelante el
# vídeo). MSYS2 UCRT64 lo instala en C:/msys64/ucrt64, pero desde un shell
# MSYS2 la ruta es /ucrt64 y desde Git Bash C:/msys64/ucrt64. Se prueban ambas
# y también lo que indique SDL2_PREFIX en el entorno.
SDL2_CANDIDATES := $(SDL2_PREFIX) /ucrt64 C:/msys64/ucrt64 $(shell cygpath -m /ucrt64 2>/dev/null)
SDL2_INC := $(firstword $(foreach p,$(SDL2_CANDIDATES),$(if $(wildcard $(p)/include/SDL2/SDL.h),$(p))))
ifneq (,$(SDL2_INC))
  SDL2_PREFIX := $(SDL2_INC)
  CXXFLAGS += -DHAVE_SDL2=1 -I$(SDL2_PREFIX)/include/SDL2
  LDFLAGS  += -L$(SDL2_PREFIX)/lib -lmingw32 -lSDL2main -lSDL2
  SDL2_FOUND := 1
else
  SDL2_FOUND := 0
endif

# Núcleo reSIDfp (VICE) — se compila dentro del proyecto.
RESIDFP_DIR := third_party/residfp
RESIDFP_SRCS := \
  $(RESIDFP_DIR)/residfp_Dac.cpp \
  $(RESIDFP_DIR)/residfp_EnvelopeGenerator.cpp \
  $(RESIDFP_DIR)/residfp_ExternalFilter.cpp \
  $(RESIDFP_DIR)/residfp_Filter.cpp \
  $(RESIDFP_DIR)/Filter6581.cpp \
  $(RESIDFP_DIR)/Filter8580.cpp \
  $(RESIDFP_DIR)/FilterModelConfig.cpp \
  $(RESIDFP_DIR)/FilterModelConfig6581.cpp \
  $(RESIDFP_DIR)/FilterModelConfig8580.cpp \
  $(RESIDFP_DIR)/Integrator6581.cpp \
  $(RESIDFP_DIR)/Integrator8580.cpp \
  $(RESIDFP_DIR)/OpAmp.cpp \
  $(RESIDFP_DIR)/residfp_SID.cpp \
  $(RESIDFP_DIR)/residfp_Spline.cpp \
  $(RESIDFP_DIR)/residfp_State.cpp \
  $(RESIDFP_DIR)/residfp_WaveformGenerator.cpp \
  $(RESIDFP_DIR)/WaveformCalculator.cpp \
  $(RESIDFP_DIR)/version.cc \
  $(RESIDFP_DIR)/resample/SincResampler.cpp

SRCS := $(wildcard src/*.cpp) $(wildcard src/video/*.cpp) $(wildcard src/terminal/*.cpp) $(RESIDFP_SRCS)
OBJS := $(patsubst %.cpp,%.o,$(patsubst %.cc,%.o,$(SRCS)))
DEPS := $(OBJS:.o=.d)
BIN  := emulador.exe

.PHONY: all run test clean rebuild info dist

all: $(BIN)

$(BIN): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS) $(LDFLAGS)
	@if [ "$(SDL2_FOUND)" = "1" ]; then cp -f $(SDL2_PREFIX)/bin/SDL2.dll . 2>/dev/null && echo "Copia: SDL2.dll"; fi

# --- Distribucion --------------------------------------------------------
#
# deja en dist/ TODO lo necesario para llevarse el emulador a otra maquina
# Windows x64: el .exe, sus DLLs, la ROM, la tarjeta y los lanzadores.
# No incluye fuentes ni herramientas: solo lo que se ejecuta.
#
#   make dist          -> regenera dist/ (compila si hace falta)
#   make dist-clean    -> borra dist/
#
# Para comprobar que no falta ninguna DLL, arranca en un equipo sin MSYS2.
DIST := dist
DIST_DLLS := SDL2.dll libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll

dist: $(BIN)
	@rm -rf $(DIST)
	@mkdir -p $(DIST)/rom
	@cp -f $(BIN) $(DIST)/
	@for d in $(DIST_DLLS); do \
	  if [ -f "$$d" ]; then cp -f "$$d" $(DIST)/; \
	  else echo "[!] falta $$d: el emulador no arrancara fuera de MSYS2"; fi; \
	done
	@cp -f rom/monitor.bin $(DIST)/rom/
	@if [ -f tarjeta.img ]; then cp -f tarjeta.img $(DIST)/; \
	else echo "[!] sin tarjeta.img: el monitor arrancara sin SD"; fi
	@cp -f run.bat run_debug.bat $(DIST)/ 2>/dev/null || true
	@# Manual en HTML autocontenido (se abre con doble clic, sin conexion).
	@# Si falta Python o el paquete markdown, se copia el .md tal cual.
	@if command -v python >/dev/null 2>&1 && \
	  python -c "import markdown" >/dev/null 2>&1; then \
	  python tools/md2html.py docs/GUIA-USO.md $(DIST)/MANUAL.html \
	    "Emulador FPGA 6502 - Manual de uso"; \
	else \
	  cp -f docs/GUIA-USO.md $(DIST)/MANUAL.md; \
	  echo "[!] sin python+markdown: se copia MANUAL.md en vez de MANUAL.html"; \
	fi
	@# Sexta comprobacion: el .exe debe cargar sin MSYS2 en el PATH.
	@$(MAKE) --no-print-directory dist-verificar

# Comprueba que el ejecutable de dist/ solo depende de DLLs del sistema o de
# las que se han copiado. Requiere objdump (MSYS2).
dist-verificar:
	@if command -v objdump >/dev/null 2>&1; then \
	  faltan=$$(objdump -p $(DIST)/$(BIN) | sed -n 's/.*DLL Name: //p' | \
	    grep -Ei '^(lib|SDL)' | grep -vE '^(KERNEL32|USER32|GDI32|WINMM|WS2_32|comdlg32|SHELL32|ADVAPI32|OLE32|OLEAUT32|SETUPAPI|IMM32|VERSION)\.dll$$' | \
	    while read n; do [ -f "$(DIST)/$$n" ] || echo "$$n"; done); \
	  if [ -n "$$faltan" ]; then echo "[!] DLLs sin copiar: $$faltan"; exit 1; \
	  else echo "dist: OK ($(DIST)/ listo para copiar)"; fi; \
	else echo "dist: generado (sin objdump, no se pudo verificar)"; fi

dist-clean:
	@rm -rf $(DIST)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

%.o: %.cc
	$(CXX) $(CXXFLAGS) -c -o $@ $<

run: $(BIN)
	./$(BIN) rom/monitor.bin 4000 tarjeta.img

# Pruebas rápidas (no necesitan SDL ni abrir ventana).
test: test_terminal.exe test_gpio_flag.exe test_video.exe
	./test_terminal.exe
	./test_gpio_flag.exe
	./test_video.exe

test_gpio_flag.exe: tools/test_gpio_flag.cpp src/gpio_uart.cpp src/gpio_uart.h src/device.h
	$(CXX) -std=c++17 -O2 -Wall -Isrc -o $@ tools/test_gpio_flag.cpp src/gpio_uart.cpp

test_terminal.exe: tools/test_terminal.cpp src/terminal/terminal_state.cpp src/terminal/terminal_state.h src/terminal/font_cp437.h
	$(CXX) -std=c++17 -O2 -Wall -Isrc -o $@ tools/test_terminal.cpp src/terminal/terminal_state.cpp

test_video.exe: tools/test_video.cpp src/video/video.cpp src/video/video.h src/video/font_c64.h src/device.h
	$(CXX) -std=c++17 -O2 -Wall -Isrc -o $@ tools/test_video.cpp src/video/video.cpp

info:
	@echo "SDL2: $(SDL2_FOUND) (1=audio/video habilitados)"
	@echo "SDL2_PREFIX: $(SDL2_PREFIX)"

clean:
	rm -f $(OBJS) $(DEPS) $(BIN) test_terminal.exe test_gpio_flag.exe test_video.exe
	rm -rf $(DIST)

rebuild: clean all

-include $(DEPS)
