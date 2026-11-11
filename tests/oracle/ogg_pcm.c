/* ogg_pcm: ogg.h against the PCM of an independent decoder (ffmpeg, raw little-endian float, 2 channels).
 *
 *   ogg_pcm SONG.ogg REFERENCE.f32 [MAX_RMS_LSB MAX_ABS_LSB]
 *
 * Decodes the whole song in blocks of irregular size and compares every sample with the reference; then seeks to a set of
 * positions and compares the PCM that follows with the reference at the same position (the seek test: PCM after ogg_seek
 * equals the PCM decoded from the start, with the other decoder as the one that decoded from the start).
 * Differences are in units of one 16-bit step (1/32768). Exit 0 when the RMS and the largest difference stay below the limits
 * (default 0.05 and 0.5: up to 1 LSB RMS is acceptable; measured 0.002 and 0.02 against ffmpeg) and the lengths agree.
 */
#define OGG_IMPLEMENTATION
#include <theta/audio/ogg.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct stats {
  double sum_sq, max_abs;
  uint64_t samples, over_one;
} stats;

static void compare(stats *s, const float *got, const float *want, size_t n)
{
  for (size_t i = 0; i < n; i++) {
    const double d = ((double)got[i] - (double)want[i]) * 32768.0;
    const double a = d < 0 ? -d : d;
    s->sum_sq += d * d;
    if (a > s->max_abs) s->max_abs = a;
    if (a > 1.0) s->over_one++;
  }
  s->samples += n;
}

static double rms(const stats *s) { return s->samples ? sqrt(s->sum_sq / (double)s->samples) : 0.0; }

static uint8_t *read_all(const char *path, size_t *size)
{
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  const long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t *d = (uint8_t *)malloc((size_t)n);
  if (!d || fread(d, 1, (size_t)n, f) != (size_t)n) {
    free(d);
    fclose(f);
    return NULL;
  }
  fclose(f);
  *size = (size_t)n;
  return d;
}

int main(int argc, char **argv)
{
  if (argc < 3) {
    fprintf(stderr, "usage: %s SONG.ogg REFERENCE.f32 [MAX_RMS_LSB MAX_ABS_LSB]\n", argv[0]);
    return 2;
  }
  const double max_rms = argc > 3 ? atof(argv[3]) : 0.05, max_abs = argc > 4 ? atof(argv[4]) : 0.5;
  size_t size = 0;
  uint8_t *mem = read_all(argv[1], &size);
  FILE *ref = fopen(argv[2], "rb");
  if (!mem || !ref) {
    fprintf(stderr, "ogg_pcm: cannot read the inputs\n");
    return 2;
  }
  fseek(ref, 0, SEEK_END);
  const uint64_t ref_frames = (uint64_t)ftell(ref) / 8;
  fseek(ref, 0, SEEK_SET);
  ogg *o = NULL;
  const ogg_result r = ogg_open(mem, size, &o);
  if (r != OGG_OK) {
    fprintf(stderr, "ogg_pcm: %s\n", ogg_result_string(r));
    return 1;
  }
  int ok = 1;
  const uint64_t length = ogg_length_frames(o);
  printf("ogg_pcm: %llu frames at %u Hz; the reference has %llu\n", (unsigned long long)length, ogg_rate(o), (unsigned long long)ref_frames);
  if (length != ref_frames) {
    printf("ogg_pcm: the lengths differ\n");
    ok = 0;
  }

  enum { MAX_BLOCK = 5000 };
  static float got[MAX_BLOCK * 2], want[MAX_BLOCK * 2];
  uint64_t rng = 88172645463325252ull, at = 0;
  stats all = { 0 };
  while (at < length && at < ref_frames) {
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    uint32_t n = 1 + (uint32_t)((rng >> 33) % MAX_BLOCK);
    if (n > length - at) n = (uint32_t)(length - at);
    if (n > ref_frames - at) n = (uint32_t)(ref_frames - at);
    if (ogg_render(o, got, n) != n || fread(want, 8, n, ref) != n) {
      printf("ogg_pcm: short read at frame %llu\n", (unsigned long long)at);
      ok = 0;
      break;
    }
    compare(&all, got, want, (size_t)n * 2);
    at += n;
  }
  printf("ogg_pcm: whole song: rms %.4f LSB, largest %.3f LSB, %llu of %llu samples more than 1 LSB off\n", rms(&all), all.max_abs,
         (unsigned long long)all.over_one, (unsigned long long)all.samples);
  if (rms(&all) > max_rms || all.max_abs > max_abs) ok = 0;

  stats seeks = { 0 };
  static const double seconds[] = { 0.0, 0.5, 31.25, 7.0, 130.0, 259.0, 12.0, 200.5 };
  for (size_t i = 0; i < sizeof seconds / sizeof seconds[0]; i++) {
    const uint64_t frame = (uint64_t)(seconds[i] * ogg_rate(o));
    if (ogg_seek(o, frame) != OGG_OK) {
      printf("ogg_pcm: seek to %.2f s failed\n", seconds[i]);
      ok = 0;
      continue;
    }
    uint32_t n = 4096;
    if (n > ref_frames - frame) n = (uint32_t)(ref_frames - frame);
    fseek(ref, (long)(frame * 8), SEEK_SET);
    if (ogg_render(o, got, n) != n || fread(want, 8, n, ref) != n) {
      printf("ogg_pcm: short read after the seek to %.2f s\n", seconds[i]);
      ok = 0;
      continue;
    }
    compare(&seeks, got, want, (size_t)n * 2);
  }
  printf("ogg_pcm: after %zu seeks: rms %.4f LSB, largest %.3f LSB\n", sizeof seconds / sizeof seconds[0], rms(&seeks), seeks.max_abs);
  if (rms(&seeks) > max_rms || seeks.max_abs > max_abs) ok = 0;
  ogg_close(o);
  fclose(ref);
  free(mem);
  printf(ok ? "ogg_pcm: ok\n" : "ogg_pcm: FAILED\n");
  return ok ? 0 : 1;
}
