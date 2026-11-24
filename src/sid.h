#pragma once

#include <cstdint>
#include <vector>

#include "device.h"

// SID 6581 ($D400-$D41F), wrapper sobre reSIDfp (VICE).
//
// Mapeo:
//   $D400-$D418  registros de escritura (3 voces + filtro + volumen)
//   $D419/$D41A  POT_X / POT_Y (no conectados)
//   $D41B        OSC3 / generador de ruido de la voz 3
//   $D41C        ENV3 (compatible C64)
//   $D41D        ENV1   (extension VU meter de este hardware)
//   $D41E        ENV2   (extension)
//   $D41F        ENV_MAX (extension)
class Sid : public Device {
public:
  Sid();
  ~Sid() override;

  bool decodes(uint16_t addr) const override;
  uint8_t read(uint16_t addr, bool& resp) override;
  void write(uint16_t addr, uint8_t data, bool& resp) override;

  // Acumula tiempo. El chip se pone al día al acceder a un registro o al vaciar el
  // audio, así cada escritura suena en el instante en que la hizo la CPU.
  void tick(uint32_t cpu_cycles) { pendientes_ += cpu_cycles; }

  // Pone el SID al día y entrega las muestras generadas (48 kHz mono).
  void sacar_audio(std::vector<int16_t>& out);

  // Resetea el chip.
  void reset();

private:
  void poner_al_dia();
  uint64_t pendientes_ = 0;       // ciclos de CPU aún no aplicados al SID
  uint32_t resto_ = 0;            // fracción de la conversión CPU -> SID
  std::vector<int16_t> muestras_; // audio generado y no recogido

  struct Impl;
  Impl* impl_ = nullptr;

  // Caché de registros para las lecturas ($D41B-$D41F) cuando reSIDfp no las
  // expone directamente.
  uint8_t regs_[32] = {0};
};
