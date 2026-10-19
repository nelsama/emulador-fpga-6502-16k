#include "spi_sd.h"

// Estado del IP SPI de Gowin (instantáneo):
//   TRDY(5) = buffer de TX listo para un nuevo byte
//   TMT(4)  = registro de desplazamiento vacío
//   RRDY(6) = hay byte recibido en el buffer de RX
// Tras escribir TX: TRDY=0 mientras "transmite". Como aquí es instantáneo,
// el byte ya llegó: RRDY=1 y TRDY vuelve a 1.

bool SpiSd::decodes(uint16_t addr) const {
  return addr >= 0xC040 && addr <= 0xC047;
}

uint8_t SpiSd::read(uint16_t addr, bool& resp) {
  resp = true;
  switch (addr) {
    case 0xC040: // RX_DATA
      return rx_data_;
    case 0xC041: // TX_DATA (lectura devuelve el buffer de TX)
      return tx_data_;
    case 0xC042: // STATUS
      return status_;
    case 0xC043: // CONTROL
      return control_;
    case 0xC044: // SS_MASK (local)
      return (uint8_t)(ss_mask_ & 0x0F);
    case 0xC045: // CS_ENABLE (local)
      return (uint8_t)(cs_enable_ & 0x01);
    default:
      resp = false;
      return 0;
  }
}

void SpiSd::write(uint16_t addr, uint8_t data, bool& resp) {
  resp = true;
  switch (addr) {
    case 0xC041: { // TX_DATA -> inicia transferencia
      tx_data_ = data;

      // MISO: 0xFF si CS0 (SD) no está activo o no hay tarjeta instalada.
      uint8_t miso = 0xFF;
      if (sd_ && sd_selected()) miso = sd_->transfer(tx_data_);

      rx_data_ = miso;

      // Después de la transferencia: TX listo de nuevo, RX disponible.
      //   TRDY(5)=0x20, TMT(4)=0x10, RRDY(6)=0x40  ->  0x70
      status_ = 0x70;
      break;
    }
    case 0xC043: // CONTROL
      control_ = data;
      break;
    case 0xC044: // SS_MASK
      ss_mask_ = (uint8_t)(data & 0x0F);
      break;
    case 0xC045: // CS_ENABLE
      cs_enable_ = (uint8_t)(data & 0x01);
      // Desactivar CS reinicia el estado SPI de la tarjeta (como en el
      // hardware real: la tarjeta vuelve a esperar un comando).
      if (!cs_enable_ && sd_) sd_->deselect();
      break;
    default:
      resp = false;
      break;
  }
}
