#include "video/render.h"

#include "video/video.h"

void render_video(const Video& v, uint32_t* out) {
  const_cast<Video&>(v).render(out);
}
