#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "spi_sd.h"

// Tarjeta SD emulada por SPI (SD v2.0, compatible SDHC y SDSC), con soporte
// del protocolo de comandos que usa el monitor (CMD0, CMD8, CMD55, ACMD41,
// CMD58, CMD17/18 lectura, CMD24/25 escritura) y un sistema de archivos FAT16.
//
// El estado de la tarjeta se guarda en una imagen en memoria; `save()` la
// vuelca a disco para que los cambios persistan.
class SdCardImpl : public SdCard {
public:
  SdCardImpl() = default;
  ~SdCardImpl() override;

  // Crea una imagen nueva vacía (FAT16, tamaño indicado en MB). Si el archivo
  // ya existe, se carga en su lugar.
  bool open(const std::string& path, uint32_t size_mb = 16);

  // Vuelca la imagen a disco.
  bool save();

  // Escribe un único sector en disco (persistencia incremental).
  void flush_sector(uint32_t sector);

  // --- Gestión de archivos en caliente (MicroFS) -------------------------
  //
  // Permiten añadir, listar y borrar archivos de la imagen sin reiniciar el
  // emulador. La tabla está en el sector 0 y se relee en cada comando del
  // monitor, así que el cambio es visible de inmediato.
  //
  // Todos devuelven false si no hay tarjeta montada, y `agregar` si el nombre
  // no es válido, la tabla está llena o no cabe en la imagen.
  struct Entrada {
    std::string nombre;
    uint32_t sector = 0;
    uint32_t bytes = 0;
    uint32_t sectores = 0;
  };

  static constexpr int MAX_ENTRADAS = 16;

  bool agregar_archivo(const std::string& nombre, const std::vector<uint8_t>& datos,
                       std::string* error = nullptr);
  bool borrar_archivo(const std::string& nombre, std::string* error = nullptr);
  // Cambia el nombre de un archivo (no mueve sus sectores).
  bool renombrar_archivo(const std::string& viejo, const std::string& nuevo,
                         std::string* error = nullptr);
  std::vector<Entrada> listar_archivos() const;
  // Lee el contenido de un archivo de la imagen (para verificarlo por HTTP).
  // Devuelve false si no está montada o el archivo no existe.
  bool leer_archivo(const std::string& nombre, std::vector<uint8_t>* datos,
                    std::string* error = nullptr) const;
  // Espacio libre en bytes (desde el sector libre hasta el final).
  uint32_t espacio_libre() const;
  bool montada() const { return mounted_; }

  // Interfaz SPI: recibe MOSI y devuelve MISO.
  uint8_t transfer(uint8_t mosi) override;

  // CS desactivado: se abandona cualquier transferencia en curso.
  void deselect() override {
    cmd_bytes_ = 0;
    reading_ = false;
    writing_ = false;
    r1_pending_ = false;
    r1_more_count_ = 0;
    data_resp_pending_ = false;
    rbuf_.clear();
    rpos_ = 0;
  }

private:
  // Protocolo SPI
  uint8_t spi_byte(uint8_t mosi);
  void prepare_command(uint8_t cmd, uint32_t arg);

  // FAT16
  void format(uint32_t size_mb);
  bool mount();
  uint32_t cluster_to_sector(uint32_t cluster) const { return data_start_ + (cluster - 2) * spc_; }

  // Imagen
  std::vector<uint8_t> img_;
  std::string path_;
  bool dirty_ = false;
  bool mounted_ = false;
  // Fichero abierto en modo escritura. Se mantiene abierto: abrir y cerrar en
  // cada sector escrito bloqueba el hilo de emulación varios ms y cortaba el
  // audio.
  void* file_ = nullptr;

  // Estado del protocolo
  int cmd_bytes_ = 0;      // 0=esperando comando, 1..4=argumento, 5=CRC
  uint8_t cmd_ = 0;
  uint32_t arg_ = 0;
  bool app_cmd_ = false;   // tras CMD55, el siguiente comando es ACMD
  bool sdhc_ = false;      // tarjeta de alta capacidad (bloques de 512 B)
  bool initialized_ok_ = false;
  bool r1_pending_ = false;   // hay un R1 que entregar en el próximo byte
  uint8_t r1_ = 0xFF;
  int r1_more_count_ = 0;     // bytes extra de R7/R3 tras el R1
  bool trace_ = false;
  bool trace_bytes_ = false;
  std::vector<uint8_t> rbuf_;
  size_t rpos_ = 0;
  bool reading_ = false;
  bool writing_ = false;
  bool token_seen_ = false; // se recibió el token 0xFE de un bloque a escribir
  bool data_resp_pending_ = false; // data response 0x05 en el próximo byte
  bool write_busy_ = false;
  int write_crc_left_ = 0;        // bytes de CRC tras el bloque de escritura
  uint32_t write_sector_ = 0;
  size_t wpos_ = 0;

  // Geometría MicroFS
  uint32_t sectors_ = 0;
  uint32_t spc_ = 4; // sin uso en MicroFS (compatibilidad)
  uint32_t reserved_ = 1;
  uint32_t num_fats_ = 2;
  uint32_t root_entries_ = 512;
  uint32_t fat_size_ = 0;       // sectores por FAT
  uint32_t root_dir_sectors_ = 0;
  uint32_t data_start_ = 0;     // primer sector de datos
  uint32_t clusters_ = 0;
};
