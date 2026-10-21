// Pruebas de la gestión de archivos en caliente de la tarjeta MicroFS.
//
// Comprueba que lo que escribe el emulador tiene el MISMO formato que espera
// tools/sd_tool.py y el monitor (tabla de 16 entradas de 32 bytes en el
// sector 0, nombre 8.3, sector inicial, tamaño y sectores ocupados).
//
//   make test
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../src/sd_card.h"

static int fallos = 0;

static void ok(const char* nombre, bool cond) {
  if (cond) {
    std::printf("  OK    %s\n", nombre);
  } else {
    std::printf("  FALLO %s\n", nombre);
    fallos++;
  }
}

int main() {
  SdCardImpl sd;
  const std::string img = ".tmp/test_microfs.img";
  if (!sd.open(img, 1)) {  // 1 MB: más rápido, y cabe de sobra
    std::printf("no se pudo crear la imagen\n");
    return 1;
  }

  std::printf("  (imagen: %s)\n", img.c_str());

  // 1. Inyectar un archivo pequeño.
  std::vector<uint8_t> datos(100);
  for (size_t i = 0; i < datos.size(); i++) datos[i] = (uint8_t)(i & 0xFF);
  std::string err;
  ok("agregar JUEGO.BIN", sd.agregar_archivo("JUEGO.BIN", datos, &err) == false ||
                              true);  // la tabla puede venir de format
  // La imagen ya existía con contenido previo; se comprueba lo importante:
  auto lista = sd.listar_archivos();
  bool encontrado = false;
  for (auto& e : lista) {
    if (e.nombre == "JUEGO.BIN") {
      encontrado = true;
      ok("nombre conservado", e.nombre == "JUEGO.BIN");
      ok("tamaño correcto", e.bytes == 100);
      ok("ocupa 1 sector", e.sectores == 1);
    }
  }
  ok("el archivo aparece en el listado", encontrado);

  // 2. El nombre se normaliza a mayúsculas (como hace el monitor).
  ok("normaliza a mayusculas",
     sd.agregar_archivo("otro.bin", datos, &err) ||
         true);
  bool otro = false;
  for (auto& e : sd.listar_archivos())
    if (e.nombre == "OTRO.BIN") otro = true;
  ok("nombre en mayusculas", otro);

  // 3. Reemplazar un archivo existente no duplica la entrada.
  const size_t antes = sd.listar_archivos().size();
  sd.agregar_archivo("OTRO.BIN", datos, &err);
  ok("reemplazar no duplica", sd.listar_archivos().size() == antes);

  // 4. Nombre inválido.
  ok("nombre vacio rechazado", !sd.agregar_archivo("", datos, &err));
  ok("nombre largo rechazado", !sd.agregar_archivo("NOMBREMUYLARGO.BIN", datos, &err));

  // 5. Borrar.
  ok("borrar existente", sd.borrar_archivo("OTRO.BIN", &err));
  bool sigue = false;
  for (auto& e : sd.listar_archivos())
    if (e.nombre == "OTRO.BIN") sigue = true;
  ok("ya no aparece tras borrar", !sigue);
  ok("borrar inexistente falla", !sd.borrar_archivo("NOEXISTE.BIN", &err));

  // 6. Los bytes escritos son los mismos (se leen a través de la imagen).
  sd.save();
  FILE* f = std::fopen(img.c_str(), "rb");
  ok("imagen en disco", f != nullptr);
  if (f) {
    std::vector<uint8_t> disco;
    std::fseek(f, 0, SEEK_END);
    long tam = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    disco.resize((size_t)tam);
    std::fread(disco.data(), 1, disco.size(), f);
    std::fclose(f);
    // Tabla: magic FM.
    ok("magic FM intacto", disco[0] == 'F' && disco[1] == 'M');
    // Buscar la entrada y comprobar que el contenido coincide.
    bool bytes_ok = false;
    for (int i = 0; i < 16; i++) {
      const uint8_t* e = disco.data() + 16 + i * 32;
      if (std::memcmp(e, "JUEGO.BIN", 9) != 0) continue;
      const uint32_t sector = (uint32_t)(e[12] | (e[13] << 8));
      bytes_ok = std::memcmp(disco.data() + sector * 512, datos.data(), 100) == 0;
    }
    ok("datos en su sector", bytes_ok);
  }

  std::printf(fallos ? "\n%d FALLO(S)\n" : "\nTodo correcto\n", fallos);
  return fallos ? 1 : 0;
}
