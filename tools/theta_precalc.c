/* theta_precalc: runs the runtime's init pass over a KX document and reports, per operator class, what did not work.
 *
 *   theta_precalc [--expect-ok [--defer ID,ID,...]] DOCUMENT.kx
 *
 * The run does not stop at the first failing operator (the host's op_error callback); every failure is counted by class with
 * its first error. Mesh operators are checked through the host's mesh sink: every mesh is prepared for the renderer
 * (wz3_mesh_prepare), so a mesh that cannot be drawn is a failure here, and the vertex and face totals are reported. The exit
 * status is non-zero when an operator failed (with --expect-ok), when a mesh operator produced no mesh although its inputs
 * exist, or when the run itself failed. */
#define _POSIX_C_SOURCE 200809L
#define WZ3_IMPLEMENTATION
#include <theta/core/wz3.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wshadow"
#endif
#include "../third_party/stb/stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

typedef struct class_stat {
  uint32_t ops, failed, meshes, no_mesh, no_handler, bitmaps, bitmap_failed;
  wz3_result bitmap_error;
  wz3_result first_error;
  uint32_t first_op;
  double seconds;
} class_stat;

typedef struct probe {
  const wz3_kx *kx;
  class_stat stat[256];
  uint8_t *got_mesh;
  wz3_graph *graph;
  uint32_t failed;
  uint64_t vertices, faces, indices, bytes;
  uint32_t bad_geometry;
  uint32_t mesh_hash, meshes;
  double last;
} probe;

/* Bitmap_Import: the one picture of theta is a JPEG, decoded here by stb_image (the core library has no decoder) */
static int import_image(void *user, const uint8_t *blob, size_t size, const char *filename, int32_t *w, int32_t *h, uint8_t **rgba)
{
  (void)user;
  (void)filename;
  int channels;
  stbi_uc *pixels = stbi_load_from_memory(blob, (int)size, w, h, &channels, 4);
  *rgba = pixels;
  return pixels != NULL;
}

static double now_seconds(void) { return (double)clock() / (double)CLOCKS_PER_SEC; }

static void on_error(void *user, uint32_t op, wz3_result error)
{
  probe *p = (probe *)user;
  class_stat *s = &p->stat[p->kx->ops[op].class_id & 255];
  if (!s->failed) {
    s->first_error = error;
    s->first_op = op;
  }
  s->failed++;
  p->failed++;
}

static wz3_result on_mesh(void *user, uint32_t op, const wz3_mesh *mesh)
{
  probe *p = (probe *)user;
  wz3_geometry g;
  const wz3_result r = wz3_mesh_prepare(mesh, &g);
  if (r != WZ3_OK) {
    p->bad_geometry++;
    return WZ3_OK;
  }
  p->got_mesh[op] = 1;
  {
    uint32_t v, e, f, h;
    if (!wz3_mesh_summary(mesh, &v, &e, &f, &h)) {
      const uint32_t words[5] = { op, v, e, f, h };
      for (int i = 0; i < 5; i++)
        for (int b = 0; b < 4; b++) p->mesh_hash = (p->mesh_hash ^ ((words[i] >> (8 * b)) & 255u)) * 0x01000193u;
    }
  }
  if (getenv("THETA_MESH_BOUNDS")) {
    char tag[24];
    snprintf(tag, sizeof tag, ",%u,", op);
    char list[256];
    snprintf(list, sizeof list, ",%s,", getenv("THETA_MESH_BOUNDS"));
    if (strstr(list, tag))
      fprintf(stderr, "mesh %u: %u vertices, bounds %.3f %.3f %.3f .. %.3f %.3f %.3f\n", op, g.n_vertices, g.bounds_min[0], g.bounds_min[1], g.bounds_min[2],
              g.bounds_max[0], g.bounds_max[1], g.bounds_max[2]);
    if (strstr(list, tag) && getenv("THETA_MESH_UVRANGE")) {
      float lo[2] = { 1e9f, 1e9f }, hi[2] = { -1e9f, -1e9f };
      for (uint32_t k = 0; k < g.n_vertices; k++)
        for (int c = 0; c < 2; c++) {
          if (g.uv0[k * 2 + c] < lo[c]) lo[c] = g.uv0[k * 2 + c];
          if (g.uv0[k * 2 + c] > hi[c]) hi[c] = g.uv0[k * 2 + c];
        }
      fprintf(stderr, "  uv range u %.3f..%.3f v %.3f..%.3f\n", lo[0], hi[0], lo[1], hi[1]);
    }
    if (strstr(list, tag) && getenv("THETA_MESH_VERTS")) {
      for (uint32_t k = 0; k < g.n_vertices && k < 24; k++)
        fprintf(stderr, "  v%u pos %.3f %.3f %.3f uv %.3f %.3f\n", k, g.positions[k * 3], g.positions[k * 3 + 1], g.positions[k * 3 + 2], g.uv0[k * 2],
                g.uv0[k * 2 + 1]);
      for (uint32_t k = 0; k < g.n_groups; k++)
        fprintf(stderr, "  group %u slot %d material %d count %u\n", k, g.groups[k].slot, g.groups[k].material, g.groups[k].count);
    }
  }
  p->vertices += g.n_vertices;
  p->indices += g.n_indices;
  p->bytes += wz3_geometry_bytes(&g);
  uint32_t v, e, f, h;
  if (!wz3_mesh_summary(mesh, &v, &e, &f, &h)) p->faces += f;
  wz3_geometry_free(&g);
  return WZ3_OK;
}

static const wz3_bitmap *on_bitmap(void *user, uint32_t op)
{
  probe *p = (probe *)user;
  const wz3_bitmap *bitmap = NULL;
  if (wz3_graph_eval_bitmap(p->graph, op, &bitmap) != WZ3_OK) return NULL;
  return bitmap;
}

static void on_progress(void *user, uint32_t op, uint32_t done, uint32_t total)
{
  probe *p = (probe *)user;
  const double t = now_seconds();
  p->stat[p->kx->ops[op].class_id & 255].seconds += t - p->last;
  p->last = t;
  (void)done;
  (void)total;
}

int main(int argc, char **argv)
{
  const char *path = NULL;
  const char *defer = "";
  int expect_ok = 0;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--expect-ok"))
      expect_ok = 1;
    else if (!strcmp(argv[i], "--defer") && i + 1 < argc)
      defer = argv[++i];
    else if (argv[i][0] == '-' || path)
      path = NULL, i = argc;
    else
      path = argv[i];
  }
  if (!path) {
    fprintf(stderr, "usage: theta_precalc [--expect-ok [--defer ID,ID,...]] DOCUMENT.kx\n");
    return 2;
  }
  FILE *f = fopen(path, "rb");
  if (!f) return 2;
  fseek(f, 0, SEEK_END);
  const long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  unsigned char *bytes = (unsigned char *)malloc((size_t)n);
  if (!bytes || fread(bytes, 1, (size_t)n, f) != (size_t)n) return 2;
  fclose(f);
  wz3_kx *kx;
  size_t at;
  wz3_result res = wz3_kx_parse(&kx, bytes, (size_t)n, &at);
  free(bytes);
  if (res != WZ3_OK) {
    fprintf(stderr, "theta_precalc: %s at byte %zu\n", wz3_result_string(res), at);
    return 1;
  }
  static probe p;
  p.kx = kx;
  p.mesh_hash = 0x811c9dc5u;
  p.got_mesh = (uint8_t *)calloc(kx->n_ops, 1);
  wz3_graph_options graph_options;
  memset(&graph_options, 0, sizeof graph_options);
  wz3_bitmap_ctx_init(&graph_options.ctx);
  graph_options.ctx.import_image = import_image;
  p.graph = wz3_graph_create(kx, &graph_options, &res);
  wz3_rt_options o;
  memset(&o, 0, sizeof o);
  o.release_consumed = 1;
  o.frame_jobs = 1;
  o.geometry = WZ3_GEOMETRY_GENERATE;
  o.host.user = &p;
  o.host.mesh = on_mesh;
  o.host.bitmap = on_bitmap;
  o.host.progress = on_progress;
  o.host.op_error = on_error;
  wz3_rt *rt = wz3_rt_create(kx, &o, &res);
  if (!rt) {
    fprintf(stderr, "theta_precalc: runtime: %s\n", wz3_result_string(res));
    return 1;
  }
  p.last = now_seconds();
  const double t0 = p.last;
  res = wz3_rt_precalc(rt);
  const double total = now_seconds() - t0;

  uint32_t mesh_ops = 0, meshes = 0, bitmap_failed_total = 0, no_handler_total = 0, bitmaps_total = 0;
  for (uint32_t i = 0; i < kx->n_ops; i++) {
    class_stat *s = &p.stat[kx->ops[i].class_id & 255];
    const wz3_class_info *info = wz3_class_lookup(kx->ops[i].class_id);
    s->ops++;
    if (info && info->output_class == WZ3_KC_BITMAP) {
      const wz3_bitmap *bitmap = NULL;
      const wz3_result br = wz3_graph_eval_bitmap(p.graph, i, &bitmap);
      s->bitmaps++;
      bitmaps_total++;
      if (br != WZ3_OK) {
        if (!s->bitmap_failed) s->bitmap_error = br;
        s->bitmap_failed++;
        bitmap_failed_total++;
      }
      wz3_graph_release(p.graph, i);
    } else if (!wz3_rt_op_has_handler(rt, i)) {
      s->no_handler++;
      no_handler_total++;
    }
    if (info && info->output_class == WZ3_KC_MESH) {
      mesh_ops++;
      if (p.got_mesh[i])
        s->meshes++, meshes++;
      else
        s->no_mesh++;
    }
  }
  printf("precalc: %s, %u operators, %u failed, %.2f s\n", wz3_result_string(res), kx->n_ops, p.failed, total);
  printf("meshes: %u of %u mesh operators, %llu vertices, %llu indices, %.1f MiB prepared in total, %u not drawable\n", meshes, mesh_ops,
         (unsigned long long)p.vertices, (unsigned long long)p.indices, (double)p.bytes / 1048576.0, p.bad_geometry);
  printf("bitmaps: %u operators evaluated, %u failed; operators of other classes without a handler: %u\n", bitmaps_total, bitmap_failed_total,
         no_handler_total);
  printf("\n  id  name              ops  failed  no mesh  no handler  bitmap fail  seconds  first error\n");
  printf("mesh fingerprint 0x%08x\n", p.mesh_hash);
  uint8_t tolerated[256];
  memset(tolerated, 0, sizeof tolerated);
  tolerated[0x03] = tolerated[0x06] = tolerated[0x16] = tolerated[0x0d] = 1;
  for (const char *q = defer; *q;) {
    char *end;
    const unsigned long id = strtoul(q, &end, 16);
    if (end == q) break;
    if (id < 256) tolerated[id] = 1;
    q = *end == ',' ? end + 1 : end;
  }
  uint32_t no_mesh_total = 0, unexpected = 0;
  for (uint32_t c = 0; c < kx->n_classes; c++) {
    if (!tolerated[kx->classes[c].id & 255]) unexpected += p.stat[kx->classes[c].id & 255].no_handler + p.stat[kx->classes[c].id & 255].bitmap_failed;
  }
  for (uint32_t c = 0; c < kx->n_classes; c++) {
    const uint16_t id = kx->classes[c].id;
    const class_stat *s = &p.stat[id & 255];
    const wz3_class_info *info = wz3_class_lookup(id);
    if (!s->failed && !s->no_mesh && !s->no_handler && !s->bitmap_failed && s->seconds < 0.05) continue;
    printf("0x%02x  %-16s %5u  %6u  %7u  %10u  %11u  %7.2f  %s", id, info ? info->name : "?", s->ops, s->failed, s->no_mesh, s->no_handler, s->bitmap_failed,
           s->seconds,
           s->failed          ? wz3_result_string(s->first_error)
           : s->bitmap_failed ? wz3_result_string(s->bitmap_error)
                              : "");
    if (s->failed) printf(" (operator %u)", s->first_op);
    printf("\n");
    no_mesh_total += s->no_mesh;
  }
  if (getenv("THETA_BITMAP_DUMP")) {
    const wz3_bitmap *bitmap = NULL;
    if (wz3_graph_eval_bitmap(p.graph, (uint32_t)atoi(getenv("THETA_BITMAP_DUMP")), &bitmap) == WZ3_OK && bitmap) {
      FILE *out = fopen("bitmap_dump.ppm", "wb");
      if (out) {
        fprintf(out, "P6\n%d %d\n255\n", bitmap->width, bitmap->height);
        for (int32_t i = 0; i < bitmap->width * bitmap->height; i++)
          for (int c = 0; c < 3; c++) fputc(bitmap->data[i * 4 + c] >> 7, out);
        fclose(out);
      }
    }
  }
  wz3_rt_destroy(rt);
  wz3_graph_destroy(p.graph);
  wz3_kx_destroy(kx);
  free(p.got_mesh);
  if (res != WZ3_OK) return 1;
  if (expect_ok) printf("unexpected: %u operators without a handler or bitmap, %u failed, %u without a mesh\n", unexpected, p.failed, no_mesh_total);
  return (expect_ok && (p.failed || no_mesh_total || p.bad_geometry || unexpected)) ? 1 : 0;
}
