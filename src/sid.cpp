#include "sid.h"

#include <cstring>
#include <memory>

#include "residfp_SID.h"
#include "residfp/residfp_defs.h" // reSIDfp::MOS6581, SamplingMethod

// Tabla de registros de reSIDfp: $D41D-$D41F son extensiones de este hardware
// para el VU meter, y se derivan de los envelopes.
namespace {

constexpr double kSidClock = 964285.714; // 27 MHz / 28 (clk_1mhz de Board.vhd)
constexpr double kSampleRate = 48000.0;

} // namespace

struct Sid::Impl {
  std::unique_ptr<reSIDfp::SID> sid;
  int16_t sample = 0;
};

Sid::Sid() : impl_(new Impl()) {
  impl_->sid.reset(new reSIDfp::SID());
  impl_->sid->setChipModel(reSIDfp::MOS6581);
  impl_->sid->setSamplingParameters(kSidClock, reSIDfp::RESAMPLE, kSampleRate);
  reset();
}

Sid::~Sid() { delete impl_; }

void Sid::reset() {
  std::memset(regs_, 0, sizeof(regs_));
  if (impl_ && impl_->sid) {
    impl_->sid->reset();
    impl_->sid->setChipModel(reSIDfp::MOS6581);
    impl_->sid->setSamplingParameters(kSidClock, reSIDfp::RESAMPLE, kSampleRate);
  }
}

bool Sid::decodes(uint16_t addr) const {
  return addr >= 0xD400 && addr <= 0xD41F;
}

uint8_t Sid::read(uint16_t addr, bool& resp) {
  // IMPORTANTE: solo responder dentro del rango $D400-$D41F; el bus 6502
  // recorre los dispositivos en orden y el primero que responde se queda la
  // lectura.
  if (!decodes(addr)) {
    resp = false;
    return 0;
  }
  resp = true;
  const uint8_t reg = (uint8_t)(addr & 0x1F);

  switch (reg) {
    case 0x19: // POT_X (no conectado)
    case 0x1A: // POT_Y
      return 0xFF;
    case 0x1B: // OSC3 / random
    case 0x1C: // ENV3
      return impl_->sid->read(reg);
    case 0x1D: // ENV1 — extensión VU meter
      return impl_->sid->readEnvelope(0);
    case 0x1E: // ENV2 — extensión VU meter
      return impl_->sid->readEnvelope(1);
    case 0x1F: { // ENV_MAX — extensión VU meter
      const uint8_t e1 = impl_->sid->readEnvelope(0);
      const uint8_t e2 = impl_->sid->readEnvelope(1);
      const uint8_t e3 = impl_->sid->readEnvelope(2);
      uint8_t m = e1 > e2 ? e1 : e2;
      return m > e3 ? m : e3;
    }
    default:
      return regs_[reg];
  }
}

void Sid::write(uint16_t addr, uint8_t data, bool& resp) {
  if (!decodes(addr)) {
    resp = false;
    return;
  }
  resp = true;
  const uint8_t reg = (uint8_t)(addr & 0x1F);
  if (reg > 0x18) return; // $D419-$D41F son de solo lectura

  regs_[reg] = data;
  impl_->sid->write(reg, data);
}

int Sid::clock(int cycles, int16_t* out, int max_out) {
  if (!out || max_out <= 0 || cycles <= 0) return 0;

  // El resampler interno de reSIDfp convierte del reloj del chip (985 kHz) a
  // la frecuencia de salida, entregando sus muestras en `out`.
  //
  // NOTA: reSIDfp escribe en su propio buffer interno y devuelve cuántas
  // muestras nuevas hay. Se procesan TODOS los ciclos pedidos (si no, el reloj
  // del SID se quedaría corto y la música se desincronizaría de la CPU).
  const int n = impl_->sid->clock((unsigned)cycles, out);
  const int produced = n < max_out ? n : max_out;
  if (produced > 0) impl_->sample = out[produced - 1];
  return produced;
}

double Sid::cycle_correction() {
  // reSIDfp no produce exactamente la frecuencia de muestreo por segundo de
  // reloj: con 964285,714 Hz y 48 kHz entrega 48053 muestras/s (un 0,11 % de
  // más). Ese excedente llena el anillo y deja sin datos al dispositivo a
  // ratos (underruns), lo que separa el sonido de la imagen.
  //
  // Se compensa generando un 0,11 % menos de ciclos de SID por bloque. El SID
  // sigue a su reloj real (la afinación no cambia); solo se ajusta cuánto
  // avanza para producir justo `sample_rate` muestras por segundo.
  return 48000.0 / 48053.0;
}
