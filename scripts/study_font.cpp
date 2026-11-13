// Desktop asset builder. Link to the same FreeInk font engine as the reader.
#include <FtFont.h>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>
using Bytes = std::vector<uint8_t>;
static Bytes read(const char* path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    throw std::runtime_error(path);
  }
  return Bytes(std::istreambuf_iterator<char>(in), {});
}
int main() {
  auto medium = read("assets/fonts/AtkinsonHyperlegibleNext-Medium.ttf");
  auto ipa = read("lib/FreeInkBook/test/fixtures/fonts/DejaVuSans.ttf");
  for (int px : {12, 15, 20}) {
    freeink::font::FtFont primary, supplement;
    if (!primary.init(medium.data(), medium.size(), px) || !supplement.init(ipa.data(), ipa.size(), px)) {
      return 1;
    }
    Bytes pixels;
    printf("static constexpr StudyGlyph glyphs%d[] = {\n", px);
    for (unsigned cp = 32; cp < 65536; ++cp) {
      auto* face = &primary;
      if (!face->glyphId(cp) && cp >= 0x250 && cp <= 0x36f) {
        face = &supplement;
      }
      auto id = face->glyphId(cp);
      if (!id || (cp >= 127 && cp < 160)) {
        continue;
      }
      auto b = face->rasterizeGlyph26_6(id, px * 64);
      if (!b || pixels.size() > 65535) {
        return 2;
      }
      size_t offset = pixels.size(), n = b->width * b->height;
      pixels.resize(offset + (n + 7) / 8);
      for (size_t i = 0; i < n; ++i) {
        if (b->pixels[i] >= 118) {
          pixels[offset + i / 8] |= 0x80 >> (i % 8);
        }
      }
      printf("{%u,%zu,%d,%d,%u,%u,%u},\n", cp, offset, b->xoff, px - 2 + b->yoff, b->width, b->height,
             b->advance);
    }
    printf("};\nstatic constexpr uint8_t studyPixels%d[] = {\n", px);
    for (size_t i = 0; i < pixels.size(); ++i) {
      printf("%u,%s", pixels[i], (i + 1) % 24 ? "" : "\n");
    }
    printf("\n};\n");
  }
}
