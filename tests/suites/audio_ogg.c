/* Tests for ogg.h (the soundtrack of theta, stb_vorbis inside).
 *
 *   suite_audio_ogg ASSETS_DIR
 *
 * 1. The shipped song: stereo, 44.1 kHz, 259.64 s; the numbers the file's last page gives.
 * 2. Pull model: the PCM does not depend on the block sizes the caller asks for.
 * 3. Seeking is sample accurate: after any seek (forward, backward, to the start, near the end) the PCM equals what decoding
 *    from the start gives at that position, bit for bit.
 * 4. The end: a position past it is the end, rendering there gives silence and reports no frames.
 * 5. Damaged input: garbage, truncation and flipped bytes never crash and never produce a non-finite sample.
 * 6. paudio with an Ogg party song: chunked rendering, pause, seeks to a sample, start offset, rejection of other rates and garbage.
 * 7. The decoder's set-up math (sin, cos, exp, log, pow without libm) against values known by hand.
 * The comparison with an independent decoder (ffmpeg) is the ctest `ogg_pcm_oracle`. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "support/suite.h"

#define OGG_IMPLEMENTATION
#include <theta/audio/ogg.h>
#define V2_IMPLEMENTATION
#include <theta/core/v2.h>
#define PLAYER_CORE_IMPLEMENTATION
#include <theta/player/player_core.h>

static int g_checks, g_failures;
#define CHECK(cond, ...)                            \
  do {                                              \
    g_checks++;                                     \
    if (!(cond)) {                                  \
      g_failures++;                                 \
      if (g_failures <= 40) {                       \
        printf("FAIL %s:%d: ", __FILE__, __LINE__); \
        printf(__VA_ARGS__);                        \
        printf("\n");                               \
      }                                             \
    }                                               \
  } while (0)

#define RATE         44100u
#define REF_SECONDS  30u
#define REF_FRAMES   (REF_SECONDS * RATE)
#define SONG_FRAMES  11450185ull /* the granule position of the last page of assets/theta.ogg (ffprobe: 259.641383 s) */
#define SONG_SECONDS 259.641383

static uint64_t g_rng = 0x9e3779b97f4a7c15ull;
static uint32_t rnd(void)
{
  g_rng ^= g_rng << 13;
  g_rng ^= g_rng >> 7;
  g_rng ^= g_rng << 17;
  return (uint32_t)(g_rng >> 32);
}

static uint8_t *read_all(const char *dir, const char *name, size_t *size)
{
  char path[1024];
  snprintf(path, sizeof path, "%s/%s", dir, name);
  FILE *f = fopen(path, "rb");
  if (!f) {
    printf("cannot open %s\n", path);
    return NULL;
  }
  fseek(f, 0, SEEK_END);
  const long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t *data = (uint8_t *)malloc((size_t)n);
  if (!data || fread(data, 1, (size_t)n, f) != (size_t)n) {
    printf("cannot read %s\n", path);
    fclose(f);
    free(data);
    return NULL;
  }
  fclose(f);
  *size = (size_t)n;
  return data;
}

/* the first sample at which two blocks differ, or -1 */
static long first_difference(const float *a, const float *b, size_t n)
{
  for (size_t i = 0; i < n; i++)
    if (memcmp(&a[i], &b[i], sizeof(float))) return (long)i;
  return -1;
}

/* the PCM of `frames` frames at `position` as decoding from the start gives it */
static int decode_from_start(const uint8_t *mem, size_t size, uint64_t position, float *out, uint32_t frames)
{
  ogg *o = NULL;
  if (ogg_open(mem, size, &o) != OGG_OK) return 0;
  static float discard[4096 * 2];
  uint64_t left = position;
  while (left) {
    const uint32_t n = left > 4096 ? 4096 : (uint32_t)left;
    ogg_render(o, discard, n);
    left -= n;
  }
  const uint32_t got = ogg_render(o, out, frames);
  ogg_close(o);
  return got == frames;
}

static void test_song(const uint8_t *mem, size_t size, float *reference)
{
  ogg *o = NULL;
  CHECK(ogg_open(mem, size, &o) == OGG_OK && o, "the song opens");
  if (!o) return;
  CHECK(ogg_rate(o) == RATE, "rate %u", ogg_rate(o));
  CHECK(ogg_length_frames(o) == SONG_FRAMES, "length %llu frames", (unsigned long long)ogg_length_frames(o));
  CHECK(ogg_position(o) == 0, "starts at 0");
  CHECK(((double)ogg_length_frames(o) / RATE - SONG_SECONDS) < 1.0 / RATE && (SONG_SECONDS - (double)ogg_length_frames(o) / RATE) < 1.0 / RATE,
        "length %.6f s against ffprobe's %.6f s", (double)ogg_length_frames(o) / RATE, SONG_SECONDS);

  uint32_t got = 0;
  for (uint32_t at = 0; at < REF_FRAMES;) {
    const uint32_t n = REF_FRAMES - at > 4096 ? 4096 : REF_FRAMES - at;
    got += ogg_render(o, reference + (size_t)at * 2, n);
    at += n;
  }
  CHECK(got == REF_FRAMES, "%u of %u frames rendered", got, REF_FRAMES);
  CHECK(ogg_position(o) == REF_FRAMES, "position %llu", (unsigned long long)ogg_position(o));

  double peak = 0, energy = 0;
  size_t nonfinite = 0;
  for (size_t i = 0; i < (size_t)REF_FRAMES * 2; i++) {
    const double x = reference[i];
    if (!(x == x) || x > 1e30 || x < -1e30) nonfinite++;
    if (x > peak) peak = x;
    if (-x > peak) peak = -x;
    energy += x * x;
  }
  CHECK(!nonfinite, "%zu non-finite samples", nonfinite);
  CHECK(peak > 0.05 && peak < 4.0, "peak %.3f", peak);
  CHECK(energy / ((double)REF_FRAMES * 2) > 1e-5, "the first 30 s are not silent (mean square %.3g)", energy / ((double)REF_FRAMES * 2));
  ogg_close(o);
}

static void test_blocks(const uint8_t *mem, size_t size, const float *reference)
{
  ogg *o = NULL;
  if (ogg_open(mem, size, &o) != OGG_OK) return;
  const uint32_t total = 3 * RATE;
  float *out = (float *)malloc((size_t)total * 2 * sizeof(float));
  if (!out) {
    ogg_close(o);
    return;
  }
  uint32_t at = 0;
  while (at < total) { /* blocks of 1 to 5000 frames, the sizes of an audio callback that is not a power of two */
    uint32_t n = 1 + rnd() % 5000;
    if (n > total - at) n = total - at;
    CHECK(ogg_render(o, out + (size_t)at * 2, n) == n, "block of %u frames at %u", n, at);
    at += n;
  }
  const long d = first_difference(out, reference, (size_t)total * 2);
  CHECK(d < 0, "irregular blocks differ from 4096-frame blocks at sample %ld", d);
  ogg_close(o);
  free(out);
}

static void test_seek(const uint8_t *mem, size_t size, const float *reference)
{
  ogg *o = NULL;
  if (ogg_open(mem, size, &o) != OGG_OK) return;
  enum { BLOCK = 2048 };
  float out[BLOCK * 2];
  /* inside the reference, in a scrambled order: backward and forward, the start, a page boundary guess and the last block */
  static const uint64_t within[] = { 0, 1, 44099, 44100, 1000003, 12345, 0, 777777, REF_FRAMES - BLOCK, 2047, 30000, 4096, 65536, 100 };
  for (size_t i = 0; i < sizeof within / sizeof within[0]; i++) {
    CHECK(ogg_seek(o, within[i]) == OGG_OK, "seek to %llu", (unsigned long long)within[i]);
    CHECK(ogg_position(o) == within[i], "position after the seek to %llu is %llu", (unsigned long long)within[i], (unsigned long long)ogg_position(o));
    CHECK(ogg_render(o, out, BLOCK) == BLOCK, "render after the seek to %llu", (unsigned long long)within[i]);
    const long d = first_difference(out, reference + within[i] * 2, BLOCK * 2);
    CHECK(d < 0, "after the seek to %llu the PCM differs at sample %ld", (unsigned long long)within[i], d);
  }
  /* far away: against a decoder that ran from the start to there */
  static const uint64_t far_frames[] = { 120ull * RATE + 17, SONG_FRAMES - 3 * RATE, 61ull * RATE };
  for (size_t i = 0; i < sizeof far_frames / sizeof far_frames[0]; i++) {
    float want[BLOCK * 2];
    CHECK(decode_from_start(mem, size, far_frames[i], want, BLOCK), "decoding from the start to %llu", (unsigned long long)far_frames[i]);
    CHECK(ogg_seek(o, far_frames[i]) == OGG_OK, "seek to %llu", (unsigned long long)far_frames[i]);
    CHECK(ogg_render(o, out, BLOCK) == BLOCK, "render at %llu", (unsigned long long)far_frames[i]);
    const long d = first_difference(out, want, BLOCK * 2);
    CHECK(d < 0, "after the seek to %llu the PCM differs at sample %ld", (unsigned long long)far_frames[i], d);
  }
  ogg_close(o);
}

static void test_end(const uint8_t *mem, size_t size)
{
  ogg *o = NULL;
  if (ogg_open(mem, size, &o) != OGG_OK) return;
  float out[256 * 2];
  const uint64_t length = ogg_length_frames(o);
  CHECK(ogg_seek(o, length - 100) == OGG_OK, "seek to 100 frames before the end");
  CHECK(ogg_render(o, out, 256) == 100, "100 frames are left, not more");
  int silent = 1;
  for (int i = 100 * 2; i < 256 * 2; i++) silent &= out[i] == 0.0f;
  CHECK(silent, "the rest of the block is silence");
  CHECK(ogg_position(o) == length, "position at the end is the length");
  CHECK(ogg_render(o, out, 256) == 0, "nothing after the end");
  CHECK(ogg_seek(o, length) == OGG_OK && ogg_position(o) == length && ogg_render(o, out, 16) == 0, "seek to the length is the end");
  CHECK(ogg_seek(o, length + 123456789ull) == OGG_OK && ogg_position(o) == length && ogg_render(o, out, 16) == 0, "seek past the length is the end");
  CHECK(ogg_seek(o, length - 1) == OGG_OK && ogg_render(o, out, 16) == 1, "one frame left");
  CHECK(ogg_seek(o, 0) == OGG_OK && ogg_render(o, out, 256) == 256, "back to the start after the end");
  ogg_close(o);
}

static void test_damage(const uint8_t *mem, size_t size)
{
  ogg *o = (ogg *)1;
  CHECK(ogg_open(NULL, 10, &o) == OGG_ERR_ARGUMENT && !o, "no data");
  CHECK(ogg_open(mem, 0, &o) == OGG_ERR_ARGUMENT, "no size");
  CHECK(ogg_open(mem, size, NULL) == OGG_ERR_ARGUMENT, "no result");
  static const uint8_t junk[4096] = { 'n', 'o', 't', ' ', 'o', 'g', 'g' };
  CHECK(ogg_open(junk, sizeof junk, &o) == OGG_ERR_FORMAT && !o, "garbage");
  CHECK(ogg_open(mem, 100, &o) == OGG_ERR_FORMAT && !o, "100 bytes of the song");
  CHECK(ogg_open(mem, 5000, &o) == OGG_ERR_FORMAT && !o, "the headers cut off");
  ogg_close(NULL);
  CHECK(ogg_render(NULL, (float[2]){ 1, 1 }, 0) == 0, "render without a stream");
  CHECK(ogg_seek(NULL, 0) == OGG_ERR_ARGUMENT, "seek without a stream");

  /* the song with a few flipped bytes (the tables of the decoder are not in the file: a flip lands in audio packets or in the
     setup header, which is rejected or decoded to something finite) */
  uint8_t *copy = (uint8_t *)malloc(size);
  float *out = (float *)malloc((size_t)8192 * 2 * sizeof(float));
  if (!copy || !out) {
    free(copy);
    free(out);
    return;
  }
  int opened = 0, rejected = 0;
  size_t nonfinite = 0;
  for (int variant = 0; variant < 24; variant++) {
    memcpy(copy, mem, size);
    const size_t zone = variant % 3 == 0 ? 4000 : variant % 3 == 1 ? 200000 : size; /* setup header, early audio, anywhere */
    for (int k = 0; k < 6; k++) copy[rnd() % zone] ^= (uint8_t)(1u << (rnd() % 8));
    ogg *m = NULL;
    if (ogg_open(copy, size, &m) != OGG_OK) {
      rejected++;
      continue;
    }
    opened++;
    for (int block = 0; block < 12; block++) {
      ogg_render(m, out, 8192);
      for (size_t i = 0; i < (size_t)8192 * 2; i++)
        if (!(out[i] == out[i]) || out[i] > 1e30f || out[i] < -1e30f) nonfinite++;
    }
    ogg_seek(m, rnd() % (uint32_t)ogg_length_frames(m));
    ogg_render(m, out, 4096);
    ogg_close(m);
  }
  CHECK(!nonfinite, "%zu non-finite samples from damaged streams", nonfinite);
  printf("  damaged songs: %d opened, %d rejected, no crash\n", opened, rejected);
  free(copy);
  free(out);
}

static void test_paudio(const uint8_t *mem, size_t size, const float *reference)
{
  paudio a;
  paudio_init(&a, RATE);
  CHECK(paudio_play_party_ogg(&a, mem, size, 0) == OGG_OK && a.stage == PAUDIO_PARTY && !a.synth && a.ogg, "play the Ogg as the party song");
  CHECK(a.length_frames == SONG_FRAMES && a.party_base == 0 && a.party_frames == 0, "length and bookkeeping");

  const uint32_t total = 3 * RATE;
  float *out = (float *)malloc((size_t)total * 2 * sizeof(float));
  if (!out) {
    paudio_destroy(&a);
    return;
  }
  uint32_t at = 0;
  while (at < total) { /* the sizes an audio callback asks for */
    uint32_t n = 1 + rnd() % 4096;
    if (n > total - at) n = total - at;
    paudio_render(&a, out + (size_t)at * 2, n);
    at += n;
  }
  CHECK(first_difference(out, reference, (size_t)total * 2) < 0, "paudio renders the stream's PCM");
  CHECK(a.party_frames == total, "party_frames counts what was rendered: %llu", (unsigned long long)a.party_frames);

  paudio_set_paused(&a, 1);
  paudio_render(&a, out, 4096);
  int silent = 1;
  for (int i = 0; i < 4096 * 2; i++) silent &= out[i] == 0.0f;
  CHECK(silent && a.party_frames == total, "paused: silence, and the stream does not advance");
  paudio_set_paused(&a, 0);
  paudio_render(&a, out, 1024);
  CHECK(first_difference(out, reference + (size_t)total * 2, 1024 * 2) < 0 || (size_t)total + 1024 > REF_FRAMES,
        "after the pause the song goes on where it was");

  CHECK(paudio_seek_sample(&a, 1000003) == 0 && a.party_base == 1000003 && a.party_frames == 0, "seek to a sample: bookkeeping");
  paudio_render(&a, out, 2048);
  CHECK(first_difference(out, reference + 1000003ull * 2, 2048 * 2) < 0, "seek to a sample: exact PCM");
  CHECK(paudio_seek(&a, 1500) == V2_OK && a.party_base == 66150 && a.party_frames == 0, "seek to a time: the sample of 1500 ms");
  paudio_render(&a, out, 2048);
  CHECK(first_difference(out, reference + 66150ull * 2, 2048 * 2) < 0, "seek to a time: PCM");
  CHECK(paudio_seek_sample(&a, SONG_FRAMES + 5000) == 0, "seek past the end");
  paudio_render(&a, out, 64);
  silent = 1;
  for (int i = 0; i < 64 * 2; i++) silent &= out[i] == 0.0f;
  CHECK(silent, "silence past the end");
  paudio_stop(&a);
  CHECK(paudio_seek_sample(&a, 0) == -1 && paudio_seek(&a, 0) == V2_ERR_BAD_ARGUMENT && !a.ogg && a.stage == PAUDIO_SILENT, "no seek without a song");

  CHECK(paudio_play_party_ogg(&a, mem, size, 123456) == OGG_OK && a.party_base == 123456, "start offset");
  paudio_render(&a, out, 2048);
  CHECK(first_difference(out, reference + 123456ull * 2, 2048 * 2) < 0, "start offset: PCM");
  CHECK(paudio_play_party_ogg(&a, mem, 5000, 0) == OGG_ERR_FORMAT && a.stage == PAUDIO_SILENT && !a.ogg, "cut-off stream");
  paudio_destroy(&a);

  paudio b;
  paudio_init(&b, 48000);
  CHECK(paudio_play_party_ogg(&b, mem, size, 0) == OGG_ERR_FORMAT && b.stage == PAUDIO_SILENT && !b.ogg, "a stream of another rate is refused");
  paudio_destroy(&b);
  free(out);
}

static double fabs_d(double x) { return x < 0 ? -x : x; }

static void test_math(void)
{
  const double pi = 3.14159265358979323846;
  CHECK(fabs_d(ogg__sin(pi / 6) - 0.5) < 1e-15, "sin(pi/6) = %.17g", ogg__sin(pi / 6));
  CHECK(fabs_d(ogg__cos(pi / 3) - 0.5) < 1e-15, "cos(pi/3) = %.17g", ogg__cos(pi / 3));
  CHECK(fabs_d(ogg__sin(pi / 2) - 1.0) < 1e-15 && fabs_d(ogg__cos(pi)) - 1.0 > -1e-15, "sin and cos at the quarter turns");
  CHECK(fabs_d(ogg__sin(-pi / 4) + 0.70710678118654752) < 1e-15, "sin(-pi/4)");
  CHECK(fabs_d(ogg__sin(1000.0) - 0.82687954053200256) < 1e-12, "sin(1000) = %.17g", ogg__sin(1000.0));
  CHECK(fabs_d(ogg__exp(1.0) - 2.7182818284590452) < 1e-15, "exp(1) = %.17g", ogg__exp(1.0));
  CHECK(fabs_d(ogg__exp(-3.5) - 0.030197383422318501) < 1e-16, "exp(-3.5) = %.17g", ogg__exp(-3.5));
  CHECK(ogg__exp(-1e9) == 0.0, "exp of a very negative number");
  CHECK(fabs_d(ogg__log(10.0) - 2.3025850929940457) < 1e-15, "log(10) = %.17g", ogg__log(10.0));
  CHECK(fabs_d(ogg__log(0.001) + 6.9077552789821371) < 1e-14, "log(0.001) = %.17g", ogg__log(0.001));
  CHECK(fabs_d(ogg__log(1.0)) < 1e-16, "log(1)");
  CHECK(ogg__pow(3.0, 4.0) == 81.0 && ogg__pow(2.0, 0.0) == 1.0 && ogg__pow(7.0, 1.0) == 7.0, "pow with whole exponents is exact");
  CHECK(fabs_d(ogg__pow(2.0, 0.5) - 1.4142135623730951) < 1e-14, "pow(2, 0.5) = %.17g", ogg__pow(2.0, 0.5));
  /* the codebook rule of the decoder: the largest r with r^dim <= entries */
  for (int dim = 1; dim <= 8; dim++)
    for (int r = 1; r <= 12; r++) {
      double e = 1;
      for (int k = 0; k < dim; k++) e *= r;
      CHECK((int)floor(ogg__exp(ogg__log(e) / dim)) >= r - 1 && ogg__pow((double)r, dim) == e, "lookup values for %d^%d", r, dim);
    }
}

int suite_audio_ogg(int argc, char **argv)
{
  if (argc < 2) {
    printf("usage: audio_ogg ASSETS_DIR\n");
    return 2;
  }
  printf("ogg.h\n");
  size_t size = 0;
  uint8_t *mem = read_all(argv[1], "theta.ogg", &size);
  float *reference = (float *)malloc((size_t)REF_FRAMES * 2 * sizeof(float));
  if (!mem || !reference) {
    free(mem);
    free(reference);
    return 1;
  }
  test_math();
  test_song(mem, size, reference);
  test_blocks(mem, size, reference);
  test_seek(mem, size, reference);
  test_end(mem, size);
  test_paudio(mem, size, reference);
  test_damage(mem, size);
  free(reference);
  free(mem);
  printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
