#include <stdlib.h>
#include <stdio.h>
#include <stdexcept>

// Tiny utility program to test detwiddling texture address into linear format

// width & height powers of 2
static void
index_to_xy(unsigned width, unsigned height, unsigned index, unsigned *x, unsigned *y)
{
  unsigned offs_width = 0u, offs_height = 0u;

  if (width > height) {
    const unsigned per_block = height * height;
    width = height;
    offs_width = (index / per_block) * height;
    index = index & (per_block - 1u);
  } else if (height > width) {
    const unsigned per_block = width * width;
    height = width;
    offs_height = (index / per_block) * width;
    index = index & (per_block - 1u);
  }

  *x = offs_width;
  *y = offs_height;
  for (unsigned i = 0; i < 10; ++i) {
    unsigned at_level = (index >> (i * 2u)) & 0x3u;
    (*x) += (at_level & 2u) ? (1u << i) : 0u;
    (*y) += (at_level & 1u) ? (1u << i) : 0u;
  }
}

static void
read_twiddle_block(FILE *fp, unsigned int *to, unsigned width, unsigned height)
{
  for (unsigned count = 0u; count < width * height; ++count) {
    unsigned short bla;
    if (fread(&bla, 2, 1, fp) != 1)
      throw std::runtime_error("Failed to read texture data");

    unsigned char a = ((bla >> 12u) & 0xf) << 4u;
    unsigned char b = float(((bla >> 0u) & 0xf) << 4u) * float(a) / 255.0f,
                  g = float(((bla >> 4u) & 0xf) << 4u) * float(a) / 255.0f,
                  r = float(((bla >> 8u) & 0xf) << 4u) * float(a) / 255.0f;
    unsigned int rgba = (b << 16u) | (g << 8u) | r;
    unsigned x, y;
    index_to_xy(width, height, count, &x, &y);
    to[(y * width) + x] = rgba;
  }
}

int
main(int argc, char *argv[])
{
  const char *fname = argv[1];
  const unsigned offset = atoi(argv[2]);
  unsigned width = atoi(argv[3]);
  unsigned height = atoi(argv[4]);
  unsigned int *data = new unsigned int[width * height];

  FILE *fp = fopen(fname, "rb");
  fseek(fp, offset, SEEK_SET);
  read_twiddle_block(fp, data, width, height);
  fclose(fp);
  fprintf(stdout, "P6\n%u %u 255\n", width, height);
  for (unsigned i = 0u; i < width * height; ++i) {
    fwrite(&data[i], 3u, 1u, stdout);
  }
  delete[] data;

  return 0u;
}
