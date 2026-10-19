// Test del SID con reSIDfp: genera las 4 formas de onda en WAV.
#include <cstdio>
#include <cstdint>
#include <vector>

#include "sid.h"

int main() {
  struct Case { const char* name; uint8_t ctrl; };
  const Case cases[] = {
      {"saw", 0x20}, {"pulse", 0x40}, {"triangle", 0x10}, {"noise", 0x80}};

  const int sample_rate = 48000;
  const double seconds = 1.0;

  for (const auto& cs : cases) {
    Sid sid;
    bool r;

    // Volumen máximo, sin filtro
    sid.write(0xD418, 0x0F, r);
    sid.write(0xD417, 0x00, r);

    // Voz 1: La4 (440 Hz) -> freq = 440 * 16777216 / 985248
    const uint32_t freq = (uint32_t)(440.0 * 16777216.0 / 985248.0);
    sid.write(0xD400, (uint8_t)(freq & 0xFF), r);
    sid.write(0xD401, (uint8_t)(freq >> 8), r);
    sid.write(0xD402, 0x00, r);
    sid.write(0xD403, 0x08, r);  // pulso al 50%
    sid.write(0xD405, 0x00, r);  // A=0, D=0
    sid.write(0xD406, 0xF0, r);  // S=15, R=0
    sid.write(0xD404, (uint8_t)(cs.ctrl | 0x01), r); // forma + gate

    std::vector<int16_t> pcm;
    pcm.reserve((size_t)(sample_rate * seconds));

    int16_t buf[4096];
    const int total_cycles = (int)(985248 * seconds);
    int done = 0;
    while (done < total_cycles) {
      int chunk = total_cycles - done;
      if (chunk > 4096) chunk = 4096;
      int n = sid.clock(chunk, buf, 4096);
      for (int i = 0; i < n && pcm.size() < (size_t)(sample_rate * seconds); i++)
        pcm.push_back(buf[i]);
      done += chunk;
    }

    char path[64];
    std::snprintf(path, sizeof(path), ".tmp/resid_%s.wav", cs.name);
    FILE* f = std::fopen(path, "wb");
    if (!f) continue;

    uint32_t data_len = (uint32_t)(pcm.size() * 2);
    uint32_t riff_len = 36 + data_len;
    uint32_t rate = sample_rate, byte_rate = rate * 2;
    uint16_t fmt = 1, ch = 1, bits = 16, align = 2;
    uint32_t fmt_len = 16;

    std::fwrite("RIFF", 1, 4, f);
    std::fwrite(&riff_len, 4, 1, f);
    std::fwrite("WAVEfmt ", 1, 8, f);
    std::fwrite(&fmt_len, 4, 1, f);
    std::fwrite(&fmt, 2, 1, f);
    std::fwrite(&ch, 2, 1, f);
    std::fwrite(&rate, 4, 1, f);
    std::fwrite(&byte_rate, 4, 1, f);
    std::fwrite(&align, 2, 1, f);
    std::fwrite(&bits, 2, 1, f);
    std::fwrite("data", 1, 4, f);
    std::fwrite(&data_len, 4, 1, f);
    std::fwrite(pcm.data(), 2, pcm.size(), f);
    std::fclose(f);

    int32_t mn = 32767, mx = -32768;
    int64_t sum = 0;
    for (auto s : pcm) {
      sum += s;
      if (s < mn) mn = s;
      if (s > mx) mx = s;
    }
    int tr = 0;
    for (size_t i = 1; i < pcm.size(); i++)
      if ((pcm[i - 1] < 0) != (pcm[i] < 0)) tr++;

    std::printf("%-9s %s  muestras=%d min=%d max=%d media=%.0f cruces=%d env1=%u\n",
                cs.name, path, (int)pcm.size(), mn, mx,
                (double)sum / (double)pcm.size(), tr, sid.read(0xD41D, r));
  }
  return 0;
}
