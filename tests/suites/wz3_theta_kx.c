/* Tests for the 2004 document layout and the shipped theta document (assets/theta.kx).
 *
 *   suite_wz3_theta_kx ASSETS_DIR FIXTURE_DIR
 *
 * 1. The document of fr-038: theta parses; its structure equals the recorded one: a fingerprint over every operator,
 *    parameter, event, spline key and blob byte, the section offsets and the class histogram. The last section ends on the
 *    last byte, the first blob is the JPEG import (operator 759).
 * 2. Robustness: truncation and bit flips never crash and are rejected or accepted cleanly. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "support/suite.h"

#define WZ3_IMPLEMENTATION
#include <theta/core/wz3.h>

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

static uint8_t *read_all(const char *dir, const char *name, size_t *size)
{
  char path[1024];
  if (name)
    snprintf(path, sizeof path, "%s/%s", dir, name);
  else
    snprintf(path, sizeof path, "%s", dir);
  FILE *f = fopen(path, "rb");
  if (!f) {
    printf("cannot open %s\n", path);
    return NULL;
  }
  fseek(f, 0, SEEK_END);
  const long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t *data = (uint8_t *)malloc((size_t)n + 1);
  if (!data || fread(data, 1, (size_t)n, f) != (size_t)n) {
    printf("cannot read %s\n", path);
    fclose(f);
    free(data);
    return NULL;
  }
  fclose(f);
  data[n] = 0;
  *size = (size_t)n;
  return data;
}

static uint32_t f2u(float f)
{
  uint32_t u;
  memcpy(&u, &f, 4);
  return u;
}

/* ---- fingerprint (an FNV-1a stream over the structure of the document) ---------------------------------------------- */

typedef struct fnv {
  uint32_t h;
} fnv;
static void fnv_byte(fnv *f, uint32_t b) { f->h = (f->h ^ (b & 255u)) * 0x01000193u; }
static void fnv_u32(fnv *f, uint32_t v)
{
  fnv_byte(f, v);
  fnv_byte(f, v >> 8);
  fnv_byte(f, v >> 16);
  fnv_byte(f, v >> 24);
}
static void fnv_bytes(fnv *f, const uint8_t *p, size_t n)
{
  for (size_t i = 0; i < n; i++) fnv_byte(f, p[i]);
}

static uint32_t kx_fingerprint(const wz3_kx *kx)
{
  fnv h = { 0x811c9dc5u };
  fnv_u32(&h, kx->flags);
  fnv_u32(&h, kx->song_size);
  fnv_u32(&h, kx->sample_size);
  fnv_u32(&h, kx->song_bpm_fixed);
  fnv_u32(&h, kx->song_length);
  fnv_u32(&h, kx->n_ops);
  fnv_u32(&h, kx->n_events);
  fnv_u32(&h, kx->n_splines);
  fnv_u32(&h, kx->n_classes);
  for (int i = 0; i < WZ3_MAX_OP_ROOT; i++) fnv_u32(&h, kx->roots[i]);
  const wz3_kx_sections *s = &kx->sections;
  fnv_u32(&h, s->header);
  fnv_u32(&h, s->song);
  fnv_u32(&h, s->samples);
  fnv_u32(&h, s->document);
  fnv_u32(&h, s->graph);
  fnv_u32(&h, s->parameters);
  fnv_u32(&h, s->animations);
  fnv_u32(&h, s->events);
  fnv_u32(&h, s->splines);
  fnv_u32(&h, s->blobs);
  fnv_u32(&h, s->end);
  fnv song = { 0x811c9dc5u };
  fnv_bytes(&song, kx->song, kx->song_size);
  fnv_u32(&h, song.h);
  fnv samples = { 0x811c9dc5u };
  fnv_bytes(&samples, kx->samples, kx->sample_size);
  fnv_u32(&h, samples.h);
  for (uint32_t c = 0; c < kx->n_classes; c++) {
    const wz3_kx_class *cls = &kx->classes[c];
    fnv_u32(&h, cls->convention);
    fnv_u32(&h, cls->id);
    fnv_u32(&h, cls->packing_length);
    for (uint32_t i = 0; i < cls->packing_length; i++) fnv_u32(&h, (uint8_t)cls->packing[i]);
  }
  for (uint32_t o = 0; o < kx->n_ops; o++) {
    const wz3_kx_op *op = &kx->ops[o];
    const wz3_kx_class *cls = &kx->classes[op->class_index];
    fnv_u32(&h, op->class_index);
    fnv_u32(&h, op->n_inputs);
    for (uint32_t i = 0; i < op->n_inputs; i++) fnv_u32(&h, op->inputs[i]);
    fnv_u32(&h, op->n_links);
    for (uint32_t i = 0; i < op->n_links; i++) fnv_u32(&h, (uint32_t)op->links[i]);
    for (uint32_t i = 0; i < cls->packing_length; i++) fnv_u32(&h, op->params[i].u);
    fnv_u32(&h, op->n_strings);
    for (uint32_t i = 0; i < op->n_strings; i++) {
      const size_t len = strlen(op->strings[i]);
      fnv_u32(&h, (uint32_t)len);
      for (size_t k = 0; k < len; k++) fnv_u32(&h, (uint8_t)op->strings[i][k]);
    }
    fnv_u32(&h, op->n_splines);
    for (uint32_t i = 0; i < op->n_splines; i++) fnv_u32(&h, (uint32_t)op->splines[i]);
    fnv_u32(&h, op->blob_size);
    if (op->blob) fnv_bytes(&h, op->blob, op->blob_size);
    fnv_u32(&h, op->anim_size);
    fnv_bytes(&h, op->anim, op->anim_size);
  }
  for (uint32_t i = 0; i < kx->n_events; i++) {
    const wz3_kx_event *e = &kx->events[i];
    fnv_u32(&h, e->operation);
    fnv_u32(&h, (uint32_t)e->start);
    fnv_u32(&h, (uint32_t)e->end);
    fnv_u32(&h, f2u(e->velocity));
    fnv_u32(&h, f2u(e->modulation));
    fnv_u32(&h, e->select);
    for (int k = 0; k < 3; k++) fnv_u32(&h, f2u(e->scale[k]));
    for (int k = 0; k < 3; k++) fnv_u32(&h, f2u(e->rotate[k]));
    for (int k = 0; k < 3; k++) fnv_u32(&h, f2u(e->translate[k]));
    fnv_u32(&h, e->color);
    fnv_u32(&h, (uint32_t)e->spline);
    fnv_u32(&h, f2u(e->start_interval));
    fnv_u32(&h, f2u(e->end_interval));
    fnv_u32(&h, e->flags);
  }
  for (uint32_t i = 0; i < kx->n_splines; i++) {
    const wz3_kx_spline *sp = &kx->splines[i];
    fnv_u32(&h, sp->interpolation);
    fnv_u32(&h, sp->n_channels);
    for (uint32_t c = 0; c < sp->n_channels; c++) {
      fnv_u32(&h, sp->channels[c].n_keys);
      for (uint32_t k = 0; k < sp->channels[c].n_keys; k++) {
        fnv_u32(&h, sp->channels[c].key_time[k]);
        fnv_u32(&h, f2u(sp->channels[c].key_value[k]));
      }
    }
  }
  return h.h;
}

/* ---- fixture lines ----------------------------------------------------------------------------------------------- */

typedef struct lines {
  char *text, *cursor;
} lines;
static char *next_line(lines *l)
{
  if (!*l->cursor) return NULL;
  char *start = l->cursor;
  char *nl = strchr(start, '\n');
  if (nl) {
    *nl = 0;
    l->cursor = nl + 1;
  } else {
    l->cursor = start + strlen(start);
  }
  return start;
}

/* ---- 1. the document of theta ------------------------------------------------------------------------------------ */

#define THETA_OPS      1329u
#define THETA_CLASSES  67u
#define THETA_EVENTS   89u
#define THETA_SPLINES  61u
#define THETA_BLOB_OPS 7u
#define THETA_EYE_OP   759u
#define THETA_EYE_SIZE 61693u

static void test_theta_document(const wz3_kx *kx, size_t kx_size, const char *fixtures)
{
  /* what the independent Python reader found */
  size_t size;
  uint8_t *text = read_all(fixtures, "kx_fingerprint.txt", &size);
  char expect[40][160];
  int n = 0;
  if (text) {
    lines l = { (char *)text, (char *)text };
    char *line;
    while ((line = next_line(&l)) && n < 40) snprintf(expect[n++], sizeof expect[0], "%s", line);
    free(text);
  }
  char got[40][160];
  int g = 0;
#define LINE(...) snprintf(got[g++], sizeof got[0], __VA_ARGS__)
  LINE("flags %u", kx->flags);
  LINE("name %s", kx->name);
  LINE("song_size %u", kx->song_size);
  LINE("sample_size %u", kx->sample_size);
  LINE("song_bpm_fixed %u", kx->song_bpm_fixed);
  LINE("song_length %u", kx->song_length);
  LINE("n_classes %u", kx->n_classes);
  LINE("n_ops %u", kx->n_ops);
  LINE("n_events %u", kx->n_events);
  LINE("n_splines %u", kx->n_splines);
  LINE("size %u", (unsigned)kx_size);
  LINE("section_header %u", kx->sections.header);
  LINE("section_song %u", kx->sections.song);
  LINE("section_samples %u", kx->sections.samples);
  LINE("section_document %u", kx->sections.document);
  LINE("section_graph %u", kx->sections.graph);
  LINE("section_parameters %u", kx->sections.parameters);
  LINE("section_animations %u", kx->sections.animations);
  LINE("section_events %u", kx->sections.events);
  LINE("section_splines %u", kx->sections.splines);
  LINE("section_blobs %u", kx->sections.blobs);
  LINE("section_end %u", kx->sections.end);
  LINE("fingerprint %u", kx_fingerprint(kx));
#undef LINE
  CHECK(n == g, "fingerprint fixture has %d lines, expected %d", n, g);
  for (int i = 0; i < g && i < n; i++) CHECK(!strcmp(expect[i], got[i]), "kx: Python '%s' vs C '%s'", expect[i], got[i]);

  /* class histogram */
  text = read_all(fixtures, "class_histogram.txt", &size);
  CHECK(text != NULL, "class_histogram.txt");
  if (text) {
    lines l = { (char *)text, (char *)text };
    uint32_t ops[256] = { 0 };
    for (uint32_t i = 0; i < kx->n_ops; i++) ops[kx->ops[i].class_index]++;
    uint32_t c = 0;
    char *line;
    while ((line = next_line(&l))) {
      char want[400], have[400];
      snprintf(want, sizeof want, "%s", line);
      if (c < kx->n_classes)
        snprintf(have, sizeof have, "%u 0x%02x %u 0x%08x %s", c, kx->classes[c].id, ops[c], kx->classes[c].convention, kx->classes[c].packing);
      else
        have[0] = 0;
      CHECK(!strcmp(want, have), "class %u: Python '%s' vs C '%s'", c, want, have);
      c++;
    }
    CHECK(c == kx->n_classes, "class histogram has %u lines, document %u classes", c, kx->n_classes);
    free(text);
  }

  /* facts of the production (study section 10, plan gate G0) */
  CHECK(kx->flags == WZ3_KX_FLAG_LAYOUT_2004, "flags %u", kx->flags);
  CHECK(kx->n_ops == THETA_OPS && kx->n_classes == THETA_CLASSES && kx->n_events == THETA_EVENTS && kx->n_splines == THETA_SPLINES,
        "ops %u classes %u events %u splines %u", kx->n_ops, kx->n_classes, kx->n_events, kx->n_splines);
  CHECK(kx->roots[0] == THETA_OPS - 1 && kx->ops[kx->roots[0]].class_id == 0x0d, "root 0 is the Demo operator");
  CHECK(kx->song == NULL || kx->song_size == 0, "the Ogg is a separate file");
  CHECK(kx->song_bpm_fixed == (138u << 16) && kx->song_length == (609u << 16), "tempo 138 BPM, 609 beats");
  CHECK(kx->sections.end == kx_size, "the document spans the whole file");
  for (uint32_t c = 0; c < kx->n_classes; c++) CHECK(wz3_class_lookup(kx->classes[c].id) != NULL, "class 0x%02x has no class metadata", kx->classes[c].id);

  /* gate G0: the blob table. The first blob is eye.jpg and starts where the splines end. */
  uint32_t blob_ops = 0, first = 0xffffffffu;
  uint64_t blob_bytes = 0;
  for (uint32_t i = 0; i < kx->n_ops; i++) {
    const wz3_kx_op *op = &kx->ops[i];
    CHECK(!op->blob_size == !op->blob, "operator %u: blob size and pointer disagree", i);
    if (!op->blob_size) continue;
    if (first == 0xffffffffu) first = i;
    blob_ops++;
    blob_bytes += op->blob_size;
  }
  CHECK(blob_ops == THETA_BLOB_OPS && blob_bytes == 853293u, "blobs: %u operators, %llu bytes", blob_ops, (unsigned long long)blob_bytes);
  CHECK(first == THETA_EYE_OP, "first blob is operator %u", first);
  if (first == THETA_EYE_OP) {
    const wz3_kx_op *op = &kx->ops[first];
    CHECK(op->blob_size == THETA_EYE_SIZE && op->blob[0] == 0xff && op->blob[1] == 0xd8 && op->blob[2] == 0xff && op->blob[op->blob_size - 2] == 0xff &&
            op->blob[op->blob_size - 1] == 0xd9,
          "eye.jpg is a complete JPEG");
    CHECK(op->strings[0] && !strcmp(op->strings[0], "eye.jpg") && kx->sections.splines < kx->sections.blobs, "eye.jpg is named by its operator");
  }

  /* every event refers to an operator of the document, all inputs point backwards */
  for (uint32_t i = 0; i < kx->n_events; i++) CHECK(kx->events[i].operation < kx->n_ops && kx->events[i].flags == 0, "event %u", i);
  for (uint32_t i = 0; i < kx->n_ops; i++)
    for (uint32_t k = 0; k < kx->ops[i].n_inputs; k++) CHECK(kx->ops[i].inputs[k] < i, "operator %u input %u", i, k);
}

/* ---- 2. robustness ------------------------------------------------------------------------------------------------- */

static uint32_t g_rng = 0x9e3779b9u;
static uint32_t xorshift(void)
{
  g_rng ^= g_rng << 13;
  g_rng ^= g_rng >> 17;
  g_rng ^= g_rng << 5;
  return g_rng;
}

static void test_robustness(const uint8_t *kx_bytes, size_t kx_size)
{
  wz3_kx *kx = NULL;
  size_t at = 0;
  /* every prefix of the header and of the tail, and a spread through the middle, is rejected with an offset inside it */
  for (size_t n = 0; n < kx_size; n += (n < 4096 || n + 4096 > kx_size) ? 7 : kx_size / 300 + 1) {
    const wz3_result r = wz3_kx_parse(&kx, kx_bytes, n, &at);
    CHECK(r != WZ3_OK && kx == NULL && at <= n, "prefix %zu: %s at %zu", n, wz3_result_string(r), at);
  }
  CHECK(wz3_kx_parse(&kx, kx_bytes, kx_size - 1, &at) == WZ3_ERR_TRUNCATED, "one byte short");
  /* single-bit flips in the part that is not blob data: a clean verdict either way (ASan/UBSan catch the rest) */
  uint8_t *copy = (uint8_t *)malloc(kx_size);
  if (!copy) return;
  memcpy(copy, kx_bytes, kx_size);
  wz3_kx *probe = NULL;
  CHECK(wz3_kx_parse(&probe, kx_bytes, kx_size, &at) == WZ3_OK && probe, "reference parse");
  const size_t limit = probe ? probe->sections.blobs : 0;
  wz3_kx_destroy(probe);
  int accepted = 0, rejected = 0;
  for (int i = 0; i < 400 && limit; i++) {
    const size_t pos = xorshift() % limit;
    const uint8_t bit = (uint8_t)(1u << (xorshift() & 7u));
    copy[pos] ^= bit;
    const wz3_result r = wz3_kx_parse(&kx, copy, kx_size, &at);
    if (r == WZ3_OK) {
      accepted++;
      wz3_kx_destroy(kx);
    } else {
      rejected++;
      CHECK(kx == NULL, "failed parse leaves no document");
    }
    copy[pos] ^= bit;
  }
  CHECK(accepted + rejected == 400, "flips");
  free(copy);
}

int suite_wz3_theta_kx(int argc, char **argv)
{
  if (argc < 3) {
    printf("usage: wz3_theta_kx ASSETS_DIR FIXTURE_DIR\n");
    return 2;
  }
  printf("the theta document\n");
  size_t kx_size = 0;
  uint8_t *kx_bytes = read_all(argv[1], "theta.kx", &kx_size);
  if (!kx_bytes) return 2;
  wz3_kx *kx = NULL;
  size_t at = 0;
  const wz3_result r = wz3_kx_parse(&kx, kx_bytes, kx_size, &at);
  CHECK(r == WZ3_OK && kx, "theta.kx parses: %s at %zu", wz3_result_string(r), at);
  if (kx) {
    test_theta_document(kx, kx_size, argv[2]);
    wz3_kx_destroy(kx);
  }
  test_robustness(kx_bytes, kx_size);
  free(kx_bytes);
  printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
