/* theta_inventory: the operator classes of a KX document.
 *
 *   theta_inventory [--expect-ops N] [--expect-classes N] DOCUMENT.kx
 *
 * Prints the header facts (layout, tempo, length, sections) and one row per class of the document's own class table: its
 * id, the name the class metadata knows it by, the output class, the number of operators, the calling convention and the
 * packing string. The convention and the packing come from the document, not from a table in the player.
 *
 * The exit status is non-zero when a class has no entry in the class metadata, or when --expect-ops / --expect-classes do
 * not match. */
#define _POSIX_C_SOURCE 200809L
#define WZ3_IMPLEMENTATION
#include <theta/core/wz3.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *output_name(int output_class)
{
  switch (output_class) {
  case WZ3_KC_BITMAP: return "bitmap";
  case WZ3_KC_MINMESH: return "minmesh";
  case WZ3_KC_SCENE: return "scene";
  case WZ3_KC_MATERIAL: return "material";
  case WZ3_KC_MESH: return "mesh";
  case WZ3_KC_IPP: return "ipp";
  case WZ3_KC_EFFECT: return "effect";
  case WZ3_KC_DEMO: return "demo";
  case WZ3_KC_SPLINE: return "spline";
  case WZ3_KC_ANY: return "any";
  default: return "?";
  }
}

static int read_file(const char *path, unsigned char **data, size_t *size)
{
  FILE *f = fopen(path, "rb");
  if (!f) return 0;
  long n;
  if (fseek(f, 0, SEEK_END) || (n = ftell(f)) < 0 || fseek(f, 0, SEEK_SET)) {
    fclose(f);
    return 0;
  }
  unsigned char *d = (unsigned char *)malloc(n ? (size_t)n : 1);
  if (!d || (n && fread(d, 1, (size_t)n, f) != (size_t)n)) {
    free(d);
    fclose(f);
    return 0;
  }
  fclose(f);
  *data = d;
  *size = (size_t)n;
  return 1;
}

static wz3_kx *load(const char *path)
{
  unsigned char *bytes;
  size_t size, at = 0;
  if (!read_file(path, &bytes, &size)) {
    fprintf(stderr, "theta_inventory: cannot read %s\n", path);
    return NULL;
  }
  wz3_kx *kx;
  const wz3_result res = wz3_kx_parse(&kx, bytes, size, &at);
  free(bytes);
  if (res != WZ3_OK) {
    fprintf(stderr, "theta_inventory: %s: %s at byte %zu\n", path, wz3_result_string(res), at);
    return NULL;
  }
  return kx;
}

int main(int argc, char **argv)
{
  const char *path = NULL;
  long expect_ops = -1, expect_classes = -1;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--expect-ops") && i + 1 < argc)
      expect_ops = atol(argv[++i]);
    else if (!strcmp(argv[i], "--expect-classes") && i + 1 < argc)
      expect_classes = atol(argv[++i]);
    else if (argv[i][0] == '-' || path) {
      fprintf(stderr, "usage: theta_inventory [--expect-ops N] [--expect-classes N] DOCUMENT.kx\n");
      return 2;
    } else
      path = argv[i];
  }
  if (!path) {
    fprintf(stderr, "usage: theta_inventory [--expect-ops N] [--expect-classes N] DOCUMENT.kx\n");
    return 2;
  }
  wz3_kx *kx = load(path);
  if (!kx) return 1;

  uint32_t *ops = (uint32_t *)calloc(kx->n_classes ? kx->n_classes : 1, sizeof(uint32_t));
  if (!ops) return 1;
  for (uint32_t i = 0; i < kx->n_ops; i++) ops[kx->ops[i].class_index]++;

  const double bpm = (double)kx->song_bpm_fixed / 65536.0;
  const double beats = (double)kx->song_length / 65536.0;
  uint64_t blob_bytes = 0;
  uint32_t blobs = 0;
  for (uint32_t i = 0; i < kx->n_ops; i++)
    if (kx->ops[i].blob_size) {
      blobs++;
      blob_bytes += kx->ops[i].blob_size;
    }
  printf("document: %s, %u operators, %u classes, %u events, %u splines\n", (kx->flags & WZ3_KX_FLAG_LAYOUT_2004) ? "2004 layout" : "2007 layout", kx->n_ops,
         kx->n_classes, kx->n_events, kx->n_splines);
  printf("timing: %.3f BPM, %.3f beats%s, embedded song %u bytes\n", bpm, beats, bpm > 0.0 ? "" : " (none)", kx->song_size);
  if (bpm > 0.0) printf("        %.2f s at that tempo\n", beats * 60.0 / bpm);
  printf("blobs: %u operators, %llu bytes; document %u bytes\n", blobs, (unsigned long long)blob_bytes, kx->sections.end);

  int unknown = 0;
  printf("\n idx    id  name              output  ops  convention  packing\n");
  for (uint32_t c = 0; c < kx->n_classes; c++) {
    const wz3_kx_class *cls = &kx->classes[c];
    const wz3_class_info *info = wz3_class_lookup(cls->id);
    if (!info) unknown++;
    printf("%4u  0x%02x  %-16s  %-7s %4u  0x%08x  %s\n", c, cls->id, info ? info->name : "?", info ? output_name(info->output_class) : "?", ops[c],
           cls->convention, cls->packing);
  }
  printf("\nclasses without class metadata: %d\n", unknown);

  int status = unknown ? 1 : 0;
  if (expect_ops >= 0 && (long)kx->n_ops != expect_ops) {
    fprintf(stderr, "theta_inventory: %u operators, expected %ld\n", kx->n_ops, expect_ops);
    status = 1;
  }
  if (expect_classes >= 0 && (long)kx->n_classes != expect_classes) {
    fprintf(stderr, "theta_inventory: %u classes, expected %ld\n", kx->n_classes, expect_classes);
    status = 1;
  }
  free(ops);
  wz3_kx_destroy(kx);
  return status;
}
