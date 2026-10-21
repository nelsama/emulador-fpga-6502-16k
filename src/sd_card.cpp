#include "sd_card.h"

#include <cstring>

// ============================================================================
//  Protocolo SPI de tarjeta SD (SPI mode, SD spec v2.00)
// ============================================================================
//  Comandos relevantes:
//    CMD0   (0x40) GO_IDLE_STATE             -> R1 = 0x01
//    CMD8   (0x48) SEND_IF_COND  [7:0]=arg   -> R7 (R1 + 4 bytes)
//    CMD9   (0x49) SEND_CSD                  -> R1 + bloque de 16
//    CMD10  (0x4A) SEND_CID                  -> R1 + bloque de 16
//    CMD16  (0x50) SET_BLOCKLEN              -> R1 = 0x00
//    CMD17  (0x51) READ_SINGLE_BLOCK         -> R1 + 0xFE + 512 B + CRC
//    CMD18  (0x52) READ_MULTIPLE_BLOCK       -> R1 + 0xFE + 512 B ...
//    CMD24  (0x58) WRITE_BLOCK               -> R1, luego 0xFE + datos
//    CMD25  (0x59) WRITE_MULTIPLE_BLOCK      -> idem
//    CMD55  (0x77) APP_CMD                   -> R1 = 0x01
//    CMD58  (0x7A) READ_OCR                  -> R1 + OCR (bit30=CCS)
//    ACMD41 (0x69) SD_SEND_OP_COND           -> R1 = 0x00 cuando lista
//    ACMD41 con HCS: inicializa como SDHC
// ============================================================================

SdCardImpl::~SdCardImpl() {
  if (!img_.empty()) save();
  if (file_) {
    std::fclose(static_cast<FILE*>(file_));
    file_ = nullptr;
  }
}

static void put16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static uint16_t get16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

// ---------------------------------------------------------------------------
//  Gestión de la imagen
// ---------------------------------------------------------------------------

bool SdCardImpl::open(const std::string& path, uint32_t size_mb) {
  path_ = path;
  if (file_) {
    std::fclose(static_cast<FILE*>(file_));
    file_ = nullptr;
  }
  FILE* f = std::fopen(path.c_str(), "rb");
  if (f) {
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    img_.resize((size_t)n);
    if (n > 0 && std::fread(img_.data(), 1, (size_t)n, f) != (size_t)n) {
      std::fprintf(stderr, "SD: lectura incompleta de %s\n", path.c_str());
    }
    std::fclose(f);
    if (!mount()) {
      std::fprintf(stderr, "SD: imagen sin FAT16 válido, reformateando\n");
      format(size_mb);
    }
    return true;
  }

  format(size_mb);
  return save();
}

bool SdCardImpl::save() {
  if (img_.empty() || path_.empty()) return false;
  FILE* f = static_cast<FILE*>(file_);
  bool own = false;
  if (!f) {
    f = std::fopen(path_.c_str(), "wb");
    if (!f) return false;
    own = true;
  } else {
    std::fseek(f, 0, SEEK_SET);
  }
  std::fwrite(img_.data(), 1, img_.size(), f);
  std::fflush(f);
  if (own) std::fclose(f);
  dirty_ = false;
  return true;
}

// Escribe un solo sector en disco sin reescribir toda la imagen.
//
// IMPORTANTE: el fichero se mantiene abierto entre escrituras. En Windows,
// abrir y cerrar el fichero en cada sector (32 KB/s durante un SAVE) bloquea el
// hilo de emulación varios ms cada vez, y eso corta el audio.
void SdCardImpl::flush_sector(uint32_t sector) {
  if (img_.empty() || path_.empty() || sector >= img_.size() / 512) return;
  if (!file_) {
    file_ = std::fopen(path_.c_str(), "r+b");
    if (!file_) return;
  }
  FILE* f = static_cast<FILE*>(file_);
  std::fseek(f, (long)sector * 512, SEEK_SET);
  std::fwrite(img_.data() + (size_t)sector * 512, 1, 512, f);
  // Volcar al sistema operativo (sin cerrar) para no perder datos, sin la
  // penalización de cerrar/abrir el fichero.
  std::fflush(f);
}

// ---------------------------------------------------------------------------
//  Formateo MicroFS
// ---------------------------------------------------------------------------
//  Formato (compatible con microfs-6502-cc65):
//    Sector 0: tabla de archivos (512 B)
//      bytes 0-1   : magic "FM" (0x46, 0x4D)
//      bytes 6-7   : proximo sector libre (little-endian)
//      bytes 16-527: 16 entradas de 32 bytes
//        +0..11  nombre (null-terminated, 8.3)
//        +12..13 sector inicial (LE)
//        +14..15 tamano en bytes (LE)
//        +16..17 sectores reservados (LE)
//    Sector 1+: datos de archivos (contiguos)
// ---------------------------------------------------------------------------

void SdCardImpl::format(uint32_t size_mb) {
  sectors_ = size_mb * 1024 * 2; // 512 B por sector
  spc_ = 1;
  fat_size_ = 0;
  num_fats_ = 0;
  reserved_ = 1;
  root_entries_ = 0;
  root_dir_sectors_ = 0;
  data_start_ = 1;
  clusters_ = sectors_ - 1;

  img_.assign((size_t)sectors_ * 512, 0);
  mounted_ = true;

  // Sector 0: tabla de archivos MicroFS
  uint8_t* tab = img_.data();
  tab[0] = 'F';
  tab[1] = 'M';
  put16(tab + 6, 1); // proximo sector libre = 1
  // Entradas de 32 bytes desde +16; todas a cero (nombre vacio).

  dirty_ = true;
  std::printf("SD: imagen MicroFS formateada, %u MB (%u sectores)\n",
              size_mb, sectors_);
}

bool SdCardImpl::mount() {
  if (img_.size() < 1024) return false;
  const uint8_t* tab = img_.data();
  // Validar una tabla MicroFS ya existente (magic "FM"). Si no, es valida
  // igualmente: el monitor decidira si formatear.
  if (tab[0] == 'F' && tab[1] == 'M') {
    std::printf("SD: tabla MicroFS detectada (siguiente sector libre=%u)\n",
                get16(tab + 6));
  } else {
    std::printf("SD: imagen sin MicroFS\n");
  }
  sectors_ = (uint32_t)(img_.size() / 512);
  mounted_ = true;
  return true;
}

// ---------------------------------------------------------------------------
//  Gestión de archivos en caliente (MicroFS)
// ---------------------------------------------------------------------------
//  Mismo formato que tools/sd_tool.py: 16 entradas de 32 bytes en el sector 0
//  (nombre 8.3 terminado en NUL, sector inicial, tamaño y sectores ocupados),
//  y el "proximo sector libre" en los bytes 6-7.

namespace {

constexpr int kEntradaBytes = 32;
constexpr int kTablaOffset = 16;

// Normaliza a 8.3 en mayúsculas, como el monitor. Devuelve "" si no vale.
std::string normalizar_nombre(const std::string& in) {
  std::string base = in, ext;
  const size_t punto = in.find_last_of('.');
  if (punto != std::string::npos) {
    base = in.substr(0, punto);
    ext = in.substr(punto + 1);
  }
  if (base.empty() || base.size() > 8 || ext.size() > 3) return "";
  std::string out;
  for (char c : base) {
    if (!(c >= ' ' && c < 127) || c == '.' || c == '\\' || c == '/') return "";
    out.push_back((char)((c >= 'a' && c <= 'z') ? c - 32 : c));
  }
  if (!ext.empty()) {
    out.push_back('.');
    for (char c : ext) {
      if (!(c >= ' ' && c < 127) || c == '.' || c == '\\' || c == '/') return "";
      out.push_back((char)((c >= 'a' && c <= 'z') ? c - 32 : c));
    }
  }
  return out;
}

}  // namespace

uint32_t SdCardImpl::espacio_libre() const {
  if (!mounted_ || img_.size() < 512) return 0;
  const uint32_t libre = get16(img_.data() + 6);
  if (libre >= sectors_) return 0;
  return (sectors_ - libre) * 512;
}

std::vector<SdCardImpl::Entrada> SdCardImpl::listar_archivos() const {
  std::vector<Entrada> out;
  if (!mounted_ || img_.size() < 512) return out;
  for (int i = 0; i < MAX_ENTRADAS; i++) {
    const uint8_t* e = img_.data() + kTablaOffset + i * kEntradaBytes;
    if (e[0] == 0) continue;
    Entrada ent;
    char nombre[13] = {0};
    std::memcpy(nombre, e, 12);
    ent.nombre = nombre;
    ent.sector = get16(e + 12);
    ent.bytes = get16(e + 14);
    ent.sectores = get16(e + 16);
    out.push_back(ent);
  }
  return out;
}

bool SdCardImpl::agregar_archivo(const std::string& nombre_in,
                                 const std::vector<uint8_t>& datos,
                                 std::string* error) {
  auto fallo = [&](const char* msg) {
    if (error) *error = msg;
    return false;
  };
  if (!mounted_ || img_.size() < 1024) return fallo("no hay tarjeta montada");

  const std::string nombre = normalizar_nombre(nombre_in);
  if (nombre.empty()) return fallo("nombre no valido (formato 8.3)");

  // Si ya existe, se borra antes (como hace SAVE del monitor).
  borrar_archivo(nombre, nullptr);

  // Slot libre en la tabla.
  int slot = -1;
  for (int i = 0; i < MAX_ENTRADAS; i++) {
    if (img_[kTablaOffset + i * kEntradaBytes] == 0) {
      slot = i;
      break;
    }
  }
  if (slot < 0) return fallo("tabla llena (16 archivos)");

  const uint32_t sectores =
      datos.empty() ? 1 : (uint32_t)((datos.size() + 511) / 512);
  const uint32_t inicio = get16(img_.data() + 6);
  if (inicio + sectores > sectors_) return fallo("no cabe en la imagen");

  // Datos.
  const size_t off = (size_t)inicio * 512;
  std::memcpy(img_.data() + off, datos.data(), datos.size());
  const size_t fin = off + (size_t)sectores * 512;
  std::memset(img_.data() + off + datos.size(), 0, fin - off - datos.size());

  // Entrada de la tabla.
  uint8_t* e = img_.data() + kTablaOffset + slot * kEntradaBytes;
  std::memset(e, 0, kEntradaBytes);
  std::memcpy(e, nombre.data(), nombre.size());
  put16(e + 12, (uint16_t)inicio);
  put16(e + 14, (uint16_t)datos.size());
  put16(e + 16, (uint16_t)sectores);

  // Nuevo sector libre.
  put16(img_.data() + 6, (uint16_t)(inicio + sectores));

  // Persistencia: tabla y datos.
  for (uint32_t s = inicio; s < inicio + sectores; s++) flush_sector(s);
  flush_sector(0);
  return true;
}

bool SdCardImpl::renombrar_archivo(const std::string& viejo_in,
                                   const std::string& nuevo_in,
                                   std::string* error) {
  auto fallo = [&](const char* msg) {
    if (error) *error = msg;
    return false;
  };
  if (!mounted_ || img_.size() < 1024) return fallo("no hay tarjeta montada");
  const std::string viejo = normalizar_nombre(viejo_in);
  const std::string nuevo = normalizar_nombre(nuevo_in);
  if (viejo.empty() || nuevo.empty()) return fallo("nombre no valido");

  // No se permite duplicar un nombre existente.
  uint8_t* entrada = nullptr;
  for (int i = 0; i < MAX_ENTRADAS; i++) {
    uint8_t* e = img_.data() + kTablaOffset + i * kEntradaBytes;
    if (e[0] == 0) continue;
    char actual[13] = {0};
    std::memcpy(actual, e, 12);
    const std::string nombre(actual);
    if (nombre == viejo) entrada = e;
    if (nombre == nuevo && nombre != viejo) return fallo("ya existe un archivo con ese nombre");
  }
  if (!entrada) return fallo("no existe");
  if (viejo == nuevo) return true;

  // Se reescribe solo el campo nombre; sector y tamano no cambian.
  std::memset(entrada, 0, 12);
  std::memcpy(entrada, nuevo.data(), nuevo.size());
  flush_sector(0);
  return true;
}

bool SdCardImpl::borrar_archivo(const std::string& nombre_in, std::string* error) {
  auto fallo = [&](const char* msg) {
    if (error) *error = msg;
    return false;
  };
  if (!mounted_ || img_.size() < 1024) return fallo("no hay tarjeta montada");
  const std::string nombre = normalizar_nombre(nombre_in);
  if (nombre.empty()) return fallo("nombre no valido");

  for (int i = 0; i < MAX_ENTRADAS; i++) {
    uint8_t* e = img_.data() + kTablaOffset + i * kEntradaBytes;
    if (e[0] == 0) continue;
    char actual[13] = {0};
    std::memcpy(actual, e, 12);
    if (nombre != actual) continue;
    std::memset(e, 0, kEntradaBytes);
    flush_sector(0);
    return true;
  }
  return fallo("no existe");
}

// ---------------------------------------------------------------------------
//  Protocolo SPI
// ---------------------------------------------------------------------------

uint8_t SdCardImpl::spi_byte(uint8_t mosi) {
  // --- Respuesta R1 pendiente (byte t1) y bytes extra de R7/R3 ---
  // Va PRIMERO: tras un comando (p.ej. CMD24) el host espera el R1 antes
  // de entrar en la fase de datos.
  if (r1_pending_) {
    r1_pending_ = false;
    if (r1_more_count_ == 0 && rpos_ < rbuf_.size()) reading_ = true;
    return r1_;
  }
  if (r1_more_count_ > 0) {
    uint8_t v = rbuf_[rpos_++];
    r1_more_count_--;
    if (r1_more_count_ == 0 && rpos_ < rbuf_.size()) reading_ = true;
    return v;
  }

  // --- Envío de un bloque de datos (CMD24/CMD25) ---
  if (writing_) {
    // Antes de los datos el host envía el token 0xFE.
    if (!token_seen_) {
      if (trace_bytes_) std::printf("SD> esperando token, MOSI=%02X\n", mosi);
      if (mosi == 0xFE) token_seen_ = true;
      return 0xFF;
    }
    rbuf_[wpos_++] = mosi;
    if (trace_bytes_ && wpos_ <= 2)
      std::printf("SD> dato[%zu]=%02X\n", wpos_ - 1, mosi);
    if (wpos_ == 512) {
      // Bloque completo: escribir la imagen. El driver envía después 2 bytes
      // de CRC y luego lee el data response, así que aquí solo se cierra el
      // bloque.
      if ((size_t)write_sector_ * 512 < img_.size()) {
        std::memcpy(img_.data() + (size_t)write_sector_ * 512, rbuf_.data(), 512);
        dirty_ = true;
        // Persistir inmediatamente: si el emulador termina de forma abrupta
        // no se pierde nada, y la imagen queda usable por sd_tool.py.
        flush_sector(write_sector_);
        if (trace_bytes_)
          std::printf("SD> sector %u escrito: %02X %02X %02X %02X ... %02X %02X\n",
                      write_sector_, rbuf_[0], rbuf_[1], rbuf_[2], rbuf_[3],
                      rbuf_[16], rbuf_[17]);
      }
      writing_ = false;
      token_seen_ = false;
      reading_ = false;
      rpos_ = 0;
      rbuf_.clear();
      write_crc_left_ = 2;      // los 2 bytes de CRC del host se ignoran
      data_resp_pending_ = true; // y después se entrega el data response
      return 0xFF;
    }
    return 0xFF;
  }

  // Bytes de CRC tras el bloque (el host los envía tras los datos).
  if (write_crc_left_ > 0) {
    write_crc_left_--;
    return 0xFF;
  }
  // Data response: (status & 0x1F) == 0x05 significa aceptado.
  if (data_resp_pending_) {
    data_resp_pending_ = false;
    write_busy_ = true;
    return 0x05;
  }
  // Busy: la tarjeta responde 0x00 mientras escribe y 0xFF al terminar.
  if (write_busy_) {
    if (mosi == 0xFF) {
      write_busy_ = false;
      return 0xFF; // escritura terminada
    }
    write_busy_ = false;
  }

  // --- Lectura de un bloque de datos ---
  if (reading_) {
    if (rpos_ < rbuf_.size()) return rbuf_[rpos_++];
    reading_ = false;
    return 0xFF;
  }

  // --- Recepción de comandos: 0x40|cmd + 4 bytes arg + CRC ---
  // IMPORTANTE: un comando sólo se reconoce cuando NO hay una lectura en
  // curso. Durante un bloque de datos hay bytes ASCII que coinciden con
  // 0x40-0x7F ('F', 'M', 'T'...) y no deben interpretarse como comandos.
  if (cmd_bytes_ == 0) {
    if ((mosi & 0xC0) == 0x40) { // byte de comando (01xxxxxx)
      cmd_ = (uint8_t)(mosi & 0x3F);
      arg_ = 0;
      cmd_bytes_ = 1;
    }
    return 0xFF;
  }

  if (cmd_bytes_ <= 4) { // bytes 1..4 = argumento (MSB primero)
    arg_ = (arg_ << 8) | mosi;
    cmd_bytes_++;
    return 0xFF;
  }

  // cmd_bytes_ == 5: byte de CRC -> ejecutar comando. La respuesta R1 sale
  // en el SIGUIENTE byte (t1), como en una tarjeta real.
  cmd_bytes_ = 0;
  prepare_command(cmd_, arg_);
  return 0xFF;
}

void SdCardImpl::prepare_command(uint8_t cmd, uint32_t arg) {
  if (cmd == 55) { // APP_CMD
    app_cmd_ = true;
    r1_pending_ = true;
    r1_ = 0x01;
    rbuf_.clear();
    rpos_ = 0;
    r1_more_count_ = 0;
    if (trace_) std::printf("SD< CMD55 (APP)\n");
    return;
  }

  bool is_acmd = app_cmd_;
  app_cmd_ = false;
  if (trace_)
    std::printf("SD< %s%u arg=%08X\n", is_acmd ? "ACMD" : "CMD", cmd, arg);

  rbuf_.clear();
  rpos_ = 0;
  r1_more_count_ = 0;
  // Al iniciar un comando nuevo se purga cualquier estado de transferencia
  // pendiente (data response, busy, lectura o escritura en curso).
  data_resp_pending_ = false;
  write_busy_ = false;
  reading_ = false;
  writing_ = false;
  token_seen_ = false;

  // Por defecto: R1 = 0x00 si inicializada, 0x01 si no.
  r1_ = initialized_ok_ ? 0x00 : 0x01;

  switch (cmd) {
    case 0: // GO_IDLE_STATE
      initialized_ok_ = false;
      sdhc_ = false;
      r1_ = 0x01;
      break;

    case 8: // SEND_IF_COND -> R7: R1 + 4 bytes de eco
      if (!mounted_) { r1_ = 0x05; break; }
      r1_ = 0x01;
      rbuf_.push_back(0x00);
      rbuf_.push_back(0x00);
      rbuf_.push_back((uint8_t)(arg >> 8));
      rbuf_.push_back((uint8_t)(arg & 0xFF));
      r1_more_count_ = 4;
      break;

    case 41: // ACMD41
      if (!is_acmd) { r1_ = 0x05; break; }
      if (arg & 0x40000000) sdhc_ = true;
      initialized_ok_ = true;
      r1_ = 0x00;
      break;

    case 58: // READ_OCR -> R3: R1 + 4 bytes OCR
      r1_ = initialized_ok_ ? 0x00 : 0x01;
      rbuf_.push_back(0x40);           // OCR[31] power up
      rbuf_.push_back(0x00);
      rbuf_.push_back(0x80);           // voltaje 3.3V
      rbuf_.push_back(sdhc_ ? 0xFF : 0x00); // bit30 CCS
      r1_more_count_ = 4;
      break;

    case 16: // SET_BLOCKLEN
      r1_ = 0x00;
      break;

    case 9:  // SEND_CSD
    case 10: // SEND_CID
      r1_ = 0x00;
      rbuf_.push_back(0xFE); // token de datos
      for (int i = 0; i < 16; i++) rbuf_.push_back(0x00);
      rbuf_.push_back(0xFF);
      rbuf_.push_back(0xFF);
      break;

    case 17: // READ_SINGLE_BLOCK
    case 18: { // READ_MULTIPLE_BLOCK
      if (!initialized_ok_ || img_.empty()) { r1_ = 0x04; break; }
      uint32_t sector = sdhc_ ? arg : (arg / 512);
      if ((uint64_t)sector * 512 >= img_.size()) { r1_ = 0x40; break; }
      r1_ = 0x00;
      // El driver descarta el primer byte tras el R1 (el token 0xFE) y lee los
      // 512 siguientes. No deben sobrar bytes: al segundo 512 se sale del modo
      // lectura.
      rbuf_.push_back(0xFE);
      const uint8_t* p = img_.data() + (size_t)sector * 512;
      rbuf_.insert(rbuf_.end(), p, p + 512);
      break;
    }

    case 24: // WRITE_BLOCK
    case 25: { // WRITE_MULTIPLE_BLOCK
      if (!initialized_ok_ || img_.empty()) { r1_ = 0x04; break; }
      write_sector_ = sdhc_ ? arg : (arg / 512);
      if ((uint64_t)write_sector_ * 512 >= img_.size()) { r1_ = 0x40; break; }
      r1_ = 0x00;
      rbuf_.assign(512, 0xFF);
      wpos_ = 0;
      token_seen_ = false;
      // Purgar cualquier resto de una lectura anterior.
      reading_ = false;
      rpos_ = 0;
      r1_more_count_ = 0;
      writing_ = true;
      if (trace_bytes_) std::printf("SD> write sector %u iniciado\n", write_sector_);
      break;
    }

    default:
      break;
  }

  r1_pending_ = true;
}

uint8_t SdCardImpl::transfer(uint8_t mosi) {
  uint8_t miso = spi_byte(mosi);
  if (trace_bytes_) {
    static int n = 0;
    if (n < 3000) std::printf("  spi %4d: MOSI=%02X MISO=%02X\n", n, mosi, miso);
    n++;
  }
  return miso;
}
