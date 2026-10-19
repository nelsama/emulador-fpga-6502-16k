#include "debug_dump.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <vector>

#include "bus.h"
#include "gpio_uart.h"
#include "ram_rom.h"
#include "sid.h"
#include "uart.h"
#include "video/video.h"

namespace {

// --- Escritura PNG mínima (sin zlib: deflate "stored") -----------------------

uint32_t crc_table[256];
bool crc_ready = false;

void init_crc() {
  if (crc_ready) return;
  for (uint32_t n = 0; n < 256; n++) {
    uint32_t c = n;
    for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
    crc_table[n] = c;
  }
  crc_ready = true;
}

uint32_t crc32(const uint8_t* data, size_t len) {
  init_crc();
  uint32_t c = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; i++) c = crc_table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
  return c ^ 0xFFFFFFFFu;
}

void put32(std::string& v, uint32_t x) {
  v.push_back((char)(x >> 24));
  v.push_back((char)(x >> 16));
  v.push_back((char)(x >> 8));
  v.push_back((char)x);
}

void write_chunk(std::string& out, const char type[4], const std::string& data) {
  put32(out, (uint32_t)data.size());
  const size_t start = out.size();
  out.append(type, 4);
  out.append(data);
  put32(out, crc32((const uint8_t*)out.data() + start, out.size() - start));
}

// Escribe un fichero completo. Devuelve false si no se pudo crear.
bool write_file(const std::string& path, const std::string& data) {
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  const bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
  std::fclose(f);
  return ok;
}

void appendf(std::string& s, const char* fmt, ...) {
  char buf[512];
  va_list ap;
  va_start(ap, fmt);
  const int n = std::vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (n > 0) s.append(buf, (size_t)(n < (int)sizeof(buf) ? n : (int)sizeof(buf) - 1));
}

// Carácter ASCII imprimible o '.' para la vista de texto de la VRAM.
char printable(uint8_t t) { return (t >= 0x20 && t < 0x7F) ? (char)t : '.'; }

// Lee un registro del SID sin efectos secundarios que nos importen.
uint8_t sid_read(Sid* s, uint16_t addr) {
  bool dummy = false;
  return s->read(addr, dummy);
}

} // namespace

namespace debug_dump {

// --- Imagen ----------------------------------------------------------------

void screenshot_png_mem(const uint32_t* rgb, int w, int h, int scale, std::string& out) {
  if (!rgb || w <= 0 || h <= 0) return;
  if (scale < 1) scale = 1;
  if (scale > 8) scale = 8;

  const int ow = w * scale;
  const int oh = h * scale;

  // Filas PNG: 1 byte de filtro (0 = ninguno) + RGB por pixel.
  std::string raw;
  raw.reserve((size_t)oh * (1 + (size_t)ow * 3));
  for (int y = 0; y < oh; y++) {
    raw.push_back(0);
    const uint32_t* row = rgb + (size_t)(y / scale) * w;
    for (int x = 0; x < ow; x++) {
      const uint32_t p = row[x / scale];
      raw.push_back((char)(p >> 16));
      raw.push_back((char)(p >> 8));
      raw.push_back((char)p);
    }
  }

  // Flujo zlib sin comprimir: cabecera + bloques "stored" de 65535 bytes.
  std::string z;
  z.reserve(raw.size() + raw.size() / 65535 * 5 + 16);
  z.push_back(0x78); // CMF: deflate, 32K
  z.push_back(0x01); // FLG
  size_t off = 0;
  while (off < raw.size()) {
    const size_t n = (raw.size() - off) > 65535 ? 65535 : (raw.size() - off);
    const bool last = (off + n) >= raw.size();
    z.push_back(last ? 1 : 0);
    z.push_back((char)(n & 0xFF));
    z.push_back((char)(n >> 8));
    z.push_back((char)(~n & 0xFF));
    z.push_back((char)((~n >> 8) & 0xFF));
    z.append(raw, off, n);
    off += n;
  }
  // Adler-32 sobre los datos sin comprimir.
  uint32_t a = 1, b = 0;
  for (size_t i = 0; i < raw.size(); i++) {
    a = (a + (uint8_t)raw[i]) % 65521;
    b = (b + a) % 65521;
  }
  put32(z, (b << 16) | a);

  std::string ihdr;
  put32(ihdr, (uint32_t)ow);
  put32(ihdr, (uint32_t)oh);
  ihdr.push_back(8); // bits por canal
  ihdr.push_back(2); // color type: RGB
  ihdr.push_back(0); // compresión
  ihdr.push_back(0); // filtro
  ihdr.push_back(0); // entrelazado

  out.clear();
  out.reserve(z.size() + 64);
  const char sig[8] = {(char)0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  out.append(sig, 8);
  write_chunk(out, "IHDR", ihdr);
  write_chunk(out, "IDAT", z);
  write_chunk(out, "IEND", "");
}

bool screenshot_png(const std::string& path, const uint32_t* rgb, int w, int h) {
  std::string png;
  screenshot_png_mem(rgb, w, h, 1, png);
  if (png.empty()) return false;
  return write_file(path, png);
}

void screenshot_ppm_mem(const uint32_t* rgb, int w, int h, std::string& out) {
  if (!rgb || w <= 0 || h <= 0) return;
  out.clear();
  out.reserve((size_t)w * h * 3 + 32);
  appendf(out, "P6\n%d %d\n255\n", w, h);
  for (int i = 0; i < w * h; i++) {
    const uint32_t p = rgb[i];
    out.push_back((char)(p >> 16));
    out.push_back((char)(p >> 8));
    out.push_back((char)p);
  }
}

bool write_video_png(const std::string& path, Bus& bus) {
  Video* v = bus.video();
  if (!v) return false;
  static std::vector<uint32_t> fb((size_t)Video::W * Video::H);
  v->render(fb.data());
  return screenshot_png(path, fb.data(), Video::W, Video::H);
}

// --- VRAM ------------------------------------------------------------------

bool dump_vram_txt_mem(Video& video, std::string& out) {
  out.clear();
  out.reserve(24000);

  out += "=== TILEMAP (64 x 32, area 00) ===\n";
  const auto& tm = video.tilemap();
  for (int y = 0; y < Video::MAP_H; y++) {
    for (int x = 0; x < Video::MAP_W; x++) appendf(out, "%02X ", tm[y * Video::MAP_W + x]);
    out.push_back('\n');
  }

  out += "\n=== ATRIBUTOS (64 x 32, area 01: bits1:0 = paleta) ===\n";
  const auto& at = video.attrib();
  for (int y = 0; y < Video::MAP_H; y++) {
    for (int x = 0; x < Video::MAP_W; x++) appendf(out, "%02X ", at[y * Video::MAP_W + x]);
    out.push_back('\n');
  }

  out += "\n=== OAM (32 sprites: X, Y, tile, flags) ===\n";
  const auto& oam = video.oam();
  int activos = 0;
  for (int s = 0; s < 32; s++) {
    const uint8_t* o = &oam[s * 5];
    if (o[1] >= 248) continue; // deshabilitado
    activos++;
    appendf(out, "  sprite %2d: X=%3d Y=%3d tile=%3d flags=%02X (pal=%d%s%s%s)\n", s, o[0], o[1],
            o[2], o[3], o[3] & 0x03, (o[3] & 0x40) ? " flipX" : "",
            (o[3] & 0x80) ? " flipY" : "", (o[3] & 0x10) ? " 2x" : "");
  }
  appendf(out, "  (%d de 32 sprites activos)\n", activos);

  out += "\n=== TILEMAP COMO TEXTO (40x30 visibles a scroll 0; tiles $20-$7F = ASCII) ===\n";
  for (int y = 0; y < 30; y++) {
    out += "  ";
    for (int x = 0; x < 40; x++) out.push_back(printable(tm[y * Video::MAP_W + x]));
    out.push_back('\n');
  }
  return true;
}

bool dump_vram_txt(const std::string& path, Video& video) {
  std::string s;
  dump_vram_txt_mem(video, s);
  return write_file(path, s);
}

bool dump_video_json_mem(Video& video, std::string& out) {
  out.clear();
  out.reserve(12000);

  const auto& tm = video.tilemap();
  const auto& at = video.attrib();
  const auto& oam = video.oam();

  out += "{\n  \"ancho\": 64,\n  \"alto\": 32,\n  \"tilemap\": [\n";
  for (int y = 0; y < Video::MAP_H; y++) {
    out += "    [";
    for (int x = 0; x < Video::MAP_W; x++) {
      appendf(out, "%d", tm[y * Video::MAP_W + x]);
      if (x != Video::MAP_W - 1) out += ", ";
    }
    out += (y == Video::MAP_H - 1) ? "]\n" : "],\n";
  }

  out += "  ],\n  \"atributos\": [\n";
  for (int y = 0; y < Video::MAP_H; y++) {
    out += "    [";
    for (int x = 0; x < Video::MAP_W; x++) {
      appendf(out, "%d", at[y * Video::MAP_W + x]);
      if (x != Video::MAP_W - 1) out += ", ";
    }
    out += (y == Video::MAP_H - 1) ? "]\n" : "],\n";
  }

  out += "  ],\n  \"texto\": [\n";
  for (int y = 0; y < 30; y++) {
    out += "    \"";
    for (int x = 0; x < 40; x++) {
      const char c = printable(tm[y * Video::MAP_W + x]);
      if (c == '"' || c == '\\') out.push_back('\\');
      out.push_back(c);
    }
    out += (y == 29) ? "\"\n" : "\",\n";
  }

  out += "  ],\n  \"sprites\": [\n";
  bool first = true;
  for (int s = 0; s < 32; s++) {
    const uint8_t* o = &oam[s * 5];
    if (o[1] >= 248) continue;
    if (!first) out += ",\n";
    first = false;
    appendf(out, "    {\"n\": %d, \"x\": %d, \"y\": %d, \"tile\": %d, \"flags\": %d}", s, o[0],
            o[1], o[2], o[3]);
  }
  out += "\n  ]\n}\n";
  return true;
}

// --- Estado ----------------------------------------------------------------

bool dump_state_txt_mem(Bus& bus, std::string& out) {
  out.clear();
  out += "=== ESTADO DEL EMULADOR ===\n";

  if (Gpio* g = bus.gpio()) {
    // leds() ya devuelve los 6 LEDs con 1 = encendido (activo en bajo).
    const uint8_t leds = g->leds();
    out += "GPIO:\n";
    appendf(out, "  dir1/dir2: %02X / %02X (1 = entrada)\n", g->dir1(), g->dir2());

    // Puertos 1 y 2 completos, bit a bit. Refleja exactamente lo que lee la CPU
    // en $C000/$C001 (pines si el bit es entrada, registro si es salida), así
    // que sirve para comprobar desde fuera si una pulsación llega al programa.
    const uint8_t p1 = g->port1_read();
    const uint8_t p2 = g->port2_read();
    const unsigned dir1 = g->dir1();
    const unsigned dir2 = g->dir2();

    out += "  puerto 1 ($C000) - joystick Atari:\n";
    appendf(out, "    valor leido: %02X  direccion: %02X\n", p1, dir1);
    out += "    bit pin senal    modo   nivel\n";
    struct { int bit; const char* n; } p1sen[] = {
        {3, "right"}, {4, "left"}, {5, "down"}, {6, "up"}, {7, "fire"},
    };
    // Pines del puerto 1 del Tang Nano 9K, por bit (ver 6502_board.cst).
    static const int kPinPuerto1[8] = {42, 41, 35, 40, 34, 33, 30, 29};
    for (int b = 7; b >= 0; b--) {
      const char* nombre = "(TM1638)";
      int pin = kPinPuerto1[b];
      for (auto& s : p1sen)
        if (s.bit == b) nombre = s.n;
      const bool entrada = (dir1 >> b) & 1;
      appendf(out, "     %d   %2d  %-9s %-6s %d\n", b, pin, nombre,
              entrada ? "entrada" : "salida", (p1 >> b) & 1);
    }

    // Resumen de pulsaciones, con el formato del joystick. Solo tiene sentido
    // si los bits están configurados como entrada.
    if ((dir1 & Gpio::JOY_MASK) == Gpio::JOY_MASK) {
      const uint8_t j = p1;
      struct { uint8_t bit; const char* n; } botones[] = {
          {Gpio::JOY_UP, "UP"},    {Gpio::JOY_DOWN, "DOWN"}, {Gpio::JOY_LEFT, "LEFT"},
          {Gpio::JOY_RIGHT, "RIGHT"}, {Gpio::JOY_FIRE, "FIRE"},
      };
      std::string lista;
      for (auto& b : botones) {
        if (j & b.bit) continue; // activo en bajo: 0 = pulsado
        if (!lista.empty()) lista += "+";
        lista += b.n;
      }
      if (lista.empty()) lista = "ninguno";
      appendf(out, "    pulsado: %s  (activo en bajo)\n", lista.c_str());
    } else {
      out += "    AVISO: los bits 3-7 no están configurados como entrada;"
             " el programa no ha llamado a joy_init().\n";
    }

    out += "  puerto 2 ($C001) - 6 LEDs:\n";
    appendf(out, "    valor leido: %02X  direccion: %02X\n", p2, dir2);
    appendf(out, "    LEDs encendidos: %02X  [", leds);
    for (int i = 5; i >= 0; i--) out += ((leds >> i) & 1) ? '#' : '.';
    out += "]  (bit5 = LEFT ... bit0 = RIGHT)\n";
  }

  if (Video* v = bus.video()) {
    const auto& tm = v->tilemap();
    int tiles_no_vacios = 0;
    for (uint8_t t : tm)
      if (t != 0) tiles_no_vacios++;
    int sprites = 0;
    const auto& oam = v->oam();
    for (int s = 0; s < 32; s++)
      if (oam[s * 5 + 1] < 248) sprites++;

    out += "VIDEO ($D800-$D817):\n";
    appendf(out, "  tiles escritos: %d de %d celdas\n", tiles_no_vacios, Video::MAP_W * Video::MAP_H);
    appendf(out, "  sprites activos: %d de 32\n", sprites);
    out += "  texto visible (24x40 desde fila 0):\n";
    for (int y = 0; y < 24; y++) {
      out += "    |";
      for (int x = 0; x < 40; x++) out.push_back(printable(tm[y * Video::MAP_W + x]));
      out += "|\n";
    }
  }

  if (Sid* s = bus.sid()) {
    out += "SID ($D400-$D41F):\n";
    for (int v = 0; v < 3; v++) {
      const uint16_t b = (uint16_t)(0xD400 + v * 7);
      const int freq = sid_read(s, b) | (sid_read(s, b + 1) << 8);
      const int pw = sid_read(s, b + 2) | (sid_read(s, b + 3) << 8);
      const uint8_t ctrl = sid_read(s, b + 4);
      appendf(out, "  voz %d: freq=%5d ($%04X) pw=%5d ctrl=%02X%s%s%s%s ad=%02X sr=%02X\n",
              v + 1, freq, freq, pw, ctrl, (ctrl & 0x10) ? " TRI" : "", (ctrl & 0x20) ? " SAW" : "",
              (ctrl & 0x40) ? " PUL" : "", (ctrl & 0x80) ? " NOI" : "", sid_read(s, b + 5),
              sid_read(s, b + 6));
    }
    const uint8_t fc_lo = sid_read(s, 0xD415);
    const uint8_t fc_hi = sid_read(s, 0xD416);
    const uint8_t res_filt = sid_read(s, 0xD417);
    const uint8_t mode_vol = sid_read(s, 0xD418);
    appendf(out, "  filtro: fc=%d rutas=%02X modo=%02X volumen=%d/15\n", fc_lo | ((fc_hi & 0x07) << 8),
            res_filt & 0x0F, mode_vol & 0xF0, mode_vol & 0x0F);
    // Envolventes: el SID player las usa como VU meter en los 6 LEDs.
    //   $D41C = ENV3 (compatible C64), $D41D = ENV1, $D41E = ENV2, $D41F = MAX.
    const uint8_t e1 = sid_read(s, 0xD41D);
    const uint8_t e2 = sid_read(s, 0xD41E);
    const uint8_t e3 = sid_read(s, 0xD41C);
    const uint8_t emax = sid_read(s, 0xD41F);
    appendf(out, "  envolventes: ENV1=%3d ENV2=%3d ENV3=%3d MAX=%3d  ($D41D/$D41E/$D41C/$D41F)\n",
            e1, e2, e3, emax);
  }

  if (Uart* u = bus.uart()) {
    out += "UART ($C020-$C023):\n";
    bool dummy = false;
    const uint8_t st = u->read(0xC021, dummy);
    appendf(out, "  STATUS=%02X%s%s%s%s%s\n", st, (st & 0x10) ? " OVERRUN" : "",
            (st & 0x08) ? " ERROR" : "", (st & 0x04) ? " TX_BUSY" : "",
            (st & 0x02) ? " RX_VALID" : "", (st & 0x01) ? " TX_READY" : "");
    appendf(out, "  baud_div=%d\n", u->read(0xC022, dummy) | (u->read(0xC023, dummy) << 8));
  }

  return true;
}

bool dump_state_txt(const std::string& path, Bus& bus) {
  std::string s;
  dump_state_txt_mem(bus, s);
  return write_file(path, s);
}

// --- Memoria ---------------------------------------------------------------

void dump_ram_bin_mem(Bus& bus, unsigned start, unsigned len, std::string& out) {
  out.clear();
  RamRom* r = bus.ram_rom();
  if (!r) return;
  if (start >= 16384) return;
  if (len > 16384 - start) len = 16384 - start;
  out.assign((const char*)r->ram() + start, len);
}

} // namespace debug_dump
