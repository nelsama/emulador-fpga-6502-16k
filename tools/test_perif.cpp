// Pruebas de CPU, timer, UART, SD y reset de la placa contra el RTL.
//
// La CPU se compara a fondo con el VHDL en tools/cosim_cpu (make cosim); aquí quedan
// los casos que no necesitan GHDL.
#include <cstdio>
#include <string>
#include <vector>

#include "bus.h"
#include "cpu_6502.h"
#include "gpio_uart.h"
#include "ram_rom.h"
#include "sd_card.h"
#include "sid.h"
#include "spi_sd.h"
#include "timer.h"
#include "uart.h"
#include "video/video.h"

static int fallos = 0;
static void check(bool ok, const char* que) {
  if (ok) return;
  std::printf("  FALLO: %s\n", que);
  fallos++;
}

struct Maquina {
  Bus bus;
  Cpu6502 cpu{bus};
  explicit Maquina(const std::vector<uint8_t>& prog) {
    std::vector<uint8_t> rom(16384, 0xEA);
    rom[0x3FFC] = 0x00; rom[0x3FFD] = 0x02; // RESET -> $0200
    rom[0x3FFE] = 0x00; rom[0x3FFF] = 0x10; // IRQ   -> $1000
    bus.ram_rom()->load_rom(rom.data(), rom.size());
    for (size_t i = 0; i < prog.size(); i++) m((int)(0x200 + i)) = prog[i];
    cpu.reset();
  }
  uint8_t& m(int a) { return bus.ram_rom()->ram()[a]; }
  int run(int n) {
    int t = 0;
    while (n--) {
      cpu.step();
      t += cpu.last_cycles();
      bus.tick((uint32_t)cpu.last_cycles());
    }
    return t;
  }
};

static void test_cpu() {
  { // LDA (zp,X)
    Maquina k({0xA2, 4, 0xA1, 0x10});
    k.m(0x14) = 0; k.m(0x15) = 3; k.m(0x300) = 0x77;
    k.run(2);
    check(k.cpu.a() == 0x77, "cpu: (zp,X) suma X antes de leer el puntero");
  }
  { // ASL abs,X
    Maquina k({0xA2, 1, 0x1E, 0x00, 0x03});
    k.m(0x301) = 1; k.m(1) = 0x40;
    k.run(2);
    check(k.m(0x301) == 2 && k.m(1) == 0x40, "cpu: ASL abs,X no recorta la direccion");
  }
  { // decimal
    Maquina k({0xF8, 0x18, 0xA9, 0x19, 0x69, 0x01, 0x38, 0xE9, 0x05});
    k.run(4);
    check(k.cpu.a() == 0x20, "cpu: ADC decimal");
    k.run(2);
    check(k.cpu.a() == 0x15, "cpu: SBC decimal");
  }
  { // ciclos
    Maquina k({0xA2, 0, 0xCA, 0xD0, 0xFD});
    check(k.run(1 + 512) == 2 + 256 * 2 + 255 * 3 + 2, "cpu: ciclos de rama tomada");
    Maquina j({0xA2, 1, 0xBD, 0xFF, 0x03});
    check(j.run(2) == 2 + 5, "cpu: ciclo extra por cruce de pagina");
  }
  { // JAM: parada hasta el reset
    Maquina k({0x02});
    k.run(5);
    check(k.cpu.parada() && k.cpu.pc() == 0x200, "cpu: JAM la detiene");
    k.cpu.reset();
    check(!k.cpu.parada(), "cpu: el reset la recupera");
  }
}

static void test_timer_irq() {
  bool r;
  { // expira tras valor+1 pulsos de 6,75 MHz
    Timer t;
    t.write(0xC034, 0xE8, r); t.write(0xC035, 3, r); // 1000
    t.write(0xC036, 1, r);
    t.tick(500);
    check(!(t.read(0xC036, r) & 0x80), "timer: aun no en 1000 pulsos");
    t.tick(1);
    check((t.read(0xC036, r) & 0x89) == 0x88, "timer: ZERO+IRQ_FLAG y EN se apaga");
    t.write(0xC036, 0x08, r);
    check((t.read(0xC036, r) & 0x88) == 0x80, "timer: escribir bit 3 borra solo IRQ_FLAG");
    t.write(0xC036, 0x80, r);
    check(!(t.read(0xC036, r) & 0x80), "timer: escribir bit 7 borra ZERO");
  }
  { // recarga y prescaler
    Timer t;
    t.write(0xC034, 9, r); t.write(0xC037, 3, r); // 10 cuentas de 4 pulsos = 40
    t.write(0xC036, 0x05, r);
    int veces = 0;
    for (int i = 0; i < 100; i++) { // 200 pulsos
      t.tick(1);
      if (t.read(0xC036, r) & 8) { veces++; t.write(0xC036, 0x0D, r); }
    }
    check(veces == 5, "timer: REPEAT recarga y el prescaler divide");
  }
  { // llega a la CPU como IRQ, con B=0 en la pila
    Maquina k({0x58, 0xEA, 0xEA, 0xEA, 0xEA, 0xEA, 0xEA, 0xEA, 0xEA, 0xEA});
    k.m(0x1000) = 0x40; // RTI
    k.bus.write(0xC034, 4);
    k.bus.write(0xC036, 0x03);
    bool entro = false;
    for (int i = 0; i < 9 && !entro; i++) { k.run(1); entro = k.cpu.pc() == 0x1000; }
    check(entro, "timer: la IRQ llega a la CPU");
    check((k.m(0x100 + (uint8_t)(k.cpu.sp() + 1)) & 0x30) == 0x20, "irq: apila B=0");
  }
  { // CLI surte efecto una instruccion despues
    Maquina k({0x58, 0xE8, 0xE8});
    k.bus.write(0xC036, 0x03); // valor 0: expira enseguida
    k.bus.tick(4);
    k.run(1);                  // CLI
    k.run(1);                  // INX aun se ejecuta
    check(k.cpu.x() == 1, "irq: tras CLI se ejecuta una instruccion mas");
    k.run(1);
    check(k.cpu.pc() == 0x1000, "irq: y despues se atiende");
  }
}

static void test_uart() {
  bool r;
  Uart u;
  u.write(0xC020, 'A', r);
  check((u.read(0xC021, r) & 0x05) == 0x04, "uart: ocupado durante la trama");
  u.tick(289);
  check(!(u.read(0xC021, r) & 1), "uart: 289 ciclos no bastan");
  u.tick(1);
  check((u.read(0xC021, r) & 0x05) == 0x01, "uart: listo a los 290 ciclos (87 us)");
  check(!u.irq(), "uart: sin IRQ si no se habilita");
  u.write(0xC021, 0x01, r);
  check(u.irq(), "uart: IRQ de TX con TX_READY");
  u.write(0xC021, 0x00, r);
  // Recepción: un byte cada 290 ciclos si la CPU los va leyendo.
  for (int i = 0; i < 5; i++) u.enqueue_rx((uint8_t)('0' + i));
  std::string leido;
  for (int c = 0; c < 290 * 6; c += 10) {
    u.tick(10);
    if (u.read(0xC021, r) & 2) leido.push_back((char)u.read(0xC020, r));
  }
  check(leido == "01234", "uart: recibe a la velocidad del cable");
}

static void test_sd() {
  const char* img = "test_perif.img";
  std::remove(img);
  std::string err;
  {
    SdCardImpl sd;
    check(sd.open(img, 1), "sd: crea la imagen");
    check(!sd.agregar_archivo("G.BIN", std::vector<uint8_t>(70000), &err),
          "sd: rechaza mas de 65535 bytes");
    check(sd.agregar_archivo("A.BIN", std::vector<uint8_t>(3000, 1), &err), "sd: agrega");
    const uint32_t libre = sd.espacio_libre();
    check(sd.agregar_archivo("A.BIN", std::vector<uint8_t>(2500, 2), &err), "sd: sustituye");
    check(sd.espacio_libre() == libre, "sd: sustituir reutiliza el sitio");
    // Llenar la tarjeta (14 x 128 sectores) y comprobar que un fallo no borra el original.
    char nombre[16];
    for (int i = 0; i < 20; i++) {
      std::snprintf(nombre, sizeof nombre, "F%d.BIN", i);
      sd.agregar_archivo(nombre, std::vector<uint8_t>(65535), &err);
    }
    sd.borrar_archivo("F0.BIN"); // borrar no libera sectores: gasta otros 128
    sd.agregar_archivo("F0.BIN", std::vector<uint8_t>(65535), &err);
    check(!sd.agregar_archivo("A.BIN", std::vector<uint8_t>(65535), &err), "sd: no cabe");
    std::vector<uint8_t> d;
    check(sd.leer_archivo("A.BIN", &d) && d.size() == 2500 && d[0] == 2,
          "sd: si no cabe, el archivo anterior sigue");
    // OCR: CCS depende de HCS.
    auto cmd = [&](uint8_t n, uint32_t arg) {
      sd.transfer((uint8_t)(0x40 | n));
      for (int s = 24; s >= 0; s -= 8) sd.transfer((uint8_t)(arg >> s));
      sd.transfer(0xFF);
      return sd.transfer(0xFF);
    };
    cmd(0, 0); cmd(55, 0); cmd(41, 0); cmd(58, 0);
    check(sd.transfer(0xFF) == 0x80, "sd: sin HCS no se declara SDHC");
    sd.deselect();
    cmd(0, 0); cmd(55, 0); cmd(41, 0x40000000); cmd(58, 0);
    check(sd.transfer(0xFF) == 0xC0, "sd: con HCS es SDHC");
  }
  // Cerrar no reescribe la imagen: un cambio externo sobrevive.
  {
    SdCardImpl sd;
    sd.open(img, 1);
    FILE* f = std::fopen(img, "r+b");
    std::fseek(f, 1000 * 512, SEEK_SET);
    std::fputc(0x5A, f);
    std::fclose(f);
  }
  FILE* f = std::fopen(img, "rb");
  std::fseek(f, 1000 * 512, SEEK_SET);
  check(std::fgetc(f) == 0x5A, "sd: al cerrar no pisa cambios externos");
  std::fclose(f);
  // Un archivo cualquiera no se reformatea.
  f = std::fopen(img, "wb");
  std::fputs("no soy una tarjeta", f);
  std::fclose(f);
  {
    SdCardImpl sd;
    check(!sd.open(img, 1), "sd: rechaza un archivo que no es imagen");
  }
  f = std::fopen(img, "rb");
  std::fseek(f, 0, SEEK_END);
  check(std::ftell(f) == 18, "sd: y no lo toca");
  std::fclose(f);
  std::remove(img);
}

static void test_reset_placa() {
  Maquina k({0xEA});
  Bus& b = k.bus;
  check(b.read(0xC002) == 0xFF, "gpio: los puertos arrancan como entrada");
  b.write(0xC002, 0); b.write(0xC004, 1); b.write(0xD804, 99); b.write(0xC036, 3);
  b.write(0xD800, 0); b.write(0xD801, 0xC0); b.write(0xD802, 77); // OAM
  b.reset_placa();
  check(b.read(0xC002) == 0xFF && b.read(0xC036) == 0, "reset: GPIO y timer");
  check(b.read(0xC004) == 1, "reset: conserva el flag de usuario");
  check(b.video()->scroll().x == 0, "reset: scroll a 0");
  check(!(b.read(0xD803) & 0x10), "reset: VIDEO_READY a 0 durante el init");
  b.tick(700);
  check((b.read(0xD803) & 0x10) && b.video()->tilemap()[0] == 0x20, "reset: init de arranque");
  check(b.video()->oam()[0] == 77, "reset: conserva el OAM");
  check(b.video()->sprite_patterns()[0] == 0x3C, "reset: arte de demo");
}

static void test_sid_muestras() {
  Sid s;
  std::vector<int16_t> o;
  size_t n = 0;
  for (int i = 0; i < 2000; i++) { s.tick(3375); s.sacar_audio(o); n += o.size(); }
  check(n > 95950 && n < 96050, "sid: 48000 muestras/s (reSIDfp redondea +-0,05 %)");
}

int main() {
  std::printf("test_perif\n");
  test_cpu();
  test_timer_irq();
  test_uart();
  test_sd();
  test_reset_placa();
  test_sid_muestras();
  std::printf(fallos ? "test_perif: %d FALLOS\n" : "test_perif: OK\n", fallos);
  return fallos ? 1 : 0;
}
