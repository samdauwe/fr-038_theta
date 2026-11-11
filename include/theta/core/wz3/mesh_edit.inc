/* ======================================================================== */
/*  GenMesh editing (genmesh.cpp): the half-edge surgery and the operators  */
/*  The mesh editing operators of the document                              */
/*                                                                          */
/*  Extrude, SmAngle, Select Random / Face / Sphere, Color, Triangulate,    */
/*  Displace, Perlin Displace. Every function follows the C++ it is named   */
/*  after, statement by statement, on the record layout of mesh.inc. Index  */
/*  walks that the source leaves unbounded carry a guard instead, so a      */
/*  corrupt document ends in an error and never in a loop. Functions return */
/*  0, or -1 with m->oom set (allocation) or after a guard tripped.         */
/* ======================================================================== */

#define WZ3__ME_UV0      4 /* index of the first UV set in wz3__mvert.v */
#define WZ3__ME_GUARD(m) ((int64_t)(m)->n_edges * 2 + 4)

enum { WZ3__ME_NORMALS = WZ3__MF_NORMAL | WZ3__MF_TANGENT };

static int wz3__me_fail(wz3_mesh *m)
{
  m->oom = 1;
  return -1;
}

/* Sel(mask, state, mode) of an element record: the byte of the 24-bit mask that belongs to it */
static inline void wz3__me_sel_vert(wz3__mvert *v, uint32_t mask, int state, int mode) { v->mask = wz3__sel_elem(v->mask, mask >> 16, state, mode); }
static inline void wz3__me_sel_face(wz3__mface *f, uint32_t mask, int state, int mode) { f->mask = wz3__sel_elem(f->mask, mask >> 8, state, mode); }
static inline void wz3__me_sel_edge(wz3__medge *e, uint32_t mask, int state, int mode) { e->mask = wz3__sel_elem(e->mask, mask, state, mode); }

static inline int32_t wz3__me_next_vert_edge(const wz3_mesh *m, int32_t h) { return wz3__mh_prev_face(m, h) ^ 1; }

/* AddNewVert */
static int wz3__me_add_new_vert(wz3_mesh *m, int32_t *out)
{
  wz3__mvert *v;
  if (wz3__mesh_push_vertex(m, &v)) return -1;
  const int32_t id = (int32_t)m->n_verts - 1;
  v->first = v->next = v->re_index = id;
  *out = id;
  return 0;
}

/* AddCopiedVert: the record of `src` (First and Next included), ReIndex pointing at itself */
static int wz3__me_add_copied_vert(wz3_mesh *m, int32_t src, int32_t *out)
{
  const wz3__mvert copy = m->verts[src];
  wz3__mvert *v;
  if (wz3__mesh_push_vertex(m, &v)) return -1;
  *v = copy;
  const int32_t id = (int32_t)m->n_verts - 1;
  v->re_index = id;
  *out = id;
  return 0;
}

/* CopyVert: the attribute vectors */
static void wz3__me_copy_vert(wz3_mesh *m, int32_t dest, int32_t src) { memcpy(m->verts[dest].v, m->verts[src].v, sizeof m->verts[dest].v); }

/* IsBorderEdge */
static int wz3__me_is_border_edge(const wz3_mesh *m, int32_t h, int mode)
{
  const wz3__medge *e = &m->edges[h >> 1];
  const int s0 = m->faces[e->face[h & 1]].select;
  int s1 = m->faces[e->face[(h & 1) ^ 1]].select;
  if ((e->crease & WZ3__ME_NORMALS) || mode) s1 = 0;
  return s0 && !s1;
}

/* FixVertCycle: re-link the corner records around the vertex of half-edge `i` */
static int wz3__me_fix_vert_cycle(wz3_mesh *m, int32_t i)
{
  int32_t v = m->verts[wz3__mh_vert(m, i)].re_index;
  const int32_t v0 = v;
  m->verts[v].first = v0;
  m->verts[v].next = v0;
  int32_t e = i;
  int64_t guard = 0;
  do {
    m->edges[e >> 1].vert[e & 1] = v;
    e = wz3__me_next_vert_edge(m, e);
    const int32_t vn = m->verts[wz3__mh_vert(m, e)].re_index;
    if (m->edges[e >> 1].crease) {
      if (vn != v && vn != v0) {
        m->verts[v].next = vn;
        m->verts[vn].first = v0;
        m->verts[vn].next = v0;
      }
      v = vn;
    }
    if (++guard > WZ3__ME_GUARD(m)) return wz3__me_fail(m);
  } while (e != i);
  return 0;
}

/* SplitFace: a new edge from the start of i0 to the start of i1; the face of the new edge on the i1 side becomes a face of its own */
static int wz3__me_split_face(wz3_mesh *m, int32_t i0, int32_t i1, uint32_t dmask, int dmode)
{
  const int32_t p0 = wz3__mh_prev_face(m, i0), p1 = wz3__mh_prev_face(m, i1);
  const int32_t f0 = wz3__mh_face(m, i0);
  const int32_t v0 = wz3__mh_vert(m, i0), v1 = wz3__mh_vert(m, i1);
  const wz3__medge template_edge = m->edges[i0 >> 1];
  const wz3__mface template_face = m->faces[f0];
  wz3__medge *edge;
  wz3__mface *face;
  if (wz3__mesh_push_edge(m, &edge)) return -1;
  const int32_t ee = (int32_t)m->n_edges - 1;
  if (wz3__mesh_push_face(m, &face)) return -1;
  const int32_t f1 = (int32_t)m->n_faces - 1;
  edge = &m->edges[ee];
  *edge = template_edge;
  edge->crease = 0;
  wz3__me_sel_edge(edge, dmask, 1, dmode);
  edge->vert[0] = v1;
  edge->vert[1] = v0;
  edge->face[0] = f0;
  edge->face[1] = f0;
  edge->next[0] = i0;
  edge->next[1] = i1;
  edge->prev[0] = p1;
  edge->prev[1] = p0;
  edge->temp[0] = edge->temp[1] = -1;

  m->edges[i0 >> 1].prev[i0 & 1] = ee * 2;
  m->edges[p0 >> 1].next[p0 & 1] = ee * 2 + 1;
  m->edges[i1 >> 1].prev[i1 & 1] = ee * 2 + 1;
  m->edges[p1 >> 1].next[p1 & 1] = ee * 2;

  face = &m->faces[f1];
  *face = template_face;
  wz3__me_sel_face(face, dmask, 1, dmode);
  m->faces[f0].edge = i0;
  m->faces[f1].edge = i1;

  int32_t i = i1;
  int64_t guard = 0;
  do {
    m->edges[i >> 1].face[i & 1] = f1;
    i = wz3__mh_next_face(m, i);
    if (++guard > WZ3__ME_GUARD(m)) return wz3__me_fail(m);
  } while (i != i1);
  return 0;
}

/* SplitBridge: a "bridge" edge between the corner of half-edge d and the corner after a^1; *va is the vertex for the new corner
   (it receives the vertex at the start of the bridge, *vb the one at its end); dv1/dv2 report a vertex that was split for a crease */
static int wz3__me_split_bridge(wz3_mesh *m, int32_t a, int32_t d, int32_t *va, int32_t *vb, uint32_t dmask, int dmode, int32_t *dv1, int32_t *dv2)
{
  wz3__medge *edge;
  if (wz3__mesh_push_edge(m, &edge)) return -1;
  const int32_t e = (int32_t)m->n_edges - 1;
  int32_t v0 = *va;
  wz3__me_sel_vert(&m->verts[v0], dmask, 1, dmode);
  m->verts[v0].first = v0;
  m->verts[v0].next = v0;

  a ^= 1;
  int32_t p0 = wz3__mh_prev_face(m, d);
  int32_t n1 = wz3__mh_next_face(m, a);
  int32_t v1 = wz3__mh_vert(m, n1);

  int32_t cf = 0, lc = 0;
  if (n1 == d) {
    p0 = e * 2 + 1;
    n1 = e * 2;
  } else {
    int64_t guard = 0;
    for (int32_t i = n1; i != d; i = wz3__mh_prev_vert(m, i)) {
      if (m->edges[i >> 1].crease) {
        cf |= m->edges[i >> 1].crease;
        lc = i;
      }
      if (++guard > WZ3__ME_GUARD(m)) return wz3__me_fail(m);
    }
  }

  edge = &m->edges[e];
  *edge = m->edges[a >> 1];
  edge->crease = cf;
  wz3__me_sel_edge(edge, dmask, 1, dmode);
  edge->vert[0] = v0;
  edge->vert[1] = v1;
  edge->face[0] = wz3__mh_face(m, d);
  edge->face[1] = wz3__mh_face(m, a);
  const int32_t n0 = d, p1 = a;
  edge->next[0] = n0;
  edge->next[1] = n1;
  edge->prev[0] = p0;
  edge->prev[1] = p1;
  edge->temp[0] = edge->temp[1] = -1;

  m->edges[n0 >> 1].prev[n0 & 1] = e * 2;
  m->edges[p0 >> 1].next[p0 & 1] = e * 2;
  m->edges[n1 >> 1].prev[n1 & 1] = e * 2 + 1;
  m->edges[p1 >> 1].next[p1 & 1] = e * 2 + 1;

  if (cf) {
    v0 = wz3__mh_vert(m, wz3__mh_prev_vert(m, lc) ^ 1);
    if (wz3__me_add_copied_vert(m, v0, &v1)) return -1;
    wz3__me_sel_vert(&m->verts[v1], dmask, 1, dmode);
    m->edges[lc >> 1].vert[lc & 1] = v1;
    if (dv1) {
      *dv1 = v0;
      *dv2 = v1;
    }
  } else if (dv1) {
    *dv1 = -1;
  }

  if (wz3__me_fix_vert_cycle(m, e * 2) || wz3__me_fix_vert_cycle(m, e * 2 + 1)) return -1;
  *va = wz3__mh_vert(m, e * 2);
  *vb = wz3__mh_vert(m, n1);
  return 0;
}

/* Crease(selType, dmask, dmode, what): sel type 0 = boundary of the selected faces, 1 = selected edges, else selected faces */
static int wz3__me_crease(wz3_mesh *m, int sel_type, uint32_t dmask, int dmode, int32_t what)
{
  const int32_t halfedges = (int32_t)m->n_edges * 2;
  for (int32_t i = 0; i < halfedges; i++) {
    const wz3__medge *edge = &m->edges[i >> 1];
    int sel;
    if (sel_type == 0)
      sel = m->faces[edge->face[i & 1]].select && !m->faces[edge->face[(i & 1) ^ 1]].select;
    else if (sel_type == 1)
      sel = edge->select;
    else
      sel = m->faces[edge->face[i & 1]].select;
    if (!sel) continue;
    if (!m->edges[i >> 1].crease) {
      for (int j = 0; j < 2; j++) {
        int32_t k = i ^ j;
        const int32_t v0 = wz3__mh_vert(m, k);
        const int32_t vf = m->verts[v0].first;
        int32_t got = 0, e = k;
        int64_t guard = 0;
        do {
          got |= m->edges[e >> 1].crease;
          e = wz3__me_next_vert_edge(m, e);
          if (++guard > WZ3__ME_GUARD(m)) return wz3__me_fail(m);
        } while (e != k);
        if (got) { /* at least one crease on this vertex already: split it */
          int32_t v;
          if (wz3__me_add_copied_vert(m, v0, &v)) return -1;
          wz3__me_sel_vert(&m->verts[v], dmask, 1, dmode);
          m->verts[v].first = vf;
          m->verts[v0].next = v;
          wz3__me_copy_vert(m, v, v0);
          guard = 0;
          do {
            m->edges[k >> 1].vert[k & 1] = v;
            k = wz3__me_next_vert_edge(m, k);
            if (++guard > WZ3__ME_GUARD(m)) return wz3__me_fail(m);
          } while (!m->edges[k >> 1].crease);
        }
      }
    }
    m->edges[i >> 1].crease |= what;
  }
  return 0;
}

/* CalcFaceNormalAccurate, with its loop condition as written: it walks on while the corner normal is not degenerate */
static void wz3__me_face_normal_accurate(const wz3_mesh *m, int32_t f, float out[3])
{
  const int32_t start = m->faces[f].edge;
  float n[3] = { 0, 0, 0 };
  double l = 0;
  int32_t e = start;
  int64_t guard = 0;
  do {
    const int32_t ne = wz3__mh_next_face(m, e);
    const float *a = m->verts[wz3__mh_vert(m, wz3__mh_prev_face(m, e))].v[0];
    const float *b = m->verts[wz3__mh_vert(m, e)].v[0];
    const float *c = m->verts[wz3__mh_vert(m, ne)].v[0];
    const float t1x = b[0] - a[0], t1y = b[1] - a[1], t1z = b[2] - a[2];
    const float t2x = c[0] - a[0], t2y = c[1] - a[1], t2z = c[2] - a[2];
    n[0] = t1y * t2z - t1z * t2y;
    n[1] = t1z * t2x - t1x * t2z;
    n[2] = t1x * t2y - t1y * t2x;
    e = ne;
    l = (double)n[0] * n[0] + (double)n[1] * n[1] + (double)n[2] * n[2];
    if (++guard > WZ3__ME_GUARD(m)) break;
  } while (l > 1e-40 && e != start);
  if (l >= 1e-40) {
    const double inverse = 1.0 / sqrt(l);
    out[0] = (float)(n[0] * inverse);
    out[1] = (float)(n[1] * inverse);
    out[2] = (float)(n[2] * inverse);
  } else {
    out[0] = 1.0f;
    out[1] = out[2] = 0.0f;
  }
}

/* ---- selection operators ---- */

/* Mesh_SelectRandom */
static wz3_mesh *wz3__mesh_op_select_random(const wz3_mesh *in, uint32_t dmask, int32_t dmode, uint32_t ratio, int32_t seed)
{
  wz3_mesh *m = wz3__mesh_checked_copy(in, 0x80000000u);
  if (!m) return NULL;
  wz3_rng random;
  wz3_rng_init(&random);
  wz3_rng_seed(&random, (int32_t)(((uint32_t)seed + (uint32_t)seed * 31743u) ^ ((uint32_t)seed << 23)));
  for (uint32_t i = 0; i < m->n_edges; i++) m->edges[i].select = (wz3_rng_u32(&random) >> 24) <= ratio;
  for (uint32_t i = 0; i < m->n_verts; i++) m->verts[i].select = (wz3_rng_u32(&random) >> 24) <= ratio;
  for (uint32_t i = 0; i < m->n_verts; i++) {
    const int32_t first = m->verts[i].first;
    if (first >= 0 && (uint32_t)first < m->n_verts) m->verts[i].select = m->verts[first].select;
  }
  for (uint32_t i = 0; i < m->n_faces; i++) {
    const int sel = (wz3_rng_u32(&random) >> 24) <= ratio;
    m->faces[i].select = sel && m->faces[i].material;
  }
  wz3__mesh_selection_to_mask(m, dmask, dmode);
  return m;
}

/* Mesh_SelectSphere (GenMesh::SelectSphere on the vertex positions as stored) */
static wz3_mesh *wz3__mesh_op_select_sphere(const wz3_mesh *in, uint32_t dmask, int32_t dmode, const double center[3], const double size_in[3])
{
  wz3_mesh *m = wz3__mesh_checked_copy(in, 0x80000000u);
  if (!m) return NULL;
  const float size[3] = { (float)(size_in[0] * 0.5f), (float)(size_in[1] * 0.5f), (float)(size_in[2] * 0.5f) };
  for (uint32_t i = 0; i < m->n_verts; i++) {
    float r = 0.0f;
    for (int j = 0; j < 3; j++) {
      const float t = (m->verts[i].v[0][j] - (float)center[j]) / size[j];
      r += t * t;
    }
    m->verts[i].select = r <= 1.0f;
  }
  wz3__mesh_vertex_to_face_edge(m);
  if (dmode & 4) wz3__mesh_face_to_vertex(m);
  wz3__mesh_selection_to_mask(m, dmask, dmode & 3);
  return m;
}

/* Mesh_SelectFace */
static wz3_mesh *wz3__mesh_op_select_face(const wz3_mesh *in, uint32_t dmask, int32_t dmode, int32_t face)
{
  wz3_mesh *m = wz3__mesh_checked_copy(in, 0x80000000u);
  if (!m) return NULL;
  wz3__mesh_all_to_selection(m, 0, WZ3__ALL_FACE);
  if (face >= 0 && (uint32_t)face < m->n_faces && m->faces[face].material) m->faces[face].select = 1;
  wz3__mesh_selection_to_mask(m, dmask << 8, dmode);
  return m;
}

/* Mesh_SmoothAngle: crease the normals and tangents along the edges whose faces meet at more than `angle` (quarter turns) */
static wz3_mesh *wz3__mesh_op_smooth_angle(const wz3_mesh *in, double angle_in)
{
  wz3_mesh *m = wz3__mesh_checked_copy(in, 0x80000000u);
  if (!m) return NULL;
  const float limit = (float)wz3__cos((double)(float)(angle_in * 6.283185307179586 * 0.25));
  for (uint32_t i = 0; i < m->n_edges; i++) {
    float n1[3], n2[3];
    wz3__me_face_normal_accurate(m, m->edges[i].face[0], n1);
    wz3__me_face_normal_accurate(m, m->edges[i].face[1], n2);
    const float dot = n1[0] * n2[0] + n1[1] * n2[1] + n1[2] * n2[2];
    m->edges[i].select = (dot < 0 ? -dot : dot) < limit;
  }
  if (wz3__me_crease(m, 1, 0, 0, WZ3__ME_NORMALS)) {
    wz3__mesh_free(m);
    return NULL;
  }
  m->got_normals = 0;
  return m;
}

/* Mesh_Color: bakes light into the vertex colours (0 directional, 1 point, 3 ambient; 2 needs light slots, which no document makes) */
static wz3_mesh *wz3__mesh_op_color(const wz3_mesh *in, const double pos[3], const double dir[2], uint32_t color, double amplify, double range, int32_t op)
{
  wz3_mesh *m = wz3__mesh_checked_copy(in, 0x80000000u);
  if (!m) return NULL;
  if (wz3__mesh_need_normals(m)) {
    wz3__mesh_free(m);
    return NULL;
  }
  float matrix[16];
  wz3_mat4_euler((float)(dir[0] * 6.283185307179586), 0.0f, (float)(dir[1] * 6.283185307179586), matrix);
  const float light_dir[3] = { matrix[1], matrix[5], matrix[9] };
  const float light_color[3] = { (float)((double)((color >> 16) & 255u) / 255.0 * amplify), (float)((double)((color >> 8) & 255u) / 255.0 * amplify),
                                 (float)((double)(color & 255u) / 255.0 * amplify) };
  const float rsq = (float)(range * range), inverse_range = (float)(1.0 / range);
  for (uint32_t i = 0; i < m->n_verts; i++) {
    float *p = m->verts[i].v[0], *n = m->verts[i].v[1], *c = m->verts[i].v[3];
    float intensity = 0.0f;
    switch (op) {
    case 0:
      intensity = light_dir[0] * n[0] + light_dir[1] * n[1] + light_dir[2] * n[2];
      if (!(intensity > 0.0f)) continue;
      break;
    case 1: {
      const float d[3] = { (float)pos[0] - p[0], (float)pos[1] - p[1], (float)pos[2] - p[2] };
      const float len = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
      if (!(len < rsq)) continue;
      intensity = d[0] * n[0] + d[1] * n[1] + d[2] * n[2];
      if (!(intensity > 0.0f)) continue;
      intensity *= (float)(1.0 / sqrt((double)len) - inverse_range);
      break;
    }
    case 3:
      c[0] += light_color[0];
      c[1] += light_color[1];
      c[2] += light_color[2];
      continue;
    default: continue;
    }
    c[0] += light_color[0] * intensity;
    c[1] += light_color[1] * intensity;
    c[2] += light_color[2] * intensity;
  }
  return m;
}

/* ---- Triangulate ---- */

#define WZ3__ME_MAX_CORNERS 256

static int wz3__me_triangulate(wz3_mesh *m, int32_t threshold, uint32_t dmask, int dmode, int32_t type)
{
  int32_t corner_edge[WZ3__ME_MAX_CORNERS], corner_vert[WZ3__ME_MAX_CORNERS];
  for (uint32_t i = 0; i < m->n_faces; i++) { /* the faces this adds are visited too, as in the source */
    if (!m->faces[i].select || !m->faces[i].material) continue;
    int32_t e = m->faces[i].edge;
    const int32_t ee = e;
    int32_t c = 0;
    do {
      if (c >= WZ3__ME_MAX_CORNERS) return wz3__me_fail(m);
      corner_edge[c] = e;
      corner_vert[c++] = wz3__mh_vert(m, e);
      e = wz3__mh_next_face(m, e);
    } while (e != ee);
    if (c < threshold) continue;
    switch (type) {
    case 0: { /* a vertex in the middle, a fan of triangles around it */
      int32_t v;
      if (wz3__me_add_new_vert(m, &v)) return -1;
      wz3__me_sel_vert(&m->verts[v], dmask, 1, dmode);
      memset(m->verts[v].v, 0, sizeof m->verts[v].v);
      const float ic = (float)(1.0 / c);
      for (int32_t j = 0; j < c; j++) {
        for (int k = 0; k < 6; k++)
          for (int w = 0; w < 4; w++) m->verts[v].v[k][w] += m->verts[corner_vert[j]].v[k][w] * ic;
        int32_t end;
        if (j == 0) {
          if (wz3__me_split_bridge(m, wz3__me_next_vert_edge(m, corner_edge[0]), corner_edge[0], &v, &end, dmask, dmode, NULL, NULL)) return -1;
        } else if (wz3__me_split_face(m, wz3__mh_prev_face(m, wz3__mh_prev_face(m, corner_edge[j])), corner_edge[j], dmask, dmode)) {
          return -1;
        }
      }
      break;
    }
    case 1: /* a fan from the first corner */
      for (int32_t j = 0; j < c - 3; j++)
        if (wz3__me_split_face(m, wz3__mh_prev_face(m, wz3__mh_prev_face(m, corner_edge[j])), corner_edge[j], dmask, dmode)) return -1;
      break;
    case 2: { /* the cylinder cap with an arc: start at the corner with the smallest z */
      int32_t start = 0;
      for (int32_t j = 0; j < c; j++)
        if (m->verts[corner_vert[j]].v[0][2] < m->verts[corner_vert[start]].v[0][2]) start = j;
      if (start != 0) {
        for (int32_t j = start + 3; j < start + c; j++)
          if (wz3__me_split_face(m, wz3__mh_prev_face(m, wz3__mh_prev_face(m, corner_edge[j % c])), corner_edge[j % c], dmask, dmode)) return -1;
      } else {
        for (int32_t j = start + 1; j < start + c - 2; j++)
          if (wz3__me_split_face(m, wz3__mh_prev_face(m, wz3__mh_prev_face(m, corner_edge[j % c])), corner_edge[j % c], dmask, dmode)) return -1;
      }
      break;
    }
    default: break;
    }
  }
  return 0;
}

/* Mesh_Triangulate: the faces of `mask` with at least `threshold` corners */
static wz3_mesh *wz3__mesh_op_triangulate(const wz3_mesh *in, int32_t mask, int32_t threshold, int32_t type)
{
  wz3_mesh *m = wz3__mesh_checked_copy(in, (uint32_t)mask << 8);
  if (!m) return NULL;
  if (wz3__me_triangulate(m, threshold, 0, 0, type)) {
    wz3__mesh_free(m);
    return NULL;
  }
  m->got_normals = 0;
  return m;
}

/* ---- Extrude ---- */

/* GenMesh::Extrude: every border edge of the selected faces gets a bridge to a copy of its start vertex, then the side faces are cut */
static int wz3__me_extrude(wz3_mesh *m, uint32_t dmask, int dmode, int mode)
{
  const int32_t ec = (int32_t)m->n_edges;
  for (int32_t i = 0; i < ec * 2; i++) {
    m->edges[i >> 1].temp[i & 1] = -1;
    if (!wz3__me_is_border_edge(m, i, mode)) continue;
    m->edges[i >> 1].temp[i & 1] = 0;
    int32_t e = i;
    int64_t guard = 0;
    do {
      e = wz3__me_next_vert_edge(m, e);
      if (++guard > WZ3__ME_GUARD(m)) return wz3__me_fail(m);
    } while (!wz3__me_is_border_edge(m, e ^ 1, mode));
    int32_t va, vb, s1 = -1, o1 = -1;
    if (wz3__me_add_copied_vert(m, wz3__mh_vert(m, i), &va)) return -1;
    const int32_t ov = va;
    if (wz3__me_split_bridge(m, e, i, &va, &vb, dmask, dmode, &s1, &o1)) return -1;
    wz3__me_copy_vert(m, ov, wz3__mh_vert(m, i));
    if (s1 != -1) wz3__me_copy_vert(m, o1, s1);
  }
  for (int32_t i = 0; i < ec * 2; i++) {
    if (m->edges[i >> 1].temp[i & 1]) continue;
    if (wz3__me_split_face(m, wz3__mh_next_face(m, wz3__mh_next_face(m, i)), wz3__mh_prev_face(m, i), 0, 0)) return -1;
    wz3__mface *last = &m->faces[m->n_faces - 1];
    last->mask = 0;
    last->select = 0;
    wz3__me_sel_face(last, dmask, 1, dmode);
  }
  return 0;
}

/* sVector::Rotate3 / Rotate34 with the matrix columns i, j, k, l at m[0..3], m[4..7], m[8..11], m[12..15] */
static void wz3__me_rotate3(const float mat[16], float v[4])
{
  const float x = v[0], y = v[1], z = v[2];
  v[0] = mat[0] * x + mat[4] * y + mat[8] * z;
  v[1] = mat[1] * x + mat[5] * y + mat[9] * z;
  v[2] = mat[2] * x + mat[6] * y + mat[10] * z;
}

static void wz3__me_unit_safe3(float v[3])
{
  const double e = (double)v[0] * v[0] + (double)v[1] * v[1] + (double)v[2] * v[2];
  if (e > 1e-20) {
    const float s = (float)(1.0 / sqrt(e));
    v[0] *= s;
    v[1] *= s;
    v[2] *= s;
  } else {
    v[0] = 1.0f;
    v[1] = v[2] = 0.0f;
  }
}

/* Mesh_Extrude (smask: faces to extrude, dmask: masks the new geometry gets; mode bits 0: borders always, 1-2: normal source
   (0 individual, 1 average, 2 fixed direction, 3 face), 3: scale around the centre of each group, 4-5: selection mode) */
static wz3_mesh *wz3__mesh_op_extrude(const wz3_mesh *in, int32_t smask, int32_t dmask_in, int32_t mode, int32_t count_in, const double dist_in[3],
                                      const double scale_in[3], const double rotate_in[3])
{
  wz3_mesh *m = wz3__mesh_checked_copy(in, (uint32_t)smask << 8);
  if (!m) return NULL;
  if (wz3__mesh_need_normals(m)) {
    wz3__mesh_free(m);
    return NULL;
  }
  const uint32_t dmask = (uint32_t)dmask_in;
  int32_t *stack = (int32_t *)WZ3_MALLOC(((size_t)m->n_faces + 1) * sizeof(int32_t));
  if (!stack) {
    wz3__mesh_free(m);
    return NULL;
  }
  int32_t grp = ((mode & 8) || ((mode & 6) == 2)) ? 0 : 1;
  for (uint32_t i = 0; i < m->n_faces; i++) m->faces[i].temp = grp - 1;
  for (;;) { /* the connected groups of selected faces */
    uint32_t seed = 0;
    while (seed < m->n_faces && !(m->faces[seed].temp == -1 && m->faces[seed].select)) seed++;
    if (seed == m->n_faces) break;
    uint32_t sp = 0;
    stack[sp++] = (int32_t)seed;
    while (sp) {
      int32_t f = stack[--sp];
      m->faces[f].temp = grp;
      int32_t e = m->faces[f].edge;
      const int32_t ee = e;
      int64_t guard = 0;
      do {
        const int32_t neighbour = wz3__mh_face(m, e ^ 1);
        if (m->faces[neighbour].temp == -1 && !wz3__me_is_border_edge(m, e, mode & 1) && sp <= m->n_faces) {
          m->faces[neighbour].temp = -2;
          stack[sp++] = neighbour;
        }
        e = wz3__mh_next_face(m, e);
        if (++guard > WZ3__ME_GUARD(m)) {
          WZ3_FREE(stack);
          wz3__mesh_free(m);
          return NULL;
        }
      } while (e != ee);
    }
    grp++;
  }
  WZ3_FREE(stack);

  const int32_t count = count_in;
  const float ic = count > 0 ? (float)(1.0 / count) : 0.0f;
  float dist[3], scale[3], rotate[3];
  for (int i = 0; i < 3; i++) {
    const float s = (float)scale_in[i];
    scale[i] = (float)(wz3__pow(fabs((double)s), (double)ic) * (s < 0 ? -1.0 : 1.0));
    rotate[i] = (float)rotate_in[i] * ic;
    dist[i] = (float)dist_in[i] * ic;
  }
  float mat[16];
  wz3_mat4_euler_turns(rotate, mat);
  if (((mode >> 4) & 3) >= WZ3__SEL_SET) wz3__mesh_all_to_mask(m, (dmask & 0xffu) << 8, WZ3__SEL_SUB);
  float n[4] = { 0, 0, 0, 0 }, p[4] = { 0, 0, 0, 1 };
  if ((mode & 6) == 4) {
    n[0] = dist[0];
    n[1] = dist[1];
    n[2] = dist[2];
  }

  for (int32_t step = 0; step < count; step++) {
    if (wz3__me_extrude(m, (dmask & 0xffu) << 8, (mode >> 4) & 3, mode & 1)) {
      wz3__mesh_free(m);
      return NULL;
    }
    for (uint32_t i = 0; i < m->n_verts; i++) m->verts[i].temp = 0;
    int32_t vcount = 0;
    for (int32_t g = 0; g < grp; g++) {
      int32_t first = -1; /* the faces of group g, linked through temp2 */
      for (int32_t j = (int32_t)m->n_faces - 1; j >= 0; j--) {
        if (m->faces[j].select && m->faces[j].temp == g) {
          m->faces[j].temp2 = first;
          first = j;
        }
      }
      if ((mode & 6) == 6) { /* face normals into the vertex normals */
        for (int32_t f = first; f != -1; f = m->faces[f].temp2) {
          const int32_t e0 = m->faces[f].edge;
          const float *a = m->verts[wz3__mh_vert(m, wz3__mh_prev_face(m, e0))].v[0];
          const float *b = m->verts[wz3__mh_vert(m, e0)].v[0];
          const float *c = m->verts[wz3__mh_vert(m, wz3__mh_next_face(m, e0))].v[0];
          const float t1x = b[0] - a[0], t1y = b[1] - a[1], t1z = b[2] - a[2];
          const float t2x = c[0] - a[0], t2y = c[1] - a[1], t2z = c[2] - a[2];
          float normal[3] = { t1y * t2z - t1z * t2y, t1z * t2x - t1x * t2z, t1x * t2y - t1y * t2x };
          wz3__me_unit_safe3(normal);
          int32_t e = e0;
          int64_t guard = 0;
          do {
            float *vn = m->verts[wz3__mh_vert(m, e)].v[1];
            vn[0] = normal[0];
            vn[1] = normal[1];
            vn[2] = normal[2];
            e = wz3__mh_next_face(m, e);
            if (++guard > WZ3__ME_GUARD(m)) {
              wz3__mesh_free(m);
              return NULL;
            }
          } while (e != e0);
        }
      }
      for (int pass = 1; pass < 4; pass++) {
        const uint8_t pass_mask = (uint8_t)(1u << pass);
        if (pass == 1) {
          if ((mode & 6) != 2) continue;
          n[0] = n[1] = n[2] = n[3] = 0.0f;
        } else if (pass == 2) {
          if ((mode & 6) == 2) {
            wz3__me_unit_safe3(n);
            n[0] *= dist[0];
            n[1] *= dist[1];
            n[2] *= dist[2];
          }
          p[0] = p[1] = p[2] = 0.0f;
          p[3] = 1.0f;
        } else {
          if (!(mode & 8)) continue;
          const float inverse = (float)(1.0 / vcount);
          p[0] *= inverse;
          p[1] *= inverse;
          p[2] *= inverse;
        }
        vcount = 0;
        for (int32_t f = first; f != -1; f = m->faces[f].temp2) {
          const int32_t e0 = m->faces[f].edge;
          int32_t e = e0;
          int64_t guard = 0;
          do {
            const int32_t vid = wz3__mh_vert(m, e);
            e = wz3__mh_next_face(m, e);
            if (++guard > WZ3__ME_GUARD(m)) {
              wz3__mesh_free(m);
              return NULL;
            }
            wz3__mvert *vert = &m->verts[vid];
            if (vert->temp & pass_mask) continue;
            vert->temp |= pass_mask;
            float *vd = vert->v[0], *vn = vert->v[1];
            if (pass == 1) {
              n[0] += vn[0];
              n[1] += vn[1];
              n[2] += vn[2];
            } else if (pass == 2) {
              if ((mode & 6) == 0 || (mode & 6) == 6) {
                vd[0] += vn[0] * dist[0];
                vd[1] += vn[1] * dist[1];
                vd[2] += vn[2] * dist[2];
              } else {
                vd[0] += n[0];
                vd[1] += n[1];
                vd[2] += n[2];
              }
              if (mode & 8) {
                p[0] += vd[0];
                p[1] += vd[1];
                p[2] += vd[2];
              } else {
                vd[0] *= scale[0];
                vd[1] *= scale[1];
                vd[2] *= scale[2];
                wz3__me_rotate3(mat, vd);
              }
              wz3__me_rotate3(mat, vn);
            } else {
              const float t[3] = { (vd[0] - p[0]) * scale[0], (vd[1] - p[1]) * scale[1], (vd[2] - p[2]) * scale[2] };
              vd[0] = mat[0] * t[0] + mat[4] * t[1] + mat[8] * t[2] + mat[12] + p[0];
              vd[1] = mat[1] * t[0] + mat[5] * t[1] + mat[9] * t[2] + mat[13] + p[1];
              vd[2] = mat[2] * t[0] + mat[6] * t[1] + mat[10] * t[2] + mat[14] + p[2];
              vd[3] = mat[3] * t[0] + mat[7] * t[1] + mat[11] * t[2] + mat[15];
            }
            vcount++;
          } while (e != e0);
        }
      }
    }
  }
  if (dmask & 0xffu) wz3__mesh_mask_to_selection(m, (dmask & 0xffu) << 8);
  wz3__mesh_face_to_vertex(m);
  wz3__mesh_selection_to_mask(m, (dmask & 0xff00u) << 8, 0);
  m->got_normals = 0;
  return m;
}

/* ---- Perlin Displace, Displace, spherical and cylindrical UV projection ---- */

/* sVector::Rotate34 of a position */
static void wz3__me_transform_point(const float mat[16], const float v[4], float out[3])
{
  out[0] = mat[0] * v[0] + mat[4] * v[1] + mat[8] * v[2] + mat[12];
  out[1] = mat[1] * v[0] + mat[5] * v[1] + mat[9] * v[2] + mat[13];
  out[2] = mat[2] * v[0] + mat[6] * v[1] + mat[10] * v[2] + mat[14];
}

/* Mesh_Perlin: noise vectors (the gradient noise of GenMinMesh's Perlin) added to the selected vertices */
static wz3_mesh *wz3__mesh_op_perlin(const wz3_mesh *in, int32_t mask, const double srt[9], const double amplitude[3])
{
  wz3_mesh *m = wz3__mesh_checked_copy(in, (uint32_t)mask << 16);
  if (!m) return NULL;
  wz3__mm_perlin noise;
  wz3__mm_perlin_init(&noise);
  float matrix[16];
  wz3__mm_matrix_srt(srt, matrix);
  for (uint32_t i = 0; i < m->n_verts; i++) {
    if (!m->verts[i].select) continue;
    float point[3], sample[3];
    wz3__me_transform_point(matrix, m->verts[i].v[0], point);
    wz3__mm_perlin3(&noise, point, sample);
    m->verts[i].v[0][0] += sample[0] * (float)amplitude[0];
    m->verts[i].v[0][1] += sample[1] * (float)amplitude[1];
    m->verts[i].v[0][2] += sample[2] * (float)amplitude[2];
  }
  return m;
}

/* Mesh_Displace: the corners of a vertex share one displacement along their normals, the mean of the bitmap heights at their UVs */
static wz3_mesh *wz3__mesh_op_displace(const wz3_mesh *in, const wz3_bitmap *bitmap, int32_t mask, const double amplitude[3])
{
  if (!bitmap || bitmap->width <= 0 || bitmap->height <= 0) return NULL;
  wz3_mesh *m = wz3__mesh_checked_copy(in, (uint32_t)mask << 16);
  if (!m) return NULL;
  if (wz3__mesh_need_normals(m)) {
    wz3__mesh_free(m);
    return NULL;
  }
  wz3__bilinear ctx;
  wz3__bilinear_init(&ctx, bitmap, 0, bitmap->width, bitmap->height);
  const float x_scale = (float)bitmap->width * 65536.0f, y_scale = (float)bitmap->height * 65536.0f;
  const float amp[3] = { (float)amplitude[0], (float)amplitude[1], (float)amplitude[2] };
  for (uint32_t i = 0; i < m->n_verts; i++) {
    if (!m->verts[i].select) continue;
    int32_t v = (int32_t)i, count = 0;
    float disp[3] = { 0, 0, 0 };
    int64_t guard = 0;
    do {
      wz3__mvert *vert = &m->verts[v];
      uint16_t height[4];
      wz3__bilinear_sample(&ctx, wz3__rt_to_int32((double)(vert->v[4][0] * x_scale)), wz3__rt_to_int32((double)(vert->v[4][1] * y_scale)), height);
      const float h = ((float)height[0] - 16384.0f) * (1.0f / 32767.0f);
      disp[0] += vert->v[1][0] * h * amp[0];
      disp[1] += vert->v[1][1] * h * amp[1];
      disp[2] += vert->v[1][2] * h * amp[2];
      vert->select = 0;
      v = vert->next;
      count++;
      if (v < 0 || (uint32_t)v >= m->n_verts || ++guard > (int64_t)m->n_verts + 1) {
        wz3__mesh_free(m);
        return NULL;
      }
    } while (v != (int32_t)i);
    const float inverse = 1.0f / (float)count;
    disp[0] *= inverse;
    disp[1] *= inverse;
    disp[2] *= inverse;
    do {
      m->verts[v].v[0][0] += disp[0];
      m->verts[v].v[0][1] += disp[1];
      m->verts[v].v[0][2] += disp[2];
      v = m->verts[v].next;
    } while (v != (int32_t)i);
  }
  m->got_normals = 0;
  return m;
}

/* atan2 as the x87 computes it, from the fdlibm atan */
static double wz3__me_atan2(double y, double x)
{
  const double pi = 3.141592653589793;
  if (x > 0) return wz3__atan(y / x);
  if (x < 0) return y >= 0 ? wz3__atan(y / x) + pi : wz3__atan(y / x) - pi;
  return y > 0 ? pi * 0.5 : y < 0 ? -pi * 0.5 : 0.0;
}

/* Mesh_UVProjection types 1 (cylindrical) and 2 (spherical): u from the angle around y, v from the height or the latitude */
static wz3_mesh *wz3__mesh_op_uv_round(const wz3_mesh *in, const double srt[9], int32_t type)
{
  wz3_mesh *m = wz3__mesh_checked_copy(in, 0x80000000u);
  if (!m) return NULL;
  float matrix[16];
  wz3__mm_matrix_srt(srt, matrix);
  const double two_pi = 6.283185307179586, pi = 3.141592653589793;
  for (uint32_t i = 0; i < m->n_verts; i++) {
    float *v0 = m->verts[i].v[0], *uv = m->verts[i].v[WZ3__ME_UV0];
    float n[3];
    wz3__me_transform_point(matrix, v0, n);
    const float xzd = n[0] * n[0] + n[2] * n[2];
    uv[0] = (float)(0.5 - wz3__me_atan2((double)n[0], (double)n[2]) / two_pi);
    if (type == 1)
      uv[1] = -n[1];
    else
      uv[1] = (float)(0.5 - wz3__me_atan2((double)n[1], sqrt((double)xzd)) / pi);
  }
  return m;
}
