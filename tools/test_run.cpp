// Prueba de sd_card: recorte de nombres a 8.3 y resolución de nombres para
// POST /run.
//
// El recorrido completo de POST /run (reset -> LOAD -> R -> juego corriendo) no
// se puede comprobar aquí: necesita UART y CPU a la vez. Se verificó a mano con
// los juegos de la SD (ver docs/GUIA-AGENTES.md, POST /run).
//
// No necesita SDL ni abre ventana.

#include <cstdio>
#include <string>
#include <vector>

#include "sd_card.h"

static int fallos = 0;

static void comprobar(const char* que, bool ok) {
  std::printf("  %s    %s\n", ok ? "OK" : "FALLO", que);
  if (!ok) fallos++;
}

int main() {
  const std::string img = "test_run.img";
  std::remove(img.c_str());

  SdCardImpl sd;
  if (!sd.open(img, 4)) {
    std::printf("  FALLO  no se pudo crear la imagen\n");
    return 1;
  }

  std::printf("test_run: nombres para POST /run\n");
  std::string err;

  comprobar("nombre que cabe: se usa tal cual",
            sd.resolver_nombre("TETRIS.BIN") == "TETRIS.BIN");

  // Nombre realmente largo (base de 10): el monitor solo conoce el 8.3.
  sd.agregar_archivo("MUYLARGUISIMO.BIN", std::vector<uint8_t>(100, 0x42), &err);
  comprobar("nombre largo: se resuelve al 8.3",
            sd.resolver_nombre("MUYLARGUISIMO.BIN") == "MUYLARGU.BIN");

  // Lo importante del arreglo: el nombre no invade el sector inicial (+12) ni
  // el tamaño (+14) de la entrada. Antes, un nombre de 12 caracteres escribía
  // 12 bytes con memcpy y corrompía la entrada.
  sd.agregar_archivo("PACMAN.BIN", std::vector<uint8_t>(500, 0x61), &err);
  bool intacta = false;
  for (const auto& ent : sd.listar_archivos()) {
    if (ent.nombre == "PACMAN.BIN") intacta = (ent.bytes == 500 && ent.sector > 0);
  }
  comprobar("nombre de 12 (PACMAN.BIN) no corrompe la entrada", intacta);

  // Dos archivos del MISMO tamaño: antes se resolvía por tamaño y pedir
  // PACMAN.BIN podía lanzar TETRIS.BIN.
  sd.agregar_archivo("TETRIS.BIN", std::vector<uint8_t>(500, 0x61), &err);
  comprobar("mismo tamano no confunde los nombres",
            sd.resolver_nombre("TETRIS.BIN") == "TETRIS.BIN" &&
                sd.resolver_nombre("PACMAN.BIN") == "PACMAN.BIN");

  comprobar("nombre inexistente: se devuelve tal cual",
            sd.resolver_nombre("NOESTA.BIN") == "NOESTA.BIN");

  std::printf("test_run: %s\n", fallos ? "FALLO" : "OK");
  std::remove(img.c_str());
  return fallos ? 1 : 0;
}
