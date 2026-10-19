#include "audio.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef HAVE_SDL2
#include <SDL.h>
#endif

namespace {

// Dispositivos por los que no sale el audio directamente: reenvían la señal
// (Steam la codifica para mandarla por red; Nahimic la procesa en cadena) y
// añaden un retraso grande, que se percibe como sonido por detrás del vídeo.
bool es_virtual(const char* nombre) {
  if (!nombre) return false;
  return std::strstr(nombre, "Steam Streaming") || std::strstr(nombre, "Nahimic") ||
         std::strstr(nombre, "VB-Audio") || std::strstr(nombre, "Virtual");
}

} // namespace

AudioOutput::~AudioOutput() { stop(); }

void AudioOutput::sdl_callback(void* userdata, uint8_t* stream, int len) {
  auto* self = static_cast<AudioOutput*>(userdata);
  const int count = len / (int)sizeof(int16_t);
  self->pull(reinterpret_cast<int16_t*>(stream), count);
}

bool AudioOutput::start(int freq, int buffer_ms) {
  freq_ = freq;
  size_ = (size_t)freq * (size_t)buffer_ms / 1000 * 8; // ~8 buffers de holgura
  if (size_ < 16384) size_ = 16384;
  ring_.assign(size_, 0);
  head_ = tail_ = fill_ = 0;
  underruns_ = 0;
  overdrops_ = 0;

#ifdef HAVE_SDL2
  if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
    std::fprintf(stderr, "audio: SDL_InitSubSystem falló: %s\n", SDL_GetError());
    return false;
  }
  std::printf("audio: driver=%s\n",
              SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "(ninguno)");
  {
    const int nd = SDL_GetNumAudioDevices(0);
    std::printf("audio: %d dispositivos de salida\n", nd);
    for (int i = 0; i < nd; i++) {
      const char* n = SDL_GetAudioDeviceName(i, 0);
      std::printf("audio:   [%d] %s%s\n", i, n ? n : "?",
                  es_virtual(n) ? "  <- virtual: añade retraso" : "");
    }
  }

  SDL_AudioSpec want{};
  want.freq = freq;
  want.format = AUDIO_S16SYS;
  want.channels = 1;
  want.samples = 1024; // ~21 ms por callback (antes 2048: sumaba 43 ms de retraso)
  want.callback = &AudioOutput::sdl_callback;
  want.userdata = this;

  // Elegir dispositivo. Muchos equipos tienen dispositivos virtuales
  // (Steam Streaming, Nahimic...) que WASAPI no puede inicializar, y el
  // "predeterminado" del sistema puede apuntar a uno de ellos. Se prueba el
  // predeterminado y, si falla, cada dispositivo disponible en orden, sin los
  // que se sabe que son virtuales.
  SDL_AudioDeviceID dev = 0;
  SDL_AudioSpec have{};

  auto open = [&](const char* name) {
    SDL_AudioSpec w = want;
    return SDL_OpenAudioDevice(name, 0, &w, &have, 0);
  };

  // Nombre del dispositivo que se acaba abriendo (para avisar si es virtual).
  const char* nombre_usado = nullptr;

  // Permite forzar un dispositivo con EMU_AUDIO_DEVICE="nombre".
  if (const char* forced = std::getenv("EMU_AUDIO_DEVICE")) {
    dev = open(forced);
    if (dev) {
      nombre_usado = forced;
      std::printf("audio: usando '%s' (EMU_AUDIO_DEVICE)\n", forced);
    } else {
      std::fprintf(stderr, "audio: no se pudo abrir '%s': %s\n", forced, SDL_GetError());
    }
  }

  if (!dev) {
    dev = open(nullptr);
    if (dev) std::printf("audio: usando el dispositivo predeterminado\n");
  }

  if (!dev) {
    const int nd = SDL_GetNumAudioDevices(0);
    for (int i = 0; i < nd && !dev; i++) {
      const char* name = SDL_GetAudioDeviceName(i, 0);
      if (!name) continue;
      // Se saltan los dispositivos claramente virtuales.
      if (es_virtual(name)) continue;
      dev = open(name);
      if (dev) {
        nombre_usado = name;
        std::printf("audio: usando '%s' (predeterminado no disponible)\n", name);
      } else {
        std::fprintf(stderr, "audio: '%s' falló: %s\n", name, SDL_GetError());
      }
    }
  }

  if (dev == 0) {
    std::fprintf(stderr, "audio: no hay ningun dispositivo disponible: %s\n", SDL_GetError());
    return false;
  }

  freq_ = have.freq;
  // El callback del dispositivo entrega `have.samples` por llamada: ese es el
  // grano mínimo de latencia que añade SDL.
  callback_samples_ = have.samples;
  SDL_PauseAudioDevice(dev, 0); // empezar a reproducir
  dev_ = reinterpret_cast<void*>(static_cast<uintptr_t>(dev));
  active_ = true;
  std::printf("Audio: %d Hz, SDL2 (periodo %d muestras = %.1f ms)\n", freq_, callback_samples_,
              callback_samples_ * 1000.0 / (double)freq_);

  // Aviso: si se acabó usando un dispositivo virtual, el sonido irá
  // inevitablemente por detrás de la imagen y el emulador no puede hacer nada.
  if (es_virtual(nombre_usado)) {
    std::printf("audio: AVISO: '%s' es un dispositivo virtual; añade retraso.\n",
                nombre_usado);
    std::printf("audio:        para forzar otro: EMU_AUDIO_DEVICE=\"nombre del dispositivo\"\n");
  } else if (!nombre_usado) {
    // SDL no dice qué dispositivo abre con NULL. Si en la lista hay virtuales,
    // avisar de que el predeterminado puede ser uno de ellos: es la causa más
    // habitual de que el sonido vaya por detrás del vídeo.
    const int nd = SDL_GetNumAudioDevices(0);
    bool hay_virtual = false;
    for (int i = 0; i < nd; i++)
      if (es_virtual(SDL_GetAudioDeviceName(i, 0))) hay_virtual = true;
    if (hay_virtual) {
      std::printf("audio: si el sonido va por detrás del vídeo, el predeterminado puede ser\n"
                  "audio:   un dispositivo virtual. Fija otro con EMU_AUDIO_DEVICE=\"nombre\".\n");
    }
  }
  return true;
#else
  (void)buffer_ms;
  std::printf("Audio: sin SDL2, salida desactivada\n");
  return false;
#endif
}

void AudioOutput::stop() {
#ifdef HAVE_SDL2
  if (dev_) {
    SDL_AudioDeviceID dev = (SDL_AudioDeviceID)(uintptr_t)dev_;
    SDL_CloseAudioDevice(dev);
    dev_ = nullptr;
  }
#endif
  active_ = false;
}

void AudioOutput::push(const int16_t* samples, int count) {
  for (int i = 0; i < count; i++) {
    if (fill_ >= size_) { // buffer lleno: se descarta (no debería pasar)
      overdrops_++;
      return;
    }
    ring_[tail_] = samples[i];
    tail_ = (tail_ + 1) % size_;
    fill_++;
  }
}

void AudioOutput::pull(int16_t* out, int count) {
  for (int i = 0; i < count; i++) {
    if (fill_ == 0) {
      out[i] = 0; // silencio si no hay datos
      underruns_++;
    } else {
      out[i] = ring_[head_];
      head_ = (head_ + 1) % size_;
      fill_--;
    }
  }
}
