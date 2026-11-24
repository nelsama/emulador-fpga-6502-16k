#include "spi_sd.h"

// Réplica de spi_master.vhd + spi_wrapper.vhd: modo 0, SCK = 3,375 MHz, cola de 1 byte.

bool SpiSd::decodes(uint16_t addr) const {
  return addr >= 0xC040 && addr <= 0xC047;
}

uint8_t SpiSd::status() const {
  return (uint8_t)(((roe_ || toe_) ? 0x80 : 0) | (rrdy_ ? 0x40 : 0) | (trdy_ ? 0x20 : 0) |
                   (tmt_ ? 0x10 : 0) | (toe_ ? 0x08 : 0) | (roe_ ? 0x04 : 0));
}

uint8_t SpiSd::read(uint16_t addr, bool& resp) {
  resp = decodes(addr);
  switch (addr) {
    case 0xC040: // RX: leerlo borra RRDY
      rrdy_ = false;
      return rx_data_;
    case 0xC041: return tx_data_;
    case 0xC042: return status();
    case 0xC043: return control_;
    case 0xC044: return ss_mask_;
    case 0xC045: return cs_enable_;
    default:     return 0; // $C046-$C047
  }
}

void SpiSd::write(uint16_t addr, uint8_t data, bool& resp) {
  resp = decodes(addr);
  if (!resp) return;
  switch (addr) {
    case 0xC041: // TX: encola un byte; si ya había otro lo reemplaza y pone TOE
      tx_data_ = data;
      pend_ = true;
      if (!trdy_) toe_ = true;
      trdy_ = false;
      break;
    case 0xC043: control_ = (uint8_t)(data & 0x0F); break; // sin efecto
    case 0xC044:
      ss_mask_ = (uint8_t)(data & 0x0F);
      if (!sd_selected() && sd_) sd_->deselect();
      break;
    case 0xC045:
      cs_enable_ = (uint8_t)(data & 0x01);
      if (!sd_selected() && sd_) sd_->deselect();
      break;
    default: break;
  }
}

void SpiSd::reset() {
  SdCard* sd = sd_;
  *this = SpiSd();
  sd_ = sd;
  if (sd_) sd_->deselect();
}

// 2 pulsos de 6,75 MHz por ciclo de CPU.
void SpiSd::tick(uint32_t cpu_cycles) {
  for (uint32_t n = cpu_cycles * 2; n > 0; n--) {
    if (cnt_ == 0) {
      if (!pend_) {
        tmt_ = true;
        return; // en reposo no cambia nada más
      }
      cnt_ = 1;
      tmt_ = false;
      continue;
    }
    if (cnt_ == 1) pend_ = false;
    if (cnt_ == 3) { // carga: la cola queda libre
      sh_tx_ = tx_data_;
      trdy_ = true;
    } else if (cnt_ == 20) { // fin: los 8 bits ya han salido y entrado
      // MISO flota a 1 sin tarjeta o sin CS0.
      rx_data_ = (sd_ && sd_selected()) ? sd_->transfer(sh_tx_) : 0xFF;
      if (rrdy_) roe_ = true;
      rrdy_ = true;
    }
    cnt_ = (cnt_ == 23) ? 0 : cnt_ + 1;
  }
}
