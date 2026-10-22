#pragma once

#include <cstdint>
#include <string>

class Bus;
class Video;
class Cpu6502;

// Utilidades de depuración: convierten el estado del emulador en imágenes,
// texto o JSON para poder inspeccionarlo desde fuera (un navegador, un script o
// un agente de IA que revisa lo que genera el programa).
//
// Cada volcado existe en dos sabores:
//   *_mem()  -> escribe en un std::string (lo que usa el servidor HTTP)
//   *()      -> escribe en un fichero
//
//   screenshot_png()  imagen exacta de lo que se vería en HDMI (320x240)
//   dump_vram_txt()   tilemap / atributos / OAM en texto legible
//   dump_video_json() lo mismo en JSON (más fácil de procesar)
//   dump_state_txt()  estado completo: CPU, vídeo, SID, GPIO, UART
namespace debug_dump {

// --- Imagen ----------------------------------------------------------------

// PNG con el framebuffer (0x00RRGGBB, 0xAARRGGBB). `scale` amplía por repetición
// de píxeles (1..8). Sin dependencias externas: deflate "stored".
void screenshot_png_mem(const uint32_t* rgb, int w, int h, int scale, std::string& out);
bool screenshot_png(const std::string& path, const uint32_t* rgb, int w, int h);

// PPM (P6) sin comprimir: 320x240 -> 230 KB. Útil si no hay decodificador PNG.
void screenshot_ppm_mem(const uint32_t* rgb, int w, int h, std::string& out);

// Renderiza el Core de Vídeo y guarda la imagen.
bool write_video_png(const std::string& path, Bus& bus);

// --- VRAM ------------------------------------------------------------------

// Tilemap, atributos, OAM y una vista ASCII de la parte visible.
bool dump_vram_txt_mem(Video& video, std::string& out);
bool dump_vram_txt(const std::string& path, Video& video);

// Tilemap, atributos y OAM en JSON.
bool dump_video_json_mem(Video& video, std::string& out);

// --- Estado ----------------------------------------------------------------

bool dump_state_txt_mem(Bus& bus, std::string& out);
// Igual, pero incluye el estado de la CPU (PC, registros y ciclos). Es lo que
// permite distinguir un programa que calcula de uno colgado: si el PC no se
// mueve entre dos lecturas y no esta en un bucle de espera, esta atascado.
bool dump_state_txt_mem(Bus& bus, const Cpu6502* cpu, std::string& out);
bool dump_state_txt(const std::string& path, Bus& bus);
bool dump_state_txt(const std::string& path, Bus& bus, const Cpu6502* cpu);

// --- Memoria ---------------------------------------------------------------

// Copia `len` bytes de RAM a partir de `start` (direcciones de CPU $0000-$3FFF).
void dump_ram_bin_mem(Bus& bus, unsigned start, unsigned len, std::string& out);

} // namespace debug_dump
