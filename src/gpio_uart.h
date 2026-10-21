#pragma once

#include <cstdint>

#include "device.h"

// Puertos GPIO ($C000-$C004), fieles al Board.vhd.
//   $C000 = Puerto 1 (datos, bidireccional)   -> joystick Atari (DB9)
//   $C001 = Puerto 2 (datos, bidireccional)   -> 6 LEDs del Tang Nano 9K
//   $C002 = Config Puerto 1 (0=salida, 1=entrada por bit)
//   $C003 = Config Puerto 2 (0=salida, 1=entrada por bit)
//   $C004 = Flag de usuario persistente (bit 0, R/W; resto de bits = 0)
//
// El flag de $C004 es un unico flip-flop en el FPGA que NO esta conectado al
// reset del sistema: sobrevive a un reset por S1 y solo se pierde al recargar
// el bitstream. El software lo usa para conservar un bit de estado entre
// reinicios. Aqui se mantiene mientras viva el emulador; ver reset de la placa
// mas abajo.
//
// Igual que en el VHDL (port_sync): un bit configurado como SALIDA devuelve el
// valor escrito; uno configurado como ENTRADA devuelve lo que haya en el pin.
// Al arrancar, dir1_/dir2_ valen 0, así que ambos puertos son de salida.
//
// Los LEDs están en el puerto 2 y son ACTIVOS EN BAJO: un bit a 0 enciende el
// LED correspondiente.
//
// --- Joystick Atari en el puerto 1 -----------------------------------------
//
// El joystick ocupa los 5 bits altos. Mapeo tal como lo espera el software
// (ver Doc/JOYSTICK-INTEGRACION.md de Pacman), con el orden de pines de la
// placa Tang Nano 9K: 42,41,35,40,34,33,30,29 para los bits 0..7.
//
//   bit  pin  señal   máscara
//   ---  ---  -----   -------
//    3    40  right   0x08
//    4    34  left    0x10
//    5    33  down    0x20
//    6    30  up      0x40
//    7    29  fire    0x80
//
// El joystick es ACTIVO EN BAJO (0 = pulsado). La salida de joy() usa ese mismo
// formato: bit a 0 = pulsado, y en reposo vale 0xF8.
class Gpio : public Device {
public:
  bool decodes(uint16_t addr) const override;
  uint8_t read(uint16_t addr, bool& resp) override;
  void write(uint16_t addr, uint8_t data, bool& resp) override;

  // Bits del joystick dentro del puerto 1.
  static constexpr uint8_t JOY_RIGHT = 0x08;
  static constexpr uint8_t JOY_LEFT  = 0x10;
  static constexpr uint8_t JOY_DOWN  = 0x20;
  static constexpr uint8_t JOY_UP    = 0x40;
  static constexpr uint8_t JOY_FIRE  = 0x80;
  static constexpr uint8_t JOY_MASK  = 0xF8;
  static constexpr uint8_t JOY_IDLE  = 0xF8; // sin pulsar (activo en bajo)

  // Estado de los 6 LEDs (bit N = 1 -> LED N encendido), ya interpretando la
  // lógica activa en bajo del hardware. Se toman del registro de salida del
  // puerto 2, que es lo que escriben los programas ($C001).
  uint8_t leds() const { return (uint8_t)(~port2_out_ & 0x3F); }

  // Bits configurados como entrada en cada puerto (1 = entrada).
  uint8_t dir1() const { return dir1_; }
  uint8_t dir2() const { return dir2_; }

  // Valor de los pines del puerto 1 (joystick) y del puerto 2 (LEDs).
  uint8_t port1_pins() const { return port1_; }

  // Flag de usuario de $C004 bit 0 (R/W, persistente).
  bool flag_usuario() const { return flag_user_; }
  // Simula el reset por boton S1: NO toca el flag de usuario, igual que en el
  // FPGA (ese flip-flop no esta en el dominio del reset del sistema).
  void reset_placa() {}

  // Lo que devuelve la CPU al leer $C000/$C001: cada bit es el pin si está
  // configurado como entrada, o el registro de salida si es salida.
  uint8_t port1_read() const {
    return (uint8_t)((port1_ & dir1_) | (port1_out_ & (uint8_t)~dir1_));
  }
  uint8_t port2_read() const {
    return (uint8_t)((port2_ & dir2_) | (port2_out_ & (uint8_t)~dir2_));
  }

  // Simula el joystick Atari del puerto 1: `v` en el formato del puerto
  // (0 = pulsado, bits 3-7; en reposo 0xF8). Lo llama la ventana con el teclado
  // o con un mando real de SDL.
  void set_joy(uint8_t v) { port1_ = (uint8_t)((port1_ & ~JOY_MASK) | (v & JOY_MASK)); }

  // Compatibilidad con el nombre anterior (se pasaba una máscara de pulsaciones).
  void set_port1(uint8_t v) { set_joy(v); }

private:
  uint8_t port1_ = JOY_IDLE; // pines del puerto 1 (activo bajo)
  uint8_t port2_ = 0xFF;     // pines del puerto 2 (los LEDs no se leen)
  uint8_t port1_out_ = 0;    // registro de salida del puerto 1
  uint8_t port2_out_ = 0;    // registro de salida del puerto 2
  uint8_t dir1_  = 0;        // 1 = entrada
  uint8_t dir2_  = 0;
  bool    flag_user_ = false; // $C004 bit0: sobrevive al reset del sistema
};
