/* Tests for the mesh editing operators of fr-038: theta.
 *
 *   suite_wz3_theta_ops ASSETS_DIR
 *
 * There is no reference implementation and no reference binary for these operators (the 2007 sources need MSVC-only code), so the checks are the
 * ones that decide whether the half-edge surgery is right: the topology stays consistent (every face and vertex cycle closes, the
 * half-edges pair up, Euler's formula holds on closed meshes), counts that follow from the operator's definition (an extruded quad adds
 * four vertices, four faces, eight edges), positions and attributes computed by hand, determinism, and the packing contract: the
 * document's class table has the parameter layout each handler reads. */
#include <math.h>
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

#define PI 3.14159265358979323846
static const double IDENTITY_SRT[9] = { 1, 1, 1, 0, 0, 0, 0, 0, 0 };

/* ---- mesh inspection ---------------------------------------------------------------------------------------------- */

typedef struct stats {
  uint32_t verts, faces, edges; /* positions (vertices that are their own First), live faces, edges */
  int consistent;
} stats;

/* GenMesh::Verify, as a function that says why it failed */
static const char *verify(const wz3_mesh *m, stats *out)
{
  const int32_t he = (int32_t)m->n_edges * 2;
  memset(out, 0, sizeof *out);
  for (uint32_t v = 0; v < m->n_verts; v++) {
    const int32_t first = m->verts[v].first, next = m->verts[v].next;
    if (first < 0 || (uint32_t)first >= m->n_verts || m->verts[first].first != first) return "vertex First is not a fixed point";
    if (next < 0 || (uint32_t)next >= m->n_verts) return "vertex Next out of range";
    if (first == (int32_t)v) out->verts++;
  }
  for (uint32_t e = 0; e < m->n_edges; e++) {
    const wz3__medge *edge = &m->edges[e];
    for (int s = 0; s < 2; s++) {
      if (edge->next[s] < 0 || edge->next[s] >= he || edge->prev[s] < 0 || edge->prev[s] >= he) return "edge Next/Prev out of range";
      if (edge->vert[s] < 0 || (uint32_t)edge->vert[s] >= m->n_verts) return "edge vertex out of range";
      if (edge->face[s] < 0 || (uint32_t)edge->face[s] >= m->n_faces) return "edge face out of range";
    }
  }
  for (int32_t h = 0; h < he; h++) {
    if (wz3__mh_prev_face(m, wz3__mh_next_face(m, h)) != h) return "Prev(Next(h)) != h";
    if (wz3__mh_face(m, wz3__mh_next_face(m, h)) != wz3__mh_face(m, h)) return "Next leaves the face";
    const wz3__mvert *a = &m->verts[wz3__mh_vert(m, h ^ 1)];
    const wz3__mvert *b = &m->verts[wz3__mh_vert(m, wz3__mh_next_face(m, h))];
    if (a->first != b->first) return "the end of a half-edge is not the start of the next one";
  }
  for (uint32_t f = 0; f < m->n_faces; f++) {
    if (!m->faces[f].material) continue;
    int32_t h = m->faces[f].edge;
    if (h < 0 || h >= he) return "face edge out of range";
    int32_t n = 0;
    const int32_t start = h;
    do {
      if (wz3__mh_face(m, h) != (int32_t)f) return "face cycle visits another face";
      h = wz3__mh_next_face(m, h);
      if (++n > he) return "face cycle does not close";
    } while (h != start);
    out->faces++;
  }
  out->edges = m->n_edges;
  out->consistent = 1;
  return NULL;
}

static int check_closed(const wz3_mesh *m, const char *what, uint32_t verts, uint32_t edges, uint32_t faces)
{
  stats s;
  const char *why = verify(m, &s);
  CHECK(!why, "%s: %s", what, why ? why : "");
  if (why) return 0;
  CHECK(s.verts == verts && s.edges == edges && s.faces == faces, "%s: %u vertices, %u edges, %u faces; expected %u, %u, %u", what, s.verts, s.edges, s.faces,
        verts, edges, faces);
  CHECK((int32_t)s.verts - (int32_t)s.edges + (int32_t)s.faces == 2, "%s: Euler characteristic %d", what, (int)s.verts - (int)s.edges + (int)s.faces);
  return 1;
}

static void bounds(const wz3_mesh *m, float min[3], float max[3])
{
  min[0] = min[1] = min[2] = 1e30f;
  max[0] = max[1] = max[2] = -1e30f;
  for (uint32_t i = 0; i < m->n_verts; i++) {
    if (m->verts[i].first != (int32_t)i) continue;
    for (int a = 0; a < 3; a++) {
      if (m->verts[i].v[0][a] < min[a]) min[a] = m->verts[i].v[0][a];
      if (m->verts[i].v[0][a] > max[a]) max[a] = m->verts[i].v[0][a];
    }
  }
}

static int near(double a, double b, double tolerance) { return fabs(a - b) <= tolerance; }

/* ---- operators ---------------------------------------------------------------------------------------------------- */

static wz3_mesh *cube(int32_t flags) { return wz3__mesh_cube(1, 1, 1, flags, IDENTITY_SRT); }

static void test_extrude(void)
{
  wz3_mesh *c = cube(1); /* flat normals: the corners of the -X face have the normal (-1, 0, 0) */
  CHECK(c != NULL, "cube");
  if (!c) return;
  check_closed(c, "cube", 8, 12, 6);
  const double dist[3] = { 0.5, 0.5, 0.5 }, scale[3] = { 1, 1, 1 }, rotate[3] = { 0, 0, 0 };
  wz3_mesh *e = wz3__mesh_op_extrude(c, 1, 0, 0, 1, dist, scale, rotate); /* smask 1: the faces with mask bit 0 */
  CHECK(e != NULL, "extrude");
  if (e) {
    check_closed(e, "extruded once", 12, 20, 10);
    float min[3], max[3];
    bounds(e, min, max);
    CHECK(near(min[0], -1.0, 1e-5) && near(max[0], 0.5, 1e-5) && near(min[1], -0.5, 1e-5) && near(max[1], 0.5, 1e-5), "extent after one step: x %g..%g", min[0],
          max[0]);
    wz3__mesh_free(e);
  }
  /* two steps of half the distance each: the same extent, four more vertices, four more faces, eight more edges each step */
  const double dist2[3] = { 2, 2, 2 };
  e = wz3__mesh_op_extrude(c, 1, 0, 0, 2, dist2, scale, rotate);
  CHECK(e != NULL, "extrude twice");
  if (e) {
    check_closed(e, "extruded twice", 16, 28, 14);
    float min[3], max[3];
    bounds(e, min, max);
    CHECK(near(min[0], -2.5, 1e-5), "extent after two steps: %g", min[0]);
    wz3__mesh_free(e);
  }
  /* scale 0.5 per extrusion with a fixed direction (mode 4): the new ring is smaller */
  const double dir[3] = { 0, 0, -1 }, shrink[3] = { 0.25, 0.25, 0.25 };
  e = wz3__mesh_op_extrude(c, 1, 0, 4, 1, dir, shrink, rotate);
  CHECK(e != NULL, "extrude with scale");
  if (e) {
    check_closed(e, "extruded with scale", 12, 20, 10);
    wz3__mesh_free(e);
  }
  /* mode 8: scaled around the centre of the group, so the ring keeps its centre */
  const double none[3] = { 0, 0, 0 }, half[3] = { 1, 0.5, 0.5 };
  e = wz3__mesh_op_extrude(c, 1, 0, 8, 1, none, half, rotate);
  CHECK(e != NULL, "extrude around the centre");
  if (e) {
    check_closed(e, "extruded around the centre", 12, 20, 10);
    float min[3], max[3];
    bounds(e, min, max);
    CHECK(near(min[0], -0.5, 1e-5) && near(min[1], -0.5, 1e-5), "the box is kept: %g %g", min[0], min[1]);
    wz3__mesh_free(e);
  }
  wz3__mesh_free(c);
}

static void test_triangulate(void)
{
  wz3_mesh *c = cube(0);
  if (!c) return;
  wz3_mesh *t = wz3__mesh_op_triangulate(c, 0x3f, 4, 1); /* fan: every quad becomes two triangles */
  CHECK(t != NULL, "triangulate fan");
  if (t) {
    check_closed(t, "fan", 8, 18, 12);
    wz3__mesh_free(t);
  }
  t = wz3__mesh_op_triangulate(c, 0x3f, 4, 0); /* a vertex in the middle of every quad: four triangles each */
  CHECK(t != NULL, "triangulate centre");
  if (t) {
    check_closed(t, "centre", 14, 36, 24);
    int found = 0;
    for (uint32_t i = 0; i < t->n_verts; i++) {
      const float *p = t->verts[i].v[0];
      if (t->verts[i].first == (int32_t)i && near(p[0], -0.5, 1e-5) && near(p[1], 0, 1e-5) && near(p[2], 0, 1e-5)) found++;
    }
    CHECK(found == 1, "the centre of the -X face exists once (%d)", found);
    wz3__mesh_free(t);
  }
  t = wz3__mesh_op_triangulate(c, 0x3f, 5, 1); /* a threshold above the corner count changes nothing */
  CHECK(t != NULL, "triangulate threshold");
  if (t) {
    check_closed(t, "threshold", 8, 12, 6);
    wz3__mesh_free(t);
  }
  wz3__mesh_free(c);
}

static void test_smooth_angle(void)
{
  wz3_mesh *c = cube(0); /* creased along every edge for the UVs only, so its normals are smooth */
  if (!c) return;
  int creased = 0;
  for (uint32_t i = 0; i < c->n_edges; i++) creased += (c->edges[i].crease & 2) != 0;
  CHECK(creased == 0, "the cube starts without a normal crease (%d)", creased);
  wz3_mesh *s = wz3__mesh_op_smooth_angle(c, 0.5); /* every edge is a 90 degree edge, harder than the limit */
  CHECK(s != NULL, "smooth angle");
  if (s) {
    creased = 0;
    for (uint32_t i = 0; i < s->n_edges; i++) creased += (s->edges[i].crease & 6) == 6;
    CHECK(creased == 12, "all twelve edges are creased for normals and tangents (%d)", creased);
    CHECK(!s->got_normals, "the normals are stale");
    wz3__mesh_need_normals(s);
    int flat = 1;
    for (uint32_t i = 0; i < s->n_verts; i++) {
      const float *n = s->verts[i].v[1];
      const int axes = (fabs(n[0]) > 0.99f) + (fabs(n[1]) > 0.99f) + (fabs(n[2]) > 0.99f);
      if (axes != 1) flat = 0;
    }
    CHECK(flat, "creased: every normal is a face normal");
    check_closed(s, "creased cube", 8, 12, 6);
    wz3__mesh_free(s);
  }
  s = wz3__mesh_op_smooth_angle(c, 1.0); /* the limit is 90 degrees: nothing is harder */
  CHECK(s != NULL, "smooth angle 1");
  if (s) {
    creased = 0;
    for (uint32_t i = 0; i < s->n_edges; i++) creased += (s->edges[i].crease & 6) != 0;
    CHECK(creased == 0, "no edge is creased at the full angle (%d)", creased);
    wz3__mesh_need_normals(s);
    int diagonal = 1;
    for (uint32_t i = 0; i < s->n_verts; i++) {
      const float *n = s->verts[i].v[1];
      if (!near(fabs(n[0]), 0.57735, 1e-3) || !near(fabs(n[1]), 0.57735, 1e-3) || !near(fabs(n[2]), 0.57735, 1e-3)) diagonal = 0;
    }
    CHECK(diagonal, "smooth: every normal points at its corner");
    wz3__mesh_free(s);
  }
  wz3__mesh_free(c);
}

static void test_selection(void)
{
  wz3_mesh *c = cube(0);
  if (!c) return;
  wz3_mesh *s = wz3__mesh_op_select_face(c, 0x80, 0, 3);
  CHECK(s != NULL, "select face");
  if (s) {
    int hits = 0, which = -1;
    for (uint32_t i = 0; i < s->n_faces; i++)
      if (s->faces[i].mask & 0x80) {
        hits++;
        which = (int)i;
      }
    CHECK(hits == 1 && which == 3, "face 3 alone has the new mask bit (%d hits, face %d)", hits, which);
    wz3__mesh_free(s);
  }
  s = wz3__mesh_op_select_face(c, 0x80, 0, 99);
  CHECK(s != NULL, "select face out of range");
  if (s) {
    int hits = 0;
    for (uint32_t i = 0; i < s->n_faces; i++) hits += (s->faces[i].mask & 0x80) != 0;
    CHECK(hits == 0, "a face beyond the mesh selects nothing");
    wz3__mesh_free(s);
  }
  s = wz3__mesh_op_select_face(c, 0x80, 0, -1);
  CHECK(s != NULL, "select face negative");
  wz3__mesh_free(s);

  /* Select Random: everything at the largest ratio, deterministic, and a smaller share at a smaller ratio */
  uint32_t shares[3] = { 0, 0, 0 };
  wz3_mesh *sphere = wz3__mesh_sphere(32, 16);
  CHECK(sphere != NULL, "sphere");
  if (sphere) {
    for (int k = 0; k < 3; k++) {
      const uint32_t ratio = k == 0 ? 255 : k == 1 ? 128 : 32;
      wz3_mesh *r = wz3__mesh_op_select_random(sphere, 0x010101, 0, ratio, 7);
      CHECK(r != NULL, "select random");
      if (!r) continue;
      for (uint32_t i = 0; i < r->n_faces; i++) shares[k] += (r->faces[i].mask & 1) != 0;
      if (k == 0) {
        uint32_t all = 0, live = 0;
        for (uint32_t i = 0; i < r->n_faces; i++) {
          live += r->faces[i].material != 0;
          all += (r->faces[i].mask & 1) != 0;
        }
        CHECK(all == live, "ratio 255 selects every face (%u of %u)", all, live);
      }
      wz3__mesh_free(r);
    }
    CHECK(shares[1] < shares[0] && shares[2] < shares[1] && shares[2] > 0, "the share falls with the ratio: %u > %u > %u", shares[0], shares[1], shares[2]);
    CHECK(near((double)shares[1] / (double)shares[0], 0.5, 0.12), "ratio 128 selects about half (%u of %u)", shares[1], shares[0]);
    wz3_mesh *a = wz3__mesh_op_select_random(sphere, 0x010101, 0, 100, 3), *b = wz3__mesh_op_select_random(sphere, 0x010101, 0, 100, 3);
    wz3_mesh *d = wz3__mesh_op_select_random(sphere, 0x010101, 0, 100, 4);
    CHECK(a && b && d, "select random twice");
    if (a && b && d) {
      int same = 1, other = 0;
      for (uint32_t i = 0; i < a->n_faces; i++) {
        if (a->faces[i].mask != b->faces[i].mask) same = 0;
        if (a->faces[i].mask != d->faces[i].mask) other = 1;
      }
      CHECK(same, "the same seed selects the same faces");
      CHECK(other, "another seed selects other faces");
    }
    wz3__mesh_free(a);
    wz3__mesh_free(b);
    wz3__mesh_free(d);

    /* Select Sphere: all vertices for a ball around the mesh, none for a speck, some for an offset ball */
    const double origin[3] = { 0, 0, 0 }, big[3] = { 4, 4, 4 }, tiny[3] = { 0.1, 0.1, 0.1 }, offset[3] = { 0.5, 0, 0 }, one[3] = { 1, 1, 1 };
    wz3_mesh *ball = wz3__mesh_op_select_sphere(sphere, 0x010000, 0, origin, big);
    wz3_mesh *speck = wz3__mesh_op_select_sphere(sphere, 0x010000, 0, origin, tiny);
    wz3_mesh *part = wz3__mesh_op_select_sphere(sphere, 0x010000, 0, offset, one);
    CHECK(ball && speck && part, "select sphere");
    if (ball && speck && part) {
      uint32_t n_ball = 0, n_speck = 0, n_part = 0;
      for (uint32_t i = 0; i < ball->n_verts; i++) {
        n_ball += (ball->verts[i].mask & 1) != 0;
        n_speck += (speck->verts[i].mask & 1) != 0;
        n_part += (part->verts[i].mask & 1) != 0;
      }
      CHECK(n_ball == ball->n_verts && n_speck == 0 && n_part > 0 && n_part < ball->n_verts, "ball %u, speck %u, offset ball %u of %u", n_ball, n_speck, n_part,
            ball->n_verts);
    }
    wz3__mesh_free(ball);
    wz3__mesh_free(speck);
    wz3__mesh_free(part);
    wz3__mesh_free(sphere);
  }
  wz3__mesh_free(c);
}

static void test_color(void)
{
  wz3_mesh *c = cube(1);
  if (!c) return;
  const double pos[3] = { 0, 0, 0 }, dir[2] = { 0, 0 };
  wz3_mesh *lit = wz3__mesh_op_color(c, pos, dir, 0x00ffffff, 1.0, 1.0, 0); /* directional, light from +Y */
  CHECK(lit != NULL, "colour");
  if (lit) {
    int up = 0, others = 0;
    for (uint32_t i = 0; i < lit->n_verts; i++) {
      const float *n = lit->verts[i].v[1], *col = lit->verts[i].v[3], *before = c->verts[i].v[3];
      const double delta = (double)col[0] - before[0];
      if (n[1] > 0.99f) {
        up += near(delta, 1.0, 1e-5);
      } else
        others += fabs(delta) > 1e-6;
    }
    CHECK(up == 4 && others == 0, "four vertices of the +Y face are lit, nothing else (%d, %d)", up, others);
    wz3__mesh_free(lit);
  }
  lit = wz3__mesh_op_color(c, pos, dir, 0x00804020, 2.0, 1.0, 3); /* ambient: every vertex gets the colour times the amplification */
  CHECK(lit != NULL, "colour ambient");
  if (lit) {
    int ok = 1;
    for (uint32_t i = 0; i < lit->n_verts; i++) {
      const float *col = lit->verts[i].v[3], *before = c->verts[i].v[3];
      if (!near((double)col[0] - before[0], 128.0 / 255.0 * 2.0, 1e-5) || !near((double)col[2] - before[2], 32.0 / 255.0 * 2.0, 1e-5)) ok = 0;
    }
    CHECK(ok, "ambient light adds the same colour everywhere");
    wz3__mesh_free(lit);
  }
  const double lamp[3] = { 0, 3, 0 };
  lit = wz3__mesh_op_color(c, lamp, dir, 0x00ffffff, 1.0, 10.0, 1); /* a point light above: the top is lit more than the bottom */
  CHECK(lit != NULL, "colour point");
  if (lit) {
    double top = 0, bottom = 0;
    for (uint32_t i = 0; i < lit->n_verts; i++) {
      const double delta = (double)lit->verts[i].v[3][0] - c->verts[i].v[3][0];
      if (lit->verts[i].v[0][1] > 0.4f)
        top += delta;
      else
        bottom += delta;
    }
    CHECK(top > 0.1 && bottom < 1e-6, "the point light reaches the top (%g) and not the bottom (%g)", top, bottom);
    wz3__mesh_free(lit);
  }
  wz3__mesh_free(c);
}

static void test_perlin_displace(void)
{
  wz3_mesh *s = wz3__mesh_sphere(16, 8);
  if (!s) return;
  const double none[3] = { 0, 0, 0 }, amp[3] = { 0.2, 0.2, 0.2 }, srt[9] = { 4, 4, 4, 0.1, 0.2, 0.3, 0.3, 0.7, 0.1 };
  wz3__mesh_all_to_selection(s, 1, WZ3__ALL_VERT);
  wz3__mesh_selection_to_mask(s, 0x010000, 0);
  wz3_mesh *same = wz3__mesh_op_perlin(s, 1, srt, none);
  wz3_mesh *moved = wz3__mesh_op_perlin(s, 1, srt, amp);
  wz3_mesh *again = wz3__mesh_op_perlin(s, 1, srt, amp);
  CHECK(same && moved && again, "perlin displace");
  if (same && moved && again) {
    int unchanged = 1, changed = 0, repeat = 1;
    double largest = 0;
    for (uint32_t i = 0; i < s->n_verts; i++)
      for (int a = 0; a < 3; a++) {
        if (same->verts[i].v[0][a] != s->verts[i].v[0][a]) unchanged = 0;
        const double d = (double)moved->verts[i].v[0][a] - s->verts[i].v[0][a];
        if (d != 0.0) changed = 1;
        if (fabs(d) > largest) largest = fabs(d);
        if (moved->verts[i].v[0][a] != again->verts[i].v[0][a]) repeat = 0;
      }
    CHECK(unchanged, "an amplitude of zero moves nothing");
    CHECK(changed && largest <= 0.2 + 1e-6, "the noise moves vertices by at most the amplitude (%g)", largest);
    CHECK(repeat, "the noise is deterministic");
  }
  /* the mask: no vertex carries the mask bit 2, so nothing moves */
  wz3_mesh *masked = wz3__mesh_op_perlin(s, 2, srt, amp);
  CHECK(masked != NULL, "perlin displace masked");
  if (masked) {
    int unchanged = 1;
    for (uint32_t i = 0; i < s->n_verts; i++)
      if (masked->verts[i].v[0][0] != s->verts[i].v[0][0]) unchanged = 0;
    CHECK(unchanged, "an unselected vertex is not displaced");
  }
  wz3__mesh_free(masked);
  wz3__mesh_free(same);
  wz3__mesh_free(moved);
  wz3__mesh_free(again);
  wz3__mesh_free(s);
}

static void test_displace(void)
{
  wz3_mesh *s = wz3__mesh_sphere(16, 8);
  wz3_bitmap *flat = wz3_bitmap_create(4, 4), *bump = wz3_bitmap_create(4, 4);
  CHECK(s && flat && bump, "displace setup");
  if (s && flat && bump) {
    for (int i = 0; i < 4 * 4 * 4; i++) {
      flat->data[i] = 0x4000;
      bump->data[i] = 0x7fff;
    }
    wz3__mesh_all_to_selection(s, 1, WZ3__ALL_VERT);
    wz3__mesh_selection_to_mask(s, 0x010000, 0);
    const double amp[3] = { 1, 1, 1 };
    wz3_mesh *a = wz3__mesh_op_displace(s, flat, 1, amp), *b = wz3__mesh_op_displace(s, bump, 1, amp);
    CHECK(a && b, "displace");
    if (a && b) {
      int unchanged = 1, radial = 1;
      for (uint32_t i = 0; i < s->n_verts; i++) {
        for (int k = 0; k < 3; k++)
          if (a->verts[i].v[0][k] != s->verts[i].v[0][k]) unchanged = 0;
        const float *p = b->verts[i].v[0];
        const double r = sqrt((double)p[0] * p[0] + (double)p[1] * p[1] + (double)p[2] * p[2]);
        if (!near(r, 0.5 + 16383.0 / 32767.0, 2e-3)) radial = 0;
      }
      CHECK(unchanged, "the middle grey (0x4000) displaces nothing");
      CHECK(radial, "white displaces every vertex outwards by about half the amplitude");
      stats st;
      const char *why = verify(b, &st);
      CHECK(!why, "displaced sphere: %s", why ? why : "");
    }
    wz3__mesh_free(a);
    wz3__mesh_free(b);
    CHECK(wz3__mesh_op_displace(s, NULL, 1, amp) == NULL, "no bitmap, no displacement");
  }
  wz3_bitmap_destroy(flat);
  wz3_bitmap_destroy(bump);
  wz3__mesh_free(s);
}

static void test_uv_round(void)
{
  wz3_mesh *s = wz3__mesh_sphere(12, 6);
  if (!s) return;
  for (int type = 1; type <= 2; type++) {
    wz3_mesh *r = wz3__mesh_op_uv_round(s, IDENTITY_SRT, type);
    CHECK(r != NULL, "uv projection %d", type);
    if (!r) continue;
    int ok = 1;
    for (uint32_t i = 0; i < r->n_verts; i++) {
      const float *p = r->verts[i].v[0], *uv = r->verts[i].v[4];
      const double x = p[0], y = p[1], z = p[2];
      const double rxz = sqrt(x * x + z * z), rad = sqrt(rxz * rxz + y * y);
      /* the angles come back through sine and cosine: u = 0.5 - angle / 2pi, v = 0.5 - latitude / pi */
      const double a = 2.0 * PI * (0.5 - (double)uv[0]);
      if (!near(rxz * wz3__sin(a), x, 1e-4) || !near(rxz * wz3__cos(a), z, 1e-4)) ok = 0;
      if (type == 1) {
        if (!near(uv[1], -y, 1e-5)) ok = 0;
      } else {
        const double lat = PI * (0.5 - (double)uv[1]);
        if (!near(rad * wz3__sin(lat), y, 1e-4) || !near(rad * wz3__cos(lat), rxz, 1e-4)) ok = 0;
      }
    }
    CHECK(ok, "type %d: u from the angle around Y, v from the %s", type, type == 1 ? "height" : "latitude");
    wz3__mesh_free(r);
  }
  wz3__mesh_free(s);
}

static void test_dots(void)
{
  wz3_bitmap *black = wz3_bitmap_create(8, 8);
  CHECK(black != NULL, "bitmap");
  if (!black) return;
  memset(black->data, 0, (size_t)8 * 8 * 4 * sizeof(uint16_t));
  wz3_bitmap *a = wz3_bitmap_dots(black, 0xffffffffu, 0xffffffffu, 4096, 5); /* count * size / 4096 = 64 dots */
  wz3_bitmap *b = wz3_bitmap_dots(black, 0xffffffffu, 0xffffffffu, 4096, 5);
  wz3_bitmap *c = wz3_bitmap_dots(black, 0xffffffffu, 0xffffffffu, 0, 5);    /* the do-while sets one dot at least */
  CHECK(a && b && c, "dots");
  if (a && b && c) {
    uint32_t n_a = 0, n_c = 0;
    for (int i = 0; i < 64; i++) {
      n_a += a->data[i * 4] != 0;
      n_c += c->data[i * 4] != 0;
    }
    CHECK(!memcmp(a->data, b->data, 64 * 4 * sizeof(uint16_t)), "the same seed sets the same dots");
    CHECK(n_a > 30 && n_a <= 64, "64 dots set between 30 and 64 distinct pixels (%u)", n_a);
    CHECK(n_c == 1, "a count of zero still sets one dot (%u)", n_c);
    int untouched = 1;
    for (int i = 0; i < 64 * 4; i++)
      if (black->data[i]) untouched = 0;
    CHECK(untouched, "the input bitmap is left as it was");
  }
  wz3_bitmap_destroy(a);
  wz3_bitmap_destroy(b);
  wz3_bitmap_destroy(c);
  wz3_bitmap_destroy(black);
}

/* ---- the packing contract ----------------------------------------------------------------------------------------- */

typedef struct contract {
  uint16_t id;
  const char *packing;
} contract;

/* The parameter layout each handler of the 2004 document reads (case does not matter: capitals only mark animatable values) */
static const contract g_contracts[] = {
  { 0x8c, "bbb" },           /* Triangulate: mask, threshold, type */
  { 0x8f, "bfff" },          /* Displace: mask, amplitude (a second input holds the height map) */
  { 0x91, "bfffeeeffffff" }, /* Perlin Displace: mask, scale, rotate, translate, amplitude */
  { 0x94, "mbbb" },          /* Select Random: destination mask, mode, ratio, seed */
  { 0x9a, "bsbbffffffeee" }, /* Extrude: source mask, destination mask, mode, count, distance, scale, rotate */
  { 0xa8, "mbffffff" },      /* Select Sphere: destination mask, mode, centre, size */
  { 0xa9, "bbi" },           /* Select Face: destination mask, mode, face */
  { 0xac, "f" },             /* SmAngle: angle */
  { 0xad, "fffeecffb" },     /* Color: position, direction, colour, amplification, range, operation */
  { 0x28, "ccib" },          /* Dots: two colours, count, seed */
  { 0x31, "eeeecbbe" },      /* Text: position, size, colour, flags, external space, internal space (no line skip before 2007) */
};

static void test_contract(const wz3_kx *kx)
{
  for (size_t i = 0; i < sizeof g_contracts / sizeof g_contracts[0]; i++) {
    const wz3_kx_class *cls = NULL;
    for (uint32_t c = 0; c < kx->n_classes; c++)
      if (kx->classes[c].id == g_contracts[i].id) cls = &kx->classes[c];
    CHECK(cls != NULL, "class 0x%02x is in the document", g_contracts[i].id);
    if (!cls) continue;
    char lower[64];
    size_t n = strlen(cls->packing);
    for (size_t k = 0; k < n && k < sizeof lower - 1; k++)
      lower[k] = (char)((cls->packing[k] >= 'A' && cls->packing[k] <= 'Z') ? cls->packing[k] + 32 : cls->packing[k]);
    lower[n < sizeof lower - 1 ? n : sizeof lower - 1] = 0;
    CHECK(!strcmp(lower, g_contracts[i].packing), "class 0x%02x packing '%s', the handler reads '%s'", g_contracts[i].id, cls->packing, g_contracts[i].packing);
  }
  /* the classes with fewer parameters than their 2007 form: the handler defaults the missing ones (Cylinder: no arc) */
  for (uint32_t c = 0; c < kx->n_classes; c++)
    if (kx->classes[c].id == 0x82)
      CHECK(!strcmp(kx->classes[c].packing, "bbbb"), "Cylinder has four parameters (the arc is missing): '%s'", kx->classes[c].packing);
}

static uint8_t *read_all(const char *dir, const char *name, size_t *size)
{
  char path[1024];
  snprintf(path, sizeof path, "%s/%s", dir, name);
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  const long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t *data = (uint8_t *)malloc((size_t)n + 1);
  if (!data || fread(data, 1, (size_t)n, f) != (size_t)n) {
    fclose(f);
    free(data);
    return NULL;
  }
  fclose(f);
  *size = (size_t)n;
  return data;
}

int suite_wz3_theta_ops(int argc, char **argv)
{
  printf("operators of fr-038: theta\n");
  test_extrude();
  test_triangulate();
  test_smooth_angle();
  test_selection();
  test_color();
  test_perlin_displace();
  test_displace();
  test_uv_round();
  test_dots();
  if (argc > 1) {
    size_t size;
    uint8_t *bytes = read_all(argv[1], "theta.kx", &size);
    wz3_kx *kx = NULL;
    CHECK(bytes && wz3_kx_parse(&kx, bytes, size, NULL) == WZ3_OK, "theta.kx parses");
    if (kx) test_contract(kx);
    wz3_kx_destroy(kx);
    free(bytes);
  }
  printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
