/* ogg.h - the soundtrack of theta: an Ogg Vorbis stream decoded on demand (stb-style single header around stb_vorbis)
 *
 * The same pull model as the V2 synthesizer: the audio callback asks for frames and gets interleaved stereo float.
 * The compressed file (4 MB) stays in memory and is decoded as it plays, so the PCM that is alive is one block, not the
 * 46 MB of the whole song (too much for a phone browser). Nothing here touches SDL, a thread or a clock.
 *
 *   #define OGG_IMPLEMENTATION      in exactly one source file of the program (the functions of stb_vorbis are external, so
 *                                   there is no static variant: two files with the implementation do not link)
 *   #include <theta/audio/ogg.h>
 *
 * The implementation includes third_party/stb/stb_vorbis.c (v1.22, public domain / MIT) and replaces the transcendental
 * functions it calls while it builds its tables (sin, cos, exp, log, pow) by small ones of this file, so that the decoded
 * samples do not depend on the C library of the machine. Define OGG_STB_VORBIS to include the decoder from another place.
 */
#ifndef OGG_H_INCLUDED
#define OGG_H_INCLUDED

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef OGG_API
#define OGG_API extern
#endif

typedef enum ogg_result {
  OGG_OK = 0,
  OGG_ERR_ARGUMENT,
  OGG_ERR_FORMAT, /* not an Ogg Vorbis stream, or damaged before the first audio */
  OGG_ERR_MEMORY,
  OGG_ERR_SEEK    /* the stream could not be repositioned; it is back at its start */
} ogg_result;

typedef struct ogg ogg;

/* `mem` must stay valid until ogg_close. The stream has to be stereo or mono (a mono one is played on both channels) and to
   have at least one frame. */
OGG_API ogg_result ogg_open(const void *mem, size_t size, ogg **out);
OGG_API void ogg_close(ogg *o);
OGG_API const char *ogg_result_string(ogg_result r);

OGG_API uint32_t ogg_rate(const ogg *o);
/* the length of the stream in frames, from its last page */
OGG_API uint64_t ogg_length_frames(const ogg *o);
/* the next frame ogg_render will produce */
OGG_API uint64_t ogg_position(const ogg *o);

/* Writes `frames` interleaved stereo frames; returns how many came from the stream. The rest (past the end) is silence. */
OGG_API uint32_t ogg_render(ogg *o, float *out, uint32_t frames);
/* Makes `frame` the next frame to render, sample accurate (a position past the end means the end). */
OGG_API ogg_result ogg_seek(ogg *o, uint64_t frame);

#ifdef __cplusplus
}
#endif

#endif /* OGG_H_INCLUDED */

#ifdef OGG_IMPLEMENTATION
#ifndef OGG_IMPLEMENTATION_DONE
#define OGG_IMPLEMENTATION_DONE

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---- the math of the decoder's set-up (double precision, no libm) ---- */

#define OGG__PI  3.14159265358979323846
#define OGG__LN2 0.69314718055994530942

/* sin of (x + quarter turns * pi / 2) */
static double ogg__sin_quarter(double x, int quarters)
{
  const double k = floor(x * (2.0 / OGG__PI) + 0.5);
  const double r = x - k * (OGG__PI / 2.0);
  const int q = (int)(((long long)k + quarters) & 3);
  const double r2 = r * r;
  double term, sum;
  if (q & 1) { /* cos r */
    term = sum = 1.0;
    for (int n = 1; n <= 9; n++) {
      term *= -r2 / ((double)(2 * n - 1) * (double)(2 * n));
      sum += term;
    }
  } else { /* sin r */
    term = sum = r;
    for (int n = 1; n <= 9; n++) {
      term *= -r2 / ((double)(2 * n) * (double)(2 * n + 1));
      sum += term;
    }
  }
  return q >= 2 ? -sum : sum;
}
static double ogg__sin(double x) { return ogg__sin_quarter(x, 0); }
static double ogg__cos(double x) { return ogg__sin_quarter(x, 1); }

static double ogg__exp(double x)
{
  if (x < -700.0) return 0.0;
  if (x > 700.0) x = 700.0;
  const double k = floor(x / OGG__LN2 + 0.5);
  const double r = x - k * OGG__LN2;
  double term = 1.0, sum = 1.0;
  for (int n = 1; n <= 18; n++) {
    term *= r / (double)n;
    sum += term;
  }
  return ldexp(sum, (int)k);
}

static double ogg__log(double x)
{
  if (!(x > 0.0)) return -1e300;
  int e;
  double m = frexp(x, &e); /* [0.5, 1) */
  if (m < 0.70710678118654752) {
    m *= 2.0;
    e--;
  }
  const double s = (m - 1.0) / (m + 1.0), s2 = s * s;
  double term = s, sum = s;
  for (int n = 1; n <= 16; n++) {
    term *= s2;
    sum += term / (double)(2 * n + 1);
  }
  return (double)e * OGG__LN2 + 2.0 * sum;
}

/* whole exponents are products, so that pow(3, 4) is exactly 81 where the decoder compares it with an integer */
static double ogg__pow(double b, double e)
{
  if (e >= 0.0 && e <= 64.0 && e == floor(e)) {
    double r = 1.0;
    for (int n = (int)e; n > 0; n--) r *= b;
    return r;
  }
  return b > 0.0 ? ogg__exp(e * ogg__log(b)) : 0.0;
}

#ifndef OGG_STB_VORBIS
#define OGG_STB_VORBIS "../../../third_party/stb/stb_vorbis.c"
#endif

#define STB_VORBIS_NO_STDIO              1
#define STB_VORBIS_NO_PUSHDATA_API       1
#define STB_VORBIS_NO_INTEGER_CONVERSION 1
#define sin(x)                           ogg__sin(x)
#define cos(x)                           ogg__cos(x)
#define exp(x)                           ogg__exp(x)
#define log(x)                           ogg__log(x)
#define pow(x, y)                        ogg__pow(x, y)
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#pragma GCC diagnostic ignored "-Wfloat-equal"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wcomment"
#pragma GCC diagnostic ignored "-Wtautological-compare"
#if !defined(__clang__)
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#endif
#include OGG_STB_VORBIS
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
#undef sin
#undef cos
#undef exp
#undef log
#undef pow
/* stb_vorbis leaves its internal macros behind; the names of this list are common enough to collide with the includer's */
#undef ADDEND
#undef C
#undef CHECK
#undef CODEBOOK_ELEMENT
#undef CODEBOOK_ELEMENT_BASE
#undef CODEBOOK_ELEMENT_FAST
#undef CRC32_POLY
#undef DECODE
#undef DECODE_RAW
#undef DECODE_VQ
#undef DIVTAB_DENOM
#undef DIVTAB_NUMER
#undef EOP
#undef FALSE
#undef FASTDEF
#undef FAST_HUFFMAN_TABLE_MASK
#undef FAST_HUFFMAN_TABLE_SIZE
#undef INVALID_BITS
#undef IS_PUSH_MODE
#undef L
#undef LIBVORBIS_MDCT
#undef LINE_OP
#undef MAGIC
#undef MAX_BLOCKSIZE
#undef MAX_BLOCKSIZE_LOG
#undef NO_CODE
#undef PAGEFLAG_continued_packet
#undef PAGEFLAG_first_page
#undef PAGEFLAG_last_page
#undef PLAYBACK_LEFT
#undef PLAYBACK_MONO
#undef PLAYBACK_RIGHT
#undef R
#undef SAMPLE_unknown
#undef TRUE
#undef USE_MEMORY
#undef array_size_required
#undef check_endianness
#undef temp_alloc
#undef temp_alloc_restore
#undef temp_alloc_save
#undef temp_block_array
#undef temp_free

struct ogg {
  stb_vorbis *v;
  uint32_t rate;
  uint64_t length;
  uint64_t pos;
};

OGG_API const char *ogg_result_string(ogg_result r)
{
  switch (r) {
  case OGG_OK: return "ok";
  case OGG_ERR_ARGUMENT: return "bad argument";
  case OGG_ERR_FORMAT: return "not an Ogg Vorbis stream";
  case OGG_ERR_MEMORY: return "out of memory";
  case OGG_ERR_SEEK: return "seek failed";
  }
  return "unknown";
}

OGG_API ogg_result ogg_open(const void *mem, size_t size, ogg **out)
{
  if (!out) return OGG_ERR_ARGUMENT;
  *out = NULL;
  if (!mem || size == 0 || size > (size_t)INT_MAX) return OGG_ERR_ARGUMENT;
  int err = 0;
  stb_vorbis *v = stb_vorbis_open_memory((const unsigned char *)mem, (int)size, &err, NULL);
  if (!v) return err == VORBIS_outofmem ? OGG_ERR_MEMORY : OGG_ERR_FORMAT;
  const stb_vorbis_info info = stb_vorbis_get_info(v);
  const unsigned length = stb_vorbis_stream_length_in_samples(v);
  if (info.channels < 1 || info.channels > 2 || info.sample_rate == 0 || length == 0) {
    stb_vorbis_close(v);
    return OGG_ERR_FORMAT;
  }
  ogg *o = (ogg *)calloc(1, sizeof *o);
  if (!o) {
    stb_vorbis_close(v);
    return OGG_ERR_MEMORY;
  }
  o->v = v;
  o->rate = info.sample_rate;
  o->length = length;
  *out = o;
  return OGG_OK;
}

OGG_API void ogg_close(ogg *o)
{
  if (!o) return;
  stb_vorbis_close(o->v);
  free(o);
}

OGG_API uint32_t ogg_rate(const ogg *o) { return o ? o->rate : 0; }
OGG_API uint64_t ogg_length_frames(const ogg *o) { return o ? o->length : 0; }
OGG_API uint64_t ogg_position(const ogg *o) { return o ? o->pos : 0; }

OGG_API uint32_t ogg_render(ogg *o, float *out, uint32_t frames)
{
  uint32_t done = 0;
  while (o && done < frames) {
    const int got = stb_vorbis_get_samples_float_interleaved(o->v, 2, out + (size_t)done * 2, (int)((frames - done) * 2));
    if (got <= 0) break;
    done += (uint32_t)got;
  }
  if (done < frames) memset(out + (size_t)done * 2, 0, (size_t)(frames - done) * 2 * sizeof(float));
  if (o) o->pos += done;
  return done;
}

OGG_API ogg_result ogg_seek(ogg *o, uint64_t frame)
{
  if (!o) return OGG_ERR_ARGUMENT;
  const int at_end = frame >= o->length;
  const uint64_t target = at_end ? o->length - 1 : frame;
  if (!stb_vorbis_seek(o->v, (unsigned)target)) {
    stb_vorbis_seek_start(o->v);
    o->pos = 0;
    return OGG_ERR_SEEK;
  }
  o->pos = target;
  if (at_end) { /* the last frame is read and dropped */
    float last[2];
    ogg_render(o, last, 1);
  }
  return OGG_OK;
}

#endif /* OGG_IMPLEMENTATION_DONE */
#endif /* OGG_IMPLEMENTATION */
