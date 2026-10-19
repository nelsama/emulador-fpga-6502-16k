cmake_minimum_required(VERSION 3.16)
project(EmuladorFpga6502 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_executable(emulador
  src/main.cpp
  src/bus.cpp
  src/cpu_6502.cpp
  src/ram_rom.cpp
  src/gpio_uart.cpp
  src/uart.cpp
  src/timer.cpp
  src/spi_sd.cpp
  src/sid.cpp
  src/tcp_server.cpp
  src/video/video.cpp
  src/video/render.cpp
)

target_include_directories(emulador PRIVATE
  src
  src/video
)

# Enlaces por plataforma (sockets).
if(WIN32)
  target_link_libraries(emulador PRIVATE ws2_32)
else()
  target_link_libraries(emulador PRIVATE pthread)
endif()

# SDL2 opcional (para el vídeo, en una fase posterior). Lo dejamos disponible
# pero no lo requerimos para el modo UART/headless.
find_package(SDL2 QUIET)
if(SDL2_FOUND)
  target_compile_definitions(emulador PRIVATE HAVE_SDL2=1)
  target_include_directories(emulador PRIVATE ${SDL2_INCLUDE_DIRS})
  target_link_libraries(emulador PRIVATE ${SDL2_LIBRARIES})
endif()
