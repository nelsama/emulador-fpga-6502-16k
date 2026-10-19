#pragma once

#include <cstdint>

class Video;

// Dibuja el estado del Video en un buffer RGBA8888 de 320x240.
void render_video(const Video& v, uint32_t* out);
