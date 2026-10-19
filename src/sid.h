#pragma once

#include <cstdint>

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

  // Avanza `cycles` ciclos del reloj del SID (clk_1mhz ≈ 985248 Hz) y escribe
  // las muestras generadas en `out`. Devuelve el número de muestras.
  int clock(int cycles, int16_t* out, int max_out);

  // Factor para que reSIDfp produzca exactamente `sample_rate` muestras por
  // segundo (ver sid.cpp). Multiplica los ciclos de SID por vuelta.
  static double cycle_correction();

  // Resetea el chip.
  void reset();

private:
  struct Impl;
  Impl* impl_ = nullptr;

  // Caché de registros para las lecturas ($D41B-$D41F) cuando reSIDfp no las
  // expone directamente.
  uint8_t regs_[32] = {0};
};
