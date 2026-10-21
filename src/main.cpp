// Emulador del computador retro fpga-6502-16k (Tang Nano 9K, GW1NR-9).
//
// Expone el UART emulado por TCP para conectar PuTTY (modo Raw) y, si se
// indica una imagen, emula una tarjeta SD conectada al SPI del sistema.
//
// Uso:
//   emulador <rom.bin> [puerto] [tarjeta.img]
//     rom.bin     : binario de 16 KB de la ROM del monitor
//     puerto      : puerto TCP del UART (default 4000)
//     tarjeta.img : imagen de tarjeta SD (se crea formateada si no existe)

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winsock2.h> // antes que windows.h (lo exige winsock2.h)
#include <windows.h>
#include <mmsystem.h>

// Cede el turno al planificador sin dormir. sleep_for dormiría 15.6 ms.
static inline void yield_cpu() { SwitchToThread(); }
#else
#include <sched.h>
static inline void yield_cpu() { sched_yield(); }
#endif

#include "bus.h"
#include "cpu_6502.h"
#include "ram_rom.h"
#include "audio.h"
#include "debug_server.h"
#include "emu_window.h"
#include "gpio_uart.h"
#include "sid.h"
#include "video/video.h"
#include "sd_card.h"
#include "tcp_server.h"
#include "timer.h"
#include "uart.h"

static std::vector<uint8_t> load_file(const char* path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    std::fprintf(stderr, "ERROR: no se pudo abrir %s\n", path);
    std::exit(1);
  }
  f.seekg(0, std::ios::end);
  size_t n = (size_t)f.tellg();
  f.seekg(0, std::ios::beg);
  std::vector<uint8_t> data(n);
  if (n) f.read((char*)data.data(), n);
  return data;
}

int main(int argc, char** argv) {
  // Sin buffer en stdout para ver los mensajes en tiempo real.
  setvbuf(stdout, nullptr, _IONBF, 0);

#ifdef _WIN32
  // Windows usa por defecto una resolución de temporizador de 15.6 ms. Con
  // ella, sleep_for(50us) duerme hasta 15 ms y el bucle de emulación da tirones
  // de 30+ ms: el audio se congela y luego se recupera de golpe. Subir la
  // resolución a 1 ms (timeBeginPeriod) deja dormir con precisión suficiente.
  // Se restaura al salir.
  timeBeginPeriod(1);
  // Prioridad alta para el hilo principal: con prioridad normal el planificador
  // de Windows lo aparca hasta 15 ms para atender a otros procesos. En el emulador
  // eso se traduce en que todo se detiene y luego se recupera de golpe: el audio
  // suena lento/frenado a ratos y se corta.
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

  // Un proceso sin ventana en primer plano recibe, al cabo de unos segundos,
  // "Efficiency Mode" / prioridad de fondo (Windows la aplica sola a procesos
  // sin consola en foco y sin entrada del usuario). Eso hace que el planificador
  // lo pause en ráfagas de 15-60 ms: exactamente los silencios y frenazos del
  // audio. Se desactiva explícitamente.
  {
    PROCESS_POWER_THROTTLING_STATE pts{};
    pts.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    pts.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    pts.StateMask = 0; // sin limitación
    SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &pts, sizeof(pts));
  }
#endif

  if (argc < 2) {
    std::fprintf(stderr,
      "Uso: %s <rom.bin> [puerto] [tarjeta.img] [opciones]\n"
      "  rom.bin     : binario de 16 KB de la ROM del monitor\n"
      "  puerto      : puerto TCP del UART (default 4000)\n"
      "  tarjeta.img : imagen SD (se crea FAT16 si no existe)\n"
      "\n"
      "Opciones:\n"
      "  --debug[=PUERTO]  activa el servidor HTTP de depuracion (8080 por\n"
      "                    defecto). Desactivado si no se indica.\n",
      argv[0]);
    return 1;
  }

  int port = 4000;
  if (argc >= 3) port = atoi(argv[2]);

  std::vector<uint8_t> rom = load_file(argv[1]);
  if (rom.size() != 16384) {
    std::fprintf(stderr,
      "WARN: la ROM mide %zu bytes (se esperaban 16384); se rellena/trunca.\n",
      rom.size());
  }

  Bus bus;
  bus.ram_rom()->load_rom(rom.data(), rom.size());

  // Tarjeta SD emulada (opcional).
  SdCardImpl sd;
  if (argc >= 4) {
    if (!sd.open(argv[3], 16)) {
      std::fprintf(stderr, "No se pudo abrir/crear la imagen SD\n");
      return 1;
    }
    bus.spi()->attach(&sd);
    std::printf("SD: %s conectada al SPI\n", argv[3]);
  }

  Cpu6502 cpu(bus);
  cpu.reset();

  // Conectar el UART emulado al servidor TCP.
  Uart& uart = *bus.uart();
  TcpServer server;
  server.on_rx = [&uart](uint8_t v) { uart.enqueue_rx(v); };
  server.on_tx_available = [&uart]() { return uart.has_tx(); };
  server.on_tx_dequeue   = [&uart]() { return uart.dequeue_tx(); };

  if (!server.start(port)) {
    std::fprintf(stderr, "No se pudo abrir el servidor TCP en %d\n", port);
    return 1;
  }

  std::printf("Monitor arrancando...\n");
  std::printf("Para conectar: PuTTY -> Raw -> localhost:%d\n", port);

  // Servidor HTTP de depuración: DESACTIVADO por defecto.
  //
  // Permite pedir la pantalla, la VRAM y el estado desde fuera sin detener la
  // emulación. Es la vía para que un agente de IA (o cualquier herramienta)
  // pueda ver qué está generando el software que se está depurando.
  //
  //   http://localhost:8080/            índice con vista previa en vivo
  //   http://localhost:8080/screen.png  captura de lo que saldría por HDMI
  //   http://localhost:8080/vram.txt    tilemap / atributos / OAM
  //   http://localhost:8080/state.txt   GPIO, SID, vídeo, UART
  //
  // Se activa con:
  //   --debug              (puerto 8080)
  //   --debug=PUERTO       (otro puerto)
  //   EMU_DEBUG_PORT=PUERTO
  //
  // También se acepta el nombre anterior --debug-http[=PUERTO].
  DebugServer debug;
  {
    int dbg_port = 0; // desactivado por defecto
    if (const char* env = std::getenv("EMU_DEBUG_PORT")) dbg_port = atoi(env);
    for (int i = 1; i < argc; i++) {
      const char* a = argv[i];
      if (std::strncmp(a, "--debug", 7) == 0) {
        dbg_port = 8080;
        const char* eq = std::strchr(a, '=');
        if (eq) dbg_port = atoi(eq + 1);
      }
    }
    if (dbg_port > 0) {
      if (!debug.start(dbg_port, bus)) {
        std::fprintf(stderr, "Aviso: no se pudo abrir la depuracion HTTP en el puerto %d\n",
                     dbg_port);
      }
    }
  }

  // Inicializar audio (opcional: si falla, el emulador sigue funcionando).
  AudioOutput audio;
  audio.start(48000, 80);

  // Ventana: vídeo del Core de Vídeo + los 6 LEDs del Tang Nano 9K.
  EmuWindow window;
  window.start(Video::W, Video::H, 3, /*terminal=*/true);
  uint32_t* framebuffer = new uint32_t[Video::W * Video::H];
  auto proximo_frame = std::chrono::steady_clock::now();  // refresco de ventana

  // Reloj del SID: en el FPGA es clk_1mhz (~1 MHz) y reSIDfp lo asume a
  // 985248 Hz. El SID avanza en proporción exacta a los ciclos de CPU.
  const double cpu_hz = 3375000.0;   // 6.75 MHz / 2
  const double sid_hz = 964285.714;   // 27 MHz / 28 (clk_1mhz del FPGA)
  const double cp_per_sid = cpu_hz / sid_hz;

  int16_t audio_buf[4096];
  double sid_accum = 0.0;   // ciclos de SID pendientes de generar

  // Colchón del anillo de audio: cuánto sonido se adelanta al vídeo. Es la
  // latencia AUDIO->oído menos lo que ya tenga guardado el driver. 25 ms es
  // suficiente para absorber los tropiezos normales del hilo, y mantiene el
  // sonido pegado a la imagen (antes eran 60 ms y se notaba el retraso).
  const int AUDIO_TARGET_MS = 25;

  // Volcado opcional de la señal generada a WAV (EMU_WAV=fichero.wav). Sirve
  // para analizar glitches sin depender del oído ni del dispositivo de audio.
  const char* wav_path = std::getenv("EMU_WAV");
  FILE* wav = nullptr;
  unsigned long wav_samples = 0;  // solo para la cabecera del WAV
  if (wav_path) {
    wav = std::fopen(wav_path, "wb");
    if (wav) {
      // Cabecera WAV de 48 kHz mono 16 bits; se corrige al cerrar.
      uint8_t hdr[44] = {0};
      std::memcpy(hdr, "RIFF", 4);
      std::memcpy(hdr + 8, "WAVEfmt ", 8);
      hdr[16] = 16;
      hdr[20] = 1;  // PCM
      hdr[22] = 1;  // mono
      hdr[24] = 0x80; hdr[25] = 0xBB; // 48000
      hdr[28] = 0x00; hdr[29] = 0xEE; hdr[30] = 0x02; hdr[31] = 0;
      hdr[32] = 2;  // block align
      hdr[34] = 16; // bits
      std::memcpy(hdr + 36, "data", 4);
      std::fwrite(hdr, 1, 44, wav);
      std::printf("WAV: %s\n", wav_path);
    }
  }

  // Timing a tiempo real.
  //
  // No se ejecuta un número fijo de ciclos por vuelta: se compara con el reloj
  // de pared y se ejecuta exactamente lo que toca. Así el audio no se acumula
  // ni se descarta (antes eso producía tirones) y los periféricos ven el mismo
  // ritmo que en el FPGA.
  using clock = std::chrono::steady_clock;
  using namespace std::chrono;
  auto t0 = clock::now();
  long long cycles_done = 0;   // ciclos de CPU ejecutados desde t0
  const long long cycles_per_ms = (long long)(cpu_hz / 1000.0);
  const long long max_lag_cycles = (long long)(cpu_hz * 0.050); // 50 ms de retraso máx.
  double worst_sleep_ms = 0.0;
  double worst_gap_at = 0.0;
  clock::time_point last_loop = clock::now();
  double worst_loop_ms = 0.0, worst_poll_ms = 0.0, worst_cpu_ms = 0.0;
  double worst_aud_ms = 0.0, worst_gap_ms = 0.0, worst_draw_ms = 0.0;
  unsigned long last_underruns_ = 0;
  const bool verbose_ = std::getenv("EMU_TRACE") != nullptr;

  // El servidor de depuración se atiende a 50 Hz, no en cada vuelta del bucle.
  // El bucle da ~20 000 vueltas por segundo (duerme 50 us cuando va al día), y
  // llamar a poll() en todas ellas acababa desbordando la pila del proceso al
  // recibir peticiones seguidas. A 50 Hz sobra para un cliente HTTP.
  auto next_debug_poll = clock::now();

  // Bucle principal.
  //
  // Cada vuelta procesa ~2 ms de emulación de golpe (en vez de micro-pasos).
  // Con pasos muy pequeños el hilo pasaba la mayor parte del tiempo cediendo el
  // CPU y Windows lo desprogramaba durante 15-45 ms, lo que producía los
  // tirones y silencios. Bloqueamos también los envíos TCP (no esperar acks por
  // bloques pequeños) para que el nivel de audio se inserte sin saltos.
  for (;;) {
    double aud_ms = 0.0;
    double draw_ms = 0.0;
    auto t_poll0 = clock::now();
    server.poll();
    if (clock::now() >= next_debug_poll) {
      debug.poll();
      next_debug_poll = clock::now() + std::chrono::milliseconds(20);
    }
    auto t_poll1 = clock::now();

    // ¿Cuántos ciclos deberían haber pasado en tiempo real?
    auto elapsed = duration_cast<microseconds>(clock::now() - t0).count();
    long long target = elapsed * (long long)cpu_hz / 1000000LL;

    // Si nos hemos quedado muy atrás (p. ej. el SO robó el hilo durante mucho
    // tiempo), se ejecuta de más hasta recuperar el ritmo, NUNCA se pierde
    // tiempo emulado: resincronizar hacia atrás hacía que el player (1918)
    // fuera un 5 % lento, con tirones visibles en la música.
    //
    // El tiempo descartado aquí no se emula (la CPU no lo ejecuta), pero su
    // audio SÍ se genera: si no, esos milisegundos se convierten en silencio y
    // el sonido se va separando del vídeo. Con tirones de ~90 ms se acumulaba
    // casi un segundo de desfase.
    long long saltados = 0;
    if (target - cycles_done > max_lag_cycles) {
      saltados = target - cycles_done;
      cycles_done = target; // al día: sin atraso pendiente
    }

    long long to_run = target - cycles_done;

    // Con `to_run` de pocos ciclos (decenas), ejecutar y volver a girar sale
    // carisimo: el bucle acababa dando 118000 vueltas/s al 100 % de CPU, y al
    // estar saturado el SO lo desaloja (huecos de 90 ms) y el audio se queda
    // sin datos. Cuando queda menos de ~1 ms de trabajo se duerme ese resto.
    if (to_run <= 0 || to_run < cycles_per_ms / 4) {
      const long long falta_us = to_run > 0 ? to_run * 1000000LL / (long long)cpu_hz : 0;
      const long long dormir = falta_us - 200; // despertar un poco antes
      const auto ws = clock::now();
      if (dormir > 100) {
        std::this_thread::sleep_for(std::chrono::microseconds(dormir));
      } else {
        std::this_thread::yield();
      }
      const double slept =
          std::chrono::duration<double, std::milli>(clock::now() - ws).count();
      if (slept > worst_sleep_ms) worst_sleep_ms = slept;
      continue;
    }

    // Generar el audio del SID ANTES de ejecutar la CPU de esta vuelta.
    //
    // Antes se hacía después, y eso metía el sonido un bloque por detrás del
    // vídeo: el juego avanzaba 2 ms y solo entonces se generaba el audio de ese
    // intervalo. Con el bucle en rachas el retraso se acumulaba y se oía un
    // desfase de cerca de un segundo.
    //
    // El colchón del anillo (AUDIO_TARGET_MS) decide el resto de la latencia:
    // es lo que se adelanta el audio para que un tropiezo del hilo no lo deje
    // mudo. Se mantiene corto pero suficiente.
    if (audio.active()) {
      auto t_aud0 = clock::now();

      // El SID avanza EXACTAMENTE con la CPU: tantos ciclos de SID como ciclos de
      // CPU se han emulado. Así el sonido y la imagen van al mismo ritmo y no se
      // desfasan con el tiempo (antes se ajustaba al nivel del buffer, y eso
      // metía error en el tempo).
      //
      // El factor `cycle_correction()` compensa que reSIDfp entrega un 0,11 %
      // más de muestras de las que consume el dispositivo; sin él, el anillo se
      // llena y el audio se queda sin datos a ratos.
      sid_accum += (double)(to_run + saltados) / cp_per_sid * Sid::cycle_correction();
      int want = (int)sid_accum;
      sid_accum -= want;

      // Control del colchón. El nivel lo mantienen estas reglas:
      //  - Si va sobrado mucho (p. ej. tras un parón): no generar nada hasta
      //    que baje al objetivo. Sin esto el colchón nunca baja y el sonido se
      //    queda retrasado para siempre.
      //  - Si va justo: generar un poco de más (1 ms como mucho) para volver al
      //    objetivo sin que se note.
      const long objetivo = (long)(audio.frequency() * AUDIO_TARGET_MS / 1000);
      const long nivel = (long)audio.fill();
      if (nivel > objetivo + (long)(audio.frequency() * 5 / 1000)) {
        want = 0; // sobra más de 5 ms: dejar que se vacíe
      } else {
        const long deficit = objetivo - nivel;
        if (deficit > 0) {
          // Cuánto audio falta para llenar el colchón, en muestras.
          //
          // Antes se sumaba como máximo 1 ms por vuelta, pero el bucle solo da
          // ~500 vueltas/s (cada una emula ~2 ms de CPU): se generaba como
          // mucho la MITAD del audio que consume el dispositivo, y el resto
          // eran underruns (silencio intermitente y sonido a tirones).
          // El límite por vuelta debe cubrir el déficit completo; lo que evita
          // pasarse de largo es la comprobación de "sobra más de 5 ms" de
          // arriba, no este tope.
          want += (int)deficit;
        }
      }

      while (want > 0) {
        const int chunk = std::min(want, 512);
        const int n = bus.sid()->clock(chunk, audio_buf, 4096);
        if (n > 0) {
          audio.push(audio_buf, n);
          if (wav) {
            std::fwrite(audio_buf, sizeof(int16_t), (size_t)n, wav);
            wav_samples += (unsigned long)n;
          }
        }
        want -= chunk;
      }
      aud_ms = std::chrono::duration<double, std::milli>(clock::now() - t_aud0).count();
    }

    // Ejecutar los ciclos que faltan (bloques de ~2 ms como máximo, para que
    // server.poll() atienda al socket con regularidad).
    long long blocked = to_run > cycles_per_ms ? cycles_per_ms : to_run;
    auto t_cpu0 = clock::now();
    long executed = 0;
    while (executed < blocked) {
      cpu.step();
      executed += cpu.last_cycles();
    }
    auto t_cpu1 = clock::now();
    cycles_done += executed;

    // Avanzar los periféricos con los ciclos de CPU realmente transcurridos.
    uart.tick((uint32_t)executed);
    bus.timer()->tick((uint32_t)executed);

    // VBLANK del Core de Video, atado al reloj de pared.
    //
    // Se calcula cuántas líneas de barrido TOCAN desde el arranque (no cuántas
    // han pasado desde la vuelta anterior): así un retraso puntual del hilo no
    // se acumula en la siguiente vuelta y la frecuencia de frames queda exacta.
    // El barrido del hardware es 858 x 525 a 27 MHz: 525 líneas por frame.
    {
      const long long us_desde_inicio =
          duration_cast<microseconds>(clock::now() - t0).count();
      // 1 línea = 858 píxeles a 27 MHz = 31,777 µs, es decir
      // 31,468 líneas por milisegundo. Con enteros: us * 27000 / 858 da
      // milésimas de línea; se divide entre 1000 para quedarse con la línea.
      const long long lineas_tocan = (us_desde_inicio * 27000LL) / 858LL / 1000LL;
      bus.video()->sync_scanlines(lineas_tocan);
    }

    if (window.active()) {
      if (!window.poll()) break; // el usuario cerro la ventana
      bus.gpio()->set_joy(window.joy());

      // Dibujar a ~60 Hz (el vídeo es de 320x240; no hace falta más).
      const auto ahora = clock::now();
      if (ahora >= proximo_frame) {
        proximo_frame = ahora + std::chrono::microseconds(16683);
        const auto t_draw0 = clock::now();
        bus.video()->render(framebuffer);
        if (window.has_terminal()) window.set_terminal(uart.terminal());
        // Pines del puerto 1 tal como los lee la CPU (mismo dato que /state.txt).
        window.set_port1(bus.gpio()->port1_read(), bus.gpio()->dir1());
        window.present(framebuffer, bus.gpio()->leds());
        draw_ms = std::chrono::duration<double, std::milli>(clock::now() - t_draw0).count();
      }
    }

    // Diagnóstico de rendimiento (cada ~5 s).
    {
      auto now = clock::now();
      const double loop_ms = std::chrono::duration<double, std::milli>(now - last_loop).count();
      const double poll_ms = std::chrono::duration<double, std::milli>(t_poll1 - t_poll0).count();
      const double cpu_ms  = std::chrono::duration<double, std::milli>(t_cpu1 - t_cpu0).count();
      const double gap_ms  = std::chrono::duration<double, std::milli>(t_poll0 - last_loop).count();
      if (gap_ms > worst_gap_ms) {
        worst_gap_ms = gap_ms;
        worst_gap_at = std::chrono::duration<double>(now - t0).count();
      }
      last_loop = now;
      if (loop_ms > worst_loop_ms) worst_loop_ms = loop_ms;
      if (poll_ms > worst_poll_ms) worst_poll_ms = poll_ms;
      if (cpu_ms > worst_cpu_ms) worst_cpu_ms = cpu_ms;
      if (aud_ms > worst_aud_ms) worst_aud_ms = aud_ms;
      if (draw_ms > worst_draw_ms) worst_draw_ms = draw_ms;

      static auto last_report = now;
      static long long cycles_at_report = 0;
      static double sum_fill = 0.0;
      static int n_fill = 0;
      static unsigned long max_fill = 0;
      if (audio.active()) {
        const unsigned long f = (unsigned long)audio.fill();
        sum_fill += (double)f;
        n_fill++;
        if (f > max_fill) max_fill = f;
      }
      if (now - last_report > std::chrono::seconds(5)) {
        // Se imprime SOLO cuando hay algo anómalo: el printf bloquea el hilo
        // (~40 ms con la salida redirigida) y eso mismo creaba los tirones que
        // intentaba medir.
        const double secs = std::chrono::duration<double>(now - last_report).count();
        const double mhz = (double)(cycles_done - cycles_at_report) / secs / 1e6;
        // Latencia media del audio = lo que hay en el anillo + el periodo del
        // dispositivo. Es lo que decide el desfase respecto al vídeo.
        const double fill_ms = n_fill ? (sum_fill / n_fill) * 1000.0 / audio.frequency() : 0.0;
        const double period_ms = audio.callback_samples() * 1000.0 / audio.frequency();
        const bool anomaly = worst_gap_ms > 5.0 || worst_loop_ms > 5.0 ||
                             audio.underruns() != last_underruns_ ||
                             fill_ms > AUDIO_TARGET_MS * 2.0;
        if (anomaly && verbose_) {
          std::fprintf(stderr,
                       "EMU: %.2f MHz | audio: colchon=%.1f ms (max %.1f) + periodo=%.1f ms -> latencia ~%.1f ms | underrun=%lu | bucle=%.1f poll=%.1f cpu=%.1f aud=%.1f dibujo=%.1f hueco=%.1f@%.1fs\n",
                       mhz, fill_ms, max_fill * 1000.0 / audio.frequency(), period_ms,
                       fill_ms + period_ms, audio.underruns(), worst_loop_ms, worst_poll_ms,
                       worst_cpu_ms, worst_aud_ms, worst_draw_ms, worst_gap_ms, worst_gap_at);
        }
        last_underruns_ = audio.underruns();
        sum_fill = 0.0;
        n_fill = 0;
        max_fill = 0;
        last_report = now;
        cycles_at_report = cycles_done;
        worst_loop_ms = worst_poll_ms = worst_cpu_ms = worst_aud_ms = worst_gap_ms = worst_sleep_ms = 0.0;
        worst_draw_ms = 0.0;
      }
    }
  }

  delete[] framebuffer;
  return 0;
}
