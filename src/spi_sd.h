#pragma once

#include <cstdint>

#include "device.h"

// Interfaz opcional de "tarjeta SD" emulada. Si se instala, el SpiSd le pasa
// cada byte transmitido y entrega su respuesta por MISO.
class SdCard {
public:
  virtual ~SdCard() = default;
  // Recibe el byte enviado por MOSI y devuelve el byte de MISO.
  virtual uint8_t transfer(uint8_t mosi) = 0;

  // CS desactivado: la tarjeta reinicia su estado de transferencia pero
  // conserva la imagen.
  virtual void deselect() {}
};

// SPI Master + SD Card ($C040-$C047), fiel a spi_wrapper.vhd.
//
//   $C040 RX_DATA  (R)
//   $C041 TX_DATA  (W, inicia transferencia)
//   $C042 STATUS   (R)  ROE(2) TOE(3) TMT(4) TRDY(5) RRDY(6) E(7)
//   $C043 CONTROL  (R/W)
//   $C044 SS_MASK  (R/W)  bit0=CS0 (SD), bit1..3 externos
//   $C045 CS_ENABLE(R/W)  bit0=1 activa los CS seleccionados
//
// Sin tarjeta conectada, MISO devuelve 0xFF (bus flotante con pull-up), que es
// lo que ve el monitor real cuando no hay SD insertada.
class SpiSd : public Device {
public:
  bool decodes(uint16_t addr) const override;
  uint8_t read(uint16_t addr, bool& resp) override;
  void write(uint16_t addr, uint8_t data, bool& resp) override;

  // Conectar una tarjeta SD emulada (puede ser nullptr para "sin tarjeta").
  void attach(SdCard* sd) { sd_ = sd; }

  // Tarjeta conectada (para el servidor de depuración y la ventana).
  SdCard* card() const { return sd_; }

  // ¿La CPU está usando el bus SPI ahora mismo? Si es así, escribir en la
  // tarjeta desde fuera (inyectar un archivo) podría pisar una transferencia
  // en curso: el que escribe fuera espera a que baje.
  bool ocupada() const { return sd_selected(); }

  // ¿Está CS0 (SD) activo?
  bool sd_selected() const { return cs_enable_ && (ss_mask_ & 0x01); }

private:
  uint8_t rx_data_ = 0;
  uint8_t tx_data_ = 0;
  uint8_t status_  = 0x30; // TRDY(5)=0x20 + TMT(4)=0x10: listo para transmitir
  uint8_t control_ = 0;
  uint8_t ss_mask_ = 0;
  uint8_t cs_enable_ = 0;
  SdCard* sd_ = nullptr;
};
