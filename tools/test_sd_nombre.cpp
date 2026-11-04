// Prueba del recorte de nombres a 8.3 al añadir un archivo a la SD.
//
// El nombre de la tarjeta MicroFS es 8.3. Antes, un archivo con nombre más largo
// se RECHAZABA entero ("nombre no valido"), así que al añadir desde la ventana un
// .bin con nombre de Windows largo no se cargaba nada. Ahora se recorta.
#include <cstdio>
#include <string>
#include <vector>

#include "sd_card.h"

static int fallos = 0;
static void check(bool ok, const std::string& que) {
  std::printf("  %s  %s\n", ok ? "OK  " : "FALLO", que.c_str());
  if (!ok) fallos++;
}

// Añade con `nombre` y comprueba que en la tarjeta queda `esperado`.
// Si `esperado` es nullptr, el nombre debe RECHAZARSE.
static void caso(SdCardImpl& sd, const char* nombre, const char* esperado) {
  std::vector<uint8_t> datos(16, 0x41);
  std::string err;
  const bool r = sd.agregar_archivo(nombre, datos, &err);

  if (!esperado) {
    check(!r, std::string("rechaza '") + nombre + "'");
    return;
  }
  if (!r) {
    check(false, std::string("acepta '") + nombre + "' (" + err + ")");
    return;
  }
  bool aparece = false;
  for (const auto& e : sd.listar_archivos())
    if (e.nombre == esperado) aparece = true;
  check(aparece, std::string("'") + nombre + "' queda como '" + esperado + "'");
}

int main() {
  std::printf("test_sd_nombre: recorte a 8.3\n");
  const std::string img = "test_sd_nombre.img";
  std::remove(img.c_str());

  SdCardImpl sd;
  if (!sd.open(img, 4)) {
    std::printf("  FALLO  no se pudo crear la imagen\n");
    return 1;
  }

  caso(sd, "PACMAN.BIN", "PACMAN.BIN");            // cabe: igual
  caso(sd, "RIVERRIVER.BIN", "RIVERRIV.BIN");      // base de 10 -> 8
  caso(sd, "MUYLARGO.NOMBREBIN", "MUYLARGO.NOM");  // extensión de 9 -> 3
  caso(sd, "corto.bin", "CORTO.BIN");              // pasa a mayúsculas
  caso(sd, "sin_extension_larga", "SIN_EXTE");     // sin extensión, base larga
  caso(sd, "A.B", "A.B");
  caso(sd, "", nullptr);                           // vacío: se rechaza
  caso(sd, ".BIN", nullptr);                       // sin base: se rechaza
  caso(sd, "UNO/DOS.BIN", nullptr);                // separador de ruta: se rechaza

  std::remove(img.c_str());
  std::printf("test_sd_nombre: %s\n", fallos ? "FALLOS" : "OK");
  return fallos ? 1 : 0;
}
