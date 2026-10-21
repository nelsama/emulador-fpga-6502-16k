# Makefile para EmuladorFpga6502
# Compila con g++ (UCRT64). Uso:
#   make          -> compila el emulador (emulador.exe)
#   make run      -> compila y ejecuta con rom/monitor.bin
#   make clean    -> borra objetos y ejecutable
#   make rebuild  -> clean + all

CXX       := g++
CXXFLAGS  := -std=c++17 -O2 -Wall -MMD -MP -Isrc \
             -Ithird_party/residfp -Ithird_party/residfp/resample
LDFLAGS   := -lws2_32 -lwinmm

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

.PHONY: all run test clean rebuild info

all: $(BIN)

$(BIN): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS) $(LDFLAGS)
	@if [ "$(SDL2_FOUND)" = "1" ]; then cp -f $(SDL2_PREFIX)/bin/SDL2.dll . 2>/dev/null && echo "Copia: SDL2.dll"; fi

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

%.o: %.cc
	$(CXX) $(CXXFLAGS) -c -o $@ $<

run: $(BIN)
	./$(BIN) rom/monitor.bin 4000 tarjeta.img

# Pruebas rápidas (no necesitan SDL ni abrir ventana).
test: test_terminal.exe test_gpio_flag.exe
	./test_terminal.exe
	./test_gpio_flag.exe

test_gpio_flag.exe: tools/test_gpio_flag.cpp src/gpio_uart.cpp src/gpio_uart.h src/device.h
	$(CXX) -std=c++17 -O2 -Wall -Isrc -o $@ tools/test_gpio_flag.cpp src/gpio_uart.cpp

test_terminal.exe: tools/test_terminal.cpp src/terminal/terminal_state.cpp src/terminal/terminal_state.h src/terminal/font_cp437.h
	$(CXX) -std=c++17 -O2 -Wall -Isrc -o $@ tools/test_terminal.cpp src/terminal/terminal_state.cpp

info:
	@echo "SDL2: $(SDL2_FOUND) (1=audio/video habilitados)"
	@echo "SDL2_PREFIX: $(SDL2_PREFIX)"

clean:
	rm -f $(OBJS) $(DEPS) $(BIN) test_terminal.exe test_gpio_flag.exe

rebuild: clean all

-include $(DEPS)
