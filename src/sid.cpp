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
  poner_al_dia();
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

  if (reg >= 0x1B) poner_al_dia();

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

  poner_al_dia();
  regs_[reg] = data;
  impl_->sid->write(reg, data);
}

void Sid::poner_al_dia() {
  // CPU = 27 MHz / 8 y SID = 27 MHz / 28: el SID da 2 ciclos por cada 7 de CPU.
  if (!impl_ || !impl_->sid || pendientes_ == 0) return;
  const uint64_t n = pendientes_ * 2 + resto_;
  pendientes_ = 0;
  resto_ = (uint32_t)(n % 7);
  unsigned ciclos = (unsigned)(n / 7);
  int16_t buf[512];
  while (ciclos > 0) {
    const unsigned parte = ciclos > 2048 ? 2048 : ciclos; // ~102 muestras
    const int m = impl_->sid->clock(parte, buf);
    muestras_.insert(muestras_.end(), buf, buf + m);
    ciclos -= parte;
  }
  // Nadie recoge el audio (p. ej. en las pruebas): no crecer sin límite.
  if (muestras_.size() > 96000) muestras_.clear();
}

void Sid::sacar_audio(std::vector<int16_t>& out) {
  poner_al_dia();
  out.clear();
  out.swap(muestras_);
}
