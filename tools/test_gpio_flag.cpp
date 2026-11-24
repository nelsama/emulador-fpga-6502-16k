// Prueba del registro $C004 (flag de usuario persistente) contra el
// comportamiento del Board.vhd:
//
//   - Solo existe el bit 0; los bits 1-7 se escriben ignorados y se leen como 0
//     (flags_in => "0000000" & flag_user).
//   - Es R/W.
//   - NO se borra con el reset del sistema (el flip-flop no está en el dominio
//     del reset); solo se pierde al recargar el bitstream.
//
// Compilar y ejecutar:
//   make test
#include <cstdio>

#include "../src/gpio_uart.h"

static int fallos = 0;

static void comprobar(const char* nombre, uint8_t obtenido, uint8_t esperado) {
  if (obtenido == esperado) {
    std::printf("  OK    %s\n", nombre);
  } else {
    std::printf("  FALLO %s (obtenido %02X, esperado %02X)\n", nombre, obtenido,
                esperado);
    fallos++;
  }
}

// Lee un registro como lo haría la CPU.
static uint8_t leer(Gpio& g, uint16_t dir) {
  bool resp = false;
  return g.read(dir, resp);
}

// Escribe un registro como lo haría la CPU.
static void escribir(Gpio& g, uint16_t dir, uint8_t v) {
  bool resp = false;
  g.write(dir, v, resp);
}

int main() {
  Gpio g;

  // 1. Arranca a 0.
  comprobar("arranca a 0", leer(g, 0xC004), 0x00);

  // 2. Escribir 1 y leer.
  escribir(g, 0xC004, 0x01);
  comprobar("escribe 01", leer(g, 0xC004), 0x01);

  // 3. Los bits 1-7 se ignoran al escribir.
  escribir(g, 0xC004, 0xFF);
  comprobar("FF deja solo el bit 0", leer(g, 0xC004), 0x01);

  // 4. Escribir 0 lo apaga (aunque vengan bits altos).
  escribir(g, 0xC004, 0xFE);
  comprobar("FE apaga el bit 0", leer(g, 0xC004), 0x00);

  // 5. Los bits altos se leen como 0.
  escribir(g, 0xC004, 0x01);
  comprobar("los bits 1-7 leen 0", leer(g, 0xC004), 0x01);

  // 6. Persiste tras un reset de la placa (no se toca el flag).
  g.reset_placa();
  comprobar("sobrevive al reset", leer(g, 0xC004), 0x01);

  // 7. No interfiere con los puertos: escribir en $C000-$C003 no lo cambia.
  escribir(g, 0xC000, 0x00);
  escribir(g, 0xC002, 0xFF);
  comprobar("no lo tocan otros registros", leer(g, 0xC004), 0x01);

  // 8. Pulsadores $C005: activos en bajo, solo lectura, bits 7-2 a 0.
  comprobar("C005 en reposo", leer(g, 0xC005), 0x03);
  g.set_botones((uint8_t)~Gpio::BTN_A);
  comprobar("C005 con BTN_A", leer(g, 0xC005), 0x02);
  g.set_botones((uint8_t)~Gpio::BTN_B);
  comprobar("C005 con BTN_B", leer(g, 0xC005), 0x01);
  escribir(g, 0xC005, 0xFF);
  comprobar("C005 ignora escrituras", leer(g, 0xC005), 0x01);
  comprobar("C005 no toca el flag", leer(g, 0xC004), 0x01);

  // 9. El decodificador acepta $C004 y $C005, y no $C006.
  comprobar("decodifica $C004", (uint8_t)g.decodes(0xC004), 1);
  comprobar("decodifica $C005", (uint8_t)g.decodes(0xC005), 1);
  comprobar("no decodifica $C006", (uint8_t)g.decodes(0xC006), 0);

  std::printf(fallos ? "\n%d FALLO(S)\n" : "\nTodo correcto\n", fallos);
  return fallos ? 1 : 0;
}
