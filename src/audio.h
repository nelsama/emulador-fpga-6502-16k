#pragma once

#include <cstdint>
#include <vector>

// Salida de audio del emulador.
//
// Mantiene un buffer circular que el hilo de audio de SDL2 va consumiendo y que
// el bucle principal va rellenando con muestras del SID. Si SDL2 no está
// disponible o el dispositivo falla, funciona como un "null sink" (las
// muestras se descartan), de modo que el emulador nunca se bloquea por audio.
class AudioOutput {
public:
  AudioOutput() = default;
  ~AudioOutput();

  // Abre el dispositivo. freq = frecuencia de muestreo; buffer_ms = latencia.
  // Devuelve false si no se pudo abrir (el emulador sigue funcionando).
  bool start(int freq = 48000, int buffer_ms = 40);
  void stop();

  bool active() const { return active_; }

  // Añade muestras mono (int16) a la cola de reproducción.
  void push(const int16_t* samples, int count);

  // Rellenado del buffer (llamado por el callback de SDL2).
  void pull(int16_t* out, int count);

  int frequency() const { return freq_; }

  // Muestras que entrega el dispositivo por callback (latencia mínima).
  int callback_samples() const { return callback_samples_; }

  // Estadísticas para diagnosticar tirones. Se cuentan las VECES que el anillo
  // se quedó sin datos (episodios), no las muestras de silencio: un solo hueco
  // de un callback son 1024 muestras y contarlas una a una da magnitudes
  // absurdas (decenas de miles) que no se corresponden con lo que se oye.
  unsigned long underruns() const { return underruns_; }
  // Muestras de silencio totales que provocaron esos episodios.
  unsigned long underrun_samples() const { return underrun_samples_; }
  unsigned long overdrops() const { return overdrops_; }
  size_t fill() const { return fill_; }

private:
  std::vector<int16_t> ring_;
  size_t head_ = 0; // posición de lectura
  size_t tail_ = 0; // posición de escritura
  size_t size_ = 0; // capacidad
  size_t fill_ = 0; // muestras disponibles

  int freq_ = 48000;
  int callback_samples_ = 0; // muestras por callback del dispositivo
  bool active_ = false;
  void* dev_ = nullptr; // SDL_AudioDeviceID
  unsigned long underruns_ = 0;         // episodios (tramos sin datos)
  unsigned long underrun_samples_ = 0;  // muestras de silencio acumuladas
  unsigned long overdrops_ = 0;
  // Dentro de un tramo de silencio: para no contar cada muestra como episodio.
  bool underflow_active_ = false;

  static void sdl_callback(void* userdata, uint8_t* stream, int len);
};
