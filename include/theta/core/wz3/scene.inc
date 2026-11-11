/* ======================================================================== */
/*  scene traversal and IPP (genscene.cpp, genoverlay.cpp)                  */
/* ======================================================================== */

enum { WZ3__FJ_K_NONE = 0, WZ3__FJ_K_MESH, WZ3__FJ_K_MINMESH, WZ3__FJ_K_SCENE, WZ3__FJ_K_EFFECT };

static int wz3__fj_class_kind(uint8_t out_class)
{
  switch (out_class) {
  case WZ3_KC_MESH: return WZ3__FJ_K_MESH;
  case WZ3_KC_MINMESH: return WZ3__FJ_K_MINMESH;
  case WZ3_KC_SCENE: return WZ3__FJ_K_SCENE;
  case WZ3_KC_EFFECT: return WZ3__FJ_K_EFFECT;
  default: return WZ3__FJ_K_NONE;
  }
}

/* objectKind(value) */
static int wz3__fj_cache_kind(const wz3__rt_cache *c)
{
  switch (c->kind) {
  case WZ3__RC_MESH: return WZ3__FJ_K_MESH;
  case WZ3__RC_MINMESH: return WZ3__FJ_K_MINMESH;
  case WZ3__RC_SCENE: return WZ3__FJ_K_SCENE;
  case WZ3__RC_EFFECT: return WZ3__FJ_K_EFFECT;
  case WZ3__RC_MISSING: return wz3__fj_class_kind(c->class_id); /* MissingCache.outputClass */
  default: return WZ3__FJ_K_NONE;
  }
}

/* outputKind(op): the cache's kind, else the class's output class */
static int wz3__fj_output_kind(const wz3_rt *rt, uint32_t index)
{
  const wz3__rt_cache *c = &rt->ops[index].cache;
  const int kind = c->kind != WZ3__RC_NONE ? wz3__fj_cache_kind(c) : WZ3__FJ_K_NONE;
  return kind ? kind : wz3__fj_class_kind(rt->ops[index].out_class);
}

static void wz3__fj_scene_free(wz3__fj_scene *s)
{
  if (!s) return;
  WZ3_FREE(s->child_mesh);
  WZ3_FREE(s);
}

static wz3__fj_scene *wz3__fj_scene_new(uint32_t n_children)
{
  wz3__fj_scene *s = (wz3__fj_scene *)WZ3_MALLOC(sizeof *s);
  if (!s) return NULL;
  memset(s, 0, sizeof *s);
  s->draw_mesh = s->effect = -1;
  if (n_children) {
    s->child_mesh = (int32_t *)WZ3_MALLOC((size_t)n_children * sizeof(int32_t));
    if (!s->child_mesh) {
      WZ3_FREE(s);
      return NULL;
    }
  }
  return s;
}

/* makeScene(value): NULL for anything that is no scene, mesh, minmesh or effect (*ok is the allocation result) */
static wz3__fj_scene *wz3__fj_make_scene(const wz3__rt_cache *cache, int32_t op_index, int *ok)
{
  *ok = 1;
  if (!cache) return NULL;
  const int kind = wz3__fj_cache_kind(cache);
  if (!kind) return NULL;
  const wz3__fj_scene *source = (kind == WZ3__FJ_K_SCENE && cache->kind == WZ3__RC_SCENE) ? (const wz3__fj_scene *)cache->obj : NULL;
  wz3__fj_scene *s = wz3__fj_scene_new(source ? source->n_children : 0);
  if (!s) {
    *ok = 0;
    return NULL;
  }
  if (source) {
    s->draw_mesh = source->draw_mesh;
    s->effect = source->effect;
    s->n_children = source->n_children;
    if (source->n_children) memcpy(s->child_mesh, source->child_mesh, (size_t)source->n_children * sizeof(int32_t));
  } else if (kind == WZ3__FJ_K_MESH || kind == WZ3__FJ_K_MINMESH) {
    s->draw_mesh = op_index;
  } else if (kind == WZ3__FJ_K_EFFECT) {
    s->effect = op_index;
  }
  return s;
}

/* scene op init: `children` are the input slots whose makeScene() is not null (in order); mode 0: a scene with
   those children, 1 (Scene): makeScene(input 0) or an empty scene, 2 (Add/Multiply/Marker/Particles/...) as 0 */
static wz3_result wz3__fj_init_scene(wz3_rt *rt, wz3__rt_op *op, wz3__rt_cache *fresh)
{
  const uint32_t n = op->src->n_inputs;
  wz3__fj_scene *scene = NULL;
  int ok = 1;
  switch (op->class_id) {
  case 0xc0: { /* Scene */
    scene = wz3__fj_make_scene(wz3__rt_input_cache(rt, op, 0), n ? (int32_t)op->src->inputs[0] : -1, &ok);
    if (!ok) return WZ3_ERR_OUT_OF_MEMORY;
    if (!scene) {
      scene = wz3__fj_scene_new(0);
      if (!scene) return WZ3_ERR_OUT_OF_MEMORY;
    }
    break;
  }
  case 0xc1:
  case 0xc7: { /* Add, Limb: every input that makes a scene is a child */
    scene = wz3__fj_scene_new(n);
    if (!scene) return WZ3_ERR_OUT_OF_MEMORY;
    for (uint32_t i = 0; i < n; i++) {
      wz3__fj_scene *child = wz3__fj_make_scene(wz3__rt_input_cache(rt, op, i), (int32_t)op->src->inputs[i], &ok);
      if (!ok) {
        wz3__fj_scene_free(scene);
        return WZ3_ERR_OUT_OF_MEMORY;
      }
      if (!child) continue;
      scene->child_mesh[scene->n_children++] = child->draw_mesh;
      wz3__fj_scene_free(child);
    }
    break;
  }
  case 0xc2:
  case 0xc3:
  case 0xc5:
  case 0x182:
  case 0xc8:
  case 0xc9: { /* Multiply, Transform, Particles, Marker, Walk, Rotate */
    wz3__fj_scene *child = wz3__fj_make_scene(wz3__rt_input_cache(rt, op, 0), n ? (int32_t)op->src->inputs[0] : -1, &ok);
    if (!ok) return WZ3_ERR_OUT_OF_MEMORY;
    if (!child && (op->class_id == 0xc2 || op->class_id == 0xc3)) return WZ3_OK; /* no cache */
    scene = wz3__fj_scene_new(child ? 1 : 0);
    if (!scene) {
      wz3__fj_scene_free(child);
      return WZ3_ERR_OUT_OF_MEMORY;
    }
    if (child) {
      scene->child_mesh[0] = child->draw_mesh;
      scene->n_children = 1;
      wz3__fj_scene_free(child);
    }
    break;
  }
  default: /* Light, MatHack, Ambient: an empty scene */
    scene = wz3__fj_scene_new(0);
    if (!scene) return WZ3_ERR_OUT_OF_MEMORY;
    break;
  }
  fresh->kind = WZ3__RC_SCENE;
  fresh->owned = 1;
  fresh->class_id = WZ3_KC_SCENE;
  fresh->obj = scene;
  return WZ3_OK;
}

/* ---- frame ---- */

static void wz3__fj_camera_env_default(wz3_camera *c)
{
  memset(c, 0, sizeof *c);
  wz3_mat4_identity(c->camera_space);
  wz3_mat4_identity(c->model_space);
  c->far_clip = 4096;
  c->near_clip = 0.125;
  c->zoom_x = c->zoom_y = 1;
  c->fog_color = 0xff808080u;
  c->fog_start = 0;
  c->fog_end = 4096;
}

static void wz3__fj_camera_defaults(wz3_camera *c)
{
  wz3__fj_camera_env_default(c);
  c->fog_color = 0;
}

static void wz3__fj_refresh_view(wz3_rt *rt)
{
  wz3__fj *fj = &rt->fj;
  fj->view.n_outputs = fj->outputs.n;
  fj->view.outputs = (const uint32_t *)fj->outputs.p;
  fj->view.n_nodes = fj->nodes.n;
  fj->view.nodes = (const wz3_ipp_node *)fj->nodes.p;
  fj->view.select_inputs = (const uint32_t *)fj->selects.p;
  if (!fj->ipp_scanned) { /* a document with a post-processing graph is drawn through sized render targets */
    fj->ipp_scanned = 1;
    for (uint32_t i = 0; i < rt->kx->n_ops; i++) {
      const uint16_t id = rt->kx->ops[i].class_id;
      if (id == 0xe6 || id == 0xe3 || id == 0xe7 || id == 0xe4 || id == 0xeb) fj->view.ipp_targets = 1;
    }
  }
  fj->view.mesh_jobs = (const wz3_mesh_job *)fj->mesh.p;
  fj->view.effect_jobs = (const wz3_effect_job *)fj->effect.p;
  fj->view.light_jobs = (const wz3_light_job *)fj->light.p;
  fj->view.instances = (const float *)fj->instance.p;
  fj->view.effect_variables = (const float *)fj->variables.p;
  fj->view.marker_tables = (const float *)fj->marker_tables.p;
}

/* Environment.initFrame, the part that concerns the frame jobs */
static void wz3__fj_init_frame(wz3_rt *rt)
{
  wz3__fj *fj = &rt->fj;
  fj->mesh.n = fj->effect.n = fj->light.n = fj->instance.n = fj->variables.n = fj->marker_tables.n = 0;
  fj->nodes.n = fj->outputs.n = fj->selects.n = 0;
  fj->b_mesh = fj->b_effect = fj->b_light = 0;
  fj->cur_table = -1;
  fj->ambient = 0;
  fj->last_output = -1;
  fj->render_pass_adjust = 0;
  if (fj->ipp_node)
    for (uint32_t i = 0; i < rt->n_ops; i++) fj->ipp_node[i] = -1;
  wz3__fj_camera_env_default(&fj->game_camera);
  wz3__fj_camera_env_default(&fj->current_camera);
  wz3__fj_refresh_view(rt);
}

/* ensureFrame(): the marker table frame.effectMarkers, created from the environment markers when absent */
static wz3_result wz3__fj_ensure_table(wz3_rt *rt)
{
  wz3__fj *fj = &rt->fj;
  if (fj->cur_table >= 0) return WZ3_OK;
  void *table;
  const wz3_result r = wz3__fj_push(&fj->marker_tables, 512 * sizeof(float), 1, &table);
  if (r != WZ3_OK) return r;
  memcpy(table, fj->markers, sizeof fj->markers);
  fj->cur_table = (int32_t)fj->marker_tables.n - 1;
  return WZ3_OK;
}

/* beginRenderFrame */
static wz3_result wz3__fj_begin_render_frame(wz3_rt *rt)
{
  wz3__fj *fj = &rt->fj;
  fj->b_mesh = fj->mesh.n;
  fj->b_effect = fj->effect.n;
  fj->b_light = fj->light.n;
  fj->cur_table = -1;
  fj->ambient = 0;
  return wz3__fj_ensure_table(rt);
}

static wz3_result wz3__fj_add_mesh_job(wz3_rt *rt, int32_t mesh_op, int32_t source_op, uint8_t has_instances, uint32_t first_instance, uint32_t n_instances,
                                       float time, int32_t pass_adjust)
{
  wz3_mesh_job *job;
  const wz3_result r = wz3__fj_push(&rt->fj.mesh, sizeof *job, 1, (void **)&job);
  if (r != WZ3_OK) return r;
  job->mesh_op = mesh_op;
  job->source_op = source_op;
  memcpy(job->matrix, wz3__fj_top(&rt->env), sizeof job->matrix);
  job->time = time;
  job->pass_adjust = pass_adjust;
  job->has_instances = has_instances;
  job->first_instance = first_instance;
  job->n_instances = n_instances;
  return WZ3_OK;
}

/* ---- execSceneInput(s) ---- */

static wz3_result wz3__fj_exec_scene_input(wz3_rt *rt, wz3__rt_op *op, uint32_t index)
{
  if (index >= op->src->n_inputs) return WZ3_OK;
  const uint32_t input = op->src->inputs[index];
  const int kind = wz3__fj_output_kind(rt, input);
  wz3__rt_env *env = &rt->env;
  if (kind == WZ3__FJ_K_MESH || kind == WZ3__FJ_K_MINMESH) {
    const wz3__fj_scene *scene = op->cache.kind == WZ3__RC_SCENE ? (const wz3__fj_scene *)op->cache.obj : NULL;
    int32_t mesh = -1;
    if (scene && scene->n_children) {
      if (index < scene->n_children) mesh = scene->child_mesh[index];
    } else if (scene) {
      mesh = scene->draw_mesh;
    }
    if (mesh < 0 && rt->ops[input].cache.kind != WZ3__RC_NONE) mesh = (int32_t)input;
    if (mesh < 0) return WZ3_OK;
    return wz3__fj_add_mesh_job(rt, mesh, (int32_t)input, 0, 0, 0, (float)wz3__fj_or0(env->vars[WZ3__FJ_KV_TIME][0]), rt->fj.render_pass_adjust);
  }
  if (kind == WZ3__FJ_K_EFFECT) {
    if (rt->ops[input].cache.kind != WZ3__RC_EFFECT) return WZ3_OK; /* an effect class without a handler yet (particles, print): nothing to run */
    wz3_result r = wz3__fj_ensure_table(rt);
    wz3_effect_job *job;
    float *vars;
    if (r != WZ3_OK) return r;
    if ((r = wz3__fj_push(&rt->fj.effect, sizeof *job, 1, (void **)&job)) != WZ3_OK) return r;
    if ((r = wz3__fj_push(&rt->fj.variables, WZ3__RT_MAX_VARS * 4 * sizeof(float), 1, (void **)&vars)) != WZ3_OK) {
      rt->fj.effect.n--;
      return r;
    }
    memcpy(vars, env->vars, sizeof env->vars);
    job->effect_op = input;
    memcpy(job->matrix, wz3__fj_top(env), sizeof job->matrix);
    job->variables = (rt->fj.variables.n - 1) * (WZ3__RT_MAX_VARS * 4);
    job->markers = (uint32_t)rt->fj.cur_table;
    job->pass_adjust = rt->fj.render_pass_adjust;
    return WZ3_OK;
  }
  return wz3__rt_exec_op(rt, input);
}

static wz3_result wz3__fj_exec_scene_inputs(wz3_rt *rt, wz3__rt_op *op)
{
  for (uint32_t i = 0; i < op->src->n_inputs; i++) {
    const wz3_result r = wz3__fj_exec_scene_input(rt, op, i);
    if (r != WZ3_OK) return r;
  }
  return WZ3_OK;
}

/* sceneTransformMatrix: srtFrom(parameters) with the defaults of an absent word */
static void wz3__fj_scene_matrix(const wz3__rt_op *op, float out[16])
{
  float srt[9];
  for (uint32_t i = 0; i < 9; i++) srt[i] = (float)wz3__fj_pd(op, i, i < 3 ? 1.0 : 0.0);
  wz3_mat4_srt(srt, out);
}

static wz3_result wz3__fj_exec_transformed(wz3_rt *rt, wz3__rt_op *op)
{
  float matrix[16];
  wz3__fj_scene_matrix(op, matrix);
  wz3_result r = wz3__rt_stack_push_mul(&rt->env, matrix);
  if (r != WZ3_OK) return r;
  r = wz3__fj_exec_scene_inputs(rt, op);
  wz3__rt_stack_pop(&rt->env);
  return r;
}

static wz3_result wz3__fj_exec_multiply(wz3_rt *rt, wz3__rt_op *op)
{
  wz3__rt_env *env = &rt->env;
  const int32_t count = wz3__fj_pi(op, 9);
  float matrix[16], next[16], saved[4];
  wz3__fj_scene_matrix(op, matrix);
  memcpy(saved, env->vars[WZ3__FJ_KV_SELECT], sizeof saved);
  wz3_result r = wz3__fj_stack_push(env, wz3__fj_top(env)); /* duplicate */
  for (int32_t i = 0; r == WZ3_OK && i < count; i++) {
    for (int k = 0; k < 4; k++) env->vars[WZ3__FJ_KV_SELECT][k] = (float)i;
    r = wz3__fj_exec_scene_inputs(rt, op);
    if (r != WZ3_OK) break;
    wz3_mat4_mul_a(matrix, wz3__fj_top(env), next);
    wz3__rt_stack_pop(env);
    r = wz3__fj_stack_push(env, next);
  }
  if (r == WZ3_OK) {
    wz3__rt_stack_pop(env);
    memcpy(env->vars[WZ3__FJ_KV_SELECT], saved, sizeof saved);
  }
  return r;
}

/* ---- Limb, Rotate, Walk (genscene.cpp): sVector/sMatrix are 4 floats / 16 floats (i, j, k, l) ---- */

#define WZ3__FJ_KV_LEG_L0    0x10
#define WZ3__FJ_KV_LEG_TIMES 0x1b

static void wz3__v_cross4(float *r, const float *a, const float *b)
{
  const float x = a[1] * b[2] - a[2] * b[1], y = a[2] * b[0] - a[0] * b[2], z = a[0] * b[1] - a[1] * b[0];
  r[0] = x;
  r[1] = y;
  r[2] = z;
  r[3] = 0.0f;
}
static float wz3__v_dot3(const float *a) { return a[0] * a[0] + a[1] * a[1] + a[2] * a[2]; }
static void wz3__v_unit3(float *v)
{
  const float s = 1.0f / sqrtf(wz3__v_dot3(v));
  v[0] *= s;
  v[1] *= s;
  v[2] *= s;
}
static void wz3__v_unit_safe3(float *v)
{
  const float e = wz3__v_dot3(v);
  if (e > 1e-20f) {
    const float s = 1.0f / sqrtf(e);
    v[0] *= s;
    v[1] *= s;
    v[2] *= s;
  } else {
    v[0] = 1.0f;
    v[1] = 0.0f;
    v[2] = 0.0f;
  }
}
/* r = m.i * v.x + m.j * v.y + m.k * v.z (xyz), Rotate3 */
static void wz3__v_rotate3(float *r, const float *m, const float *v)
{
  for (int c = 0; c < 3; c++) {
    float x = m[c] * v[0];
    x += m[4 + c] * v[1];
    x += m[8 + c] * v[2];
    r[c] = x;
  }
}
/* r = m.i * v.x + m.j * v.y + m.k * v.z + m.l (all four components), Rotate34 */
static void wz3__v_rotate34(float *r, const float *m, const float *v)
{
  for (int c = 0; c < 4; c++) {
    float x = m[c] * v[0];
    x += m[4 + c] * v[1];
    x += m[8 + c] * v[2];
    x += m[12 + c];
    r[c] = x;
  }
}

static wz3_result wz3__fj_exec_limb(wz3_rt *rt, wz3__rt_op *op)
{
  if (op->src->n_inputs != 2) return WZ3_OK;
  wz3__rt_env *env = &rt->env;
  float matrix[16], pos[3], dirv[3];
  memcpy(matrix, wz3__fj_top(env), sizeof matrix);
  for (uint32_t k = 0; k < 3; k++) {
    pos[k] = (float)wz3__fj_or0(wz3__fj_p(op, k));
    dirv[k] = (float)wz3__fj_or0(wz3__fj_p(op, 3 + k));
  }
  const float a = (float)wz3__fj_or0(wz3__fj_p(op, 6)), b = (float)wz3__fj_or0(wz3__fj_p(op, 7));
  const uint32_t flags = wz3__fj_pu(op, 8);
  if (!(flags & 2))
    for (int k = 0; k < 3; k++) pos[k] -= matrix[12 + k];
  float i[4] = { dirv[0], dirv[1], dirv[2], 0.0f }, j[4] = { -pos[0], -pos[1], -pos[2], 0.0f }, k[4];
  if (!(flags & 2)) {
    const float v[4] = { i[0], i[1], i[2], i[3] };
    wz3__v_rotate3(i, matrix, v);
  }
  wz3__v_cross4(k, i, j);
  wz3__v_cross4(i, j, k);
  const float len = sqrtf(wz3__v_dot3(j));
  wz3__v_unit3(i);
  wz3__v_unit3(j);
  wz3__v_unit3(k);
  const float t = (len * len + a * a - b * b) / (2.0f * len);
  float s = (a - t) * (a + t);
  s = s <= 0.0f ? 0.0f : sqrtf(s);
  float m0[16], m1[16];
  memcpy(m0 + 8, k, sizeof k);
  for (int c = 0; c < 4; c++) m0[4 + c] = j[c] * t;
  for (int c = 0; c < 3; c++) m0[4 + c] += i[c] * s;
  wz3__v_unit3(m0 + 4);
  wz3__v_cross4(m0, m0 + 4, m0 + 8);
  m0[12] = m0[13] = m0[14] = 0.0f;
  m0[15] = 1.0f;
  if (a <= t && !(flags & 1)) {
    memcpy(m1, m0, sizeof m1);
    for (int c = 0; c < 3; c++) m1[12 + c] += j[c] * (-a - b);
  } else {
    memcpy(m1 + 8, k, sizeof k);
    for (int c = 0; c < 4; c++) m1[4 + c] = j[c] * (len - t);
    for (int c = 0; c < 3; c++) m1[4 + c] += i[c] * -s;
    wz3__v_unit3(m1 + 4);
    wz3__v_cross4(m1, m1 + 4, m1 + 8);
    m1[12] = pos[0];
    m1[13] = pos[1];
    m1[14] = pos[2];
    m1[15] = 1.0f;
  }
  for (uint32_t pass = 0; pass < 2; pass++) {
    const float *own = pass ? m1 : m0, *other = pass ? m0 : m1;
    wz3_result r;
    if (flags & 2) {
      r = wz3__rt_stack_push_mul(env, own);
    } else {
      r = wz3__fj_stack_push(env, own);
      if (r == WZ3_OK) {
        float *top = wz3__fj_top(env);
        top[12] += matrix[12];
        top[13] += matrix[13];
        top[14] += matrix[14];
      }
    }
    if (r != WZ3_OK) return r;
    float saved[16];
    for (int row = 0; row < 4; row++) {
      memcpy(saved + row * 4, env->vars[WZ3__RT_KV_MATRIX_I + row], 4 * sizeof(float));
      memcpy(env->vars[WZ3__RT_KV_MATRIX_I + row], other + row * 4, 4 * sizeof(float));
    }
    r = wz3__fj_exec_scene_input(rt, op, pass);
    for (int row = 0; row < 4; row++) memcpy(env->vars[WZ3__RT_KV_MATRIX_I + row], saved + row * 4, 4 * sizeof(float));
    wz3__rt_stack_pop(env);
    if (r != WZ3_OK) return r;
  }
  return WZ3_OK;
}

static wz3_result wz3__fj_exec_rotate(wz3_rt *rt, wz3__rt_op *op)
{
  wz3__rt_env *env = &rt->env;
  float matrix[16], mat[16];
  float v[4] = { (float)wz3__fj_or0(wz3__fj_p(op, 0)), (float)wz3__fj_or0(wz3__fj_p(op, 1)), (float)wz3__fj_or0(wz3__fj_p(op, 2)), 0.0f };
  wz3__v_unit_safe3(v);
  memcpy(matrix, wz3__fj_top(env), sizeof matrix);
  memset(mat, 0, sizeof mat);
  switch (wz3__fj_pi(op, 3)) {
  case 0:
    memcpy(mat, v, sizeof v);
    wz3__v_cross4(mat + 8, mat, matrix + 4);
    wz3__v_unit3(mat + 8);
    wz3__v_cross4(mat + 4, mat + 8, mat);
    wz3__v_unit3(mat + 4);
    break;
  case 1:
    memcpy(mat + 4, v, sizeof v);
    wz3__v_cross4(mat, mat + 4, matrix + 8);
    wz3__v_unit3(mat);
    wz3__v_cross4(mat + 8, mat, mat + 4);
    wz3__v_unit3(mat + 8);
    break;
  case 2:
    memcpy(mat + 8, v, sizeof v);
    wz3__v_cross4(mat + 4, mat + 8, matrix);
    wz3__v_unit3(mat + 4);
    wz3__v_cross4(mat, mat + 4, mat + 8);
    wz3__v_unit3(mat);
    break;
  default: memcpy(mat, rt->fj.current_camera.camera_space, sizeof mat); break;
  }
  memcpy(mat + 12, matrix + 12, 4 * sizeof(float));
  wz3_result r = wz3__fj_stack_push(env, mat);
  if (r != WZ3_OK) return r;
  r = wz3__rt_exec_inputs(rt, op);
  wz3__rt_stack_pop(env);
  return r;
}

/* WalkMem: the plain-old-data instance memory of a Walk operator */
typedef struct wz3__fj_walk {
  float foot_old[8][4], foot_new[8][4], foot_delta[8][4];
  float old_pos[4], new_pos[4], foot_center[4], deviation[4];
  int32_t step_time[8];
  int32_t last_time, state, last_leg, next_leg, idle, deviation_flag, init_steps;
} wz3__fj_walk;

static float wz3__range(float x, float max, float min) { return x > max ? max : x < min ? min : x; }

static wz3_result wz3__fj_exec_walk(wz3_rt *rt, wz3__rt_op *op)
{
  wz3__rt_env *env = &rt->env;
  const uint32_t flags = wz3__fj_pu(op, 0);
  int32_t foot_count = wz3__fj_pi(op, 1);
  foot_count = foot_count < 1 ? 1 : foot_count > 8 ? 8 : foot_count;
  const float step_tresh = (float)wz3__fj_or0(wz3__fj_p(op, 2)), run_low = (float)wz3__fj_or0(wz3__fj_p(op, 3)),
              run_high = (float)wz3__fj_or0(wz3__fj_p(op, 4));
  int32_t foot_group[2] = { wz3__fj_pi(op, 5), wz3__fj_pi(op, 6) };
  for (int s = 0; s < 2; s++) foot_group[s] = foot_group[s] < 1 ? 1 : foot_group[s];
  const float step_length[2] = { (float)wz3__fj_or0(wz3__fj_p(op, 7)), (float)wz3__fj_or0(wz3__fj_p(op, 8)) };
  int32_t step_speed[2], step_next[2];
  step_speed[0] = wz3_ftol((double)((float)wz3__fj_or0(wz3__fj_p(op, 9)) * 1000.0f));
  step_speed[1] = wz3_ftol((double)((float)wz3__fj_or0(wz3__fj_p(op, 10)) * 1000.0f));
  step_next[0] = wz3_ftol((double)((float)wz3__fj_or0(wz3__fj_p(op, 11)) * 1000.0f));
  step_next[1] = wz3_ftol((double)((float)wz3__fj_or0(wz3__fj_p(op, 12)) * 1000.0f));
  for (int s = 0; s < 2; s++) {
    if (step_next[s] > step_speed[s]) step_next[s] = step_speed[s];
    if (step_speed[s] < 1) step_speed[s] = 1;
    if (step_next[s] < 1) step_next[s] = 1;
  }

  float matrix[16];
  memcpy(matrix, wz3__fj_top(env), sizeof matrix);
  wz3__rt_instance *inst;
  wz3_result r = wz3__rt_chain_instance(env, op->id, &inst);
  if (r != WZ3_OK) return r;
  if (!inst->blob || inst->blob_size != sizeof(wz3__fj_walk)) {
    void *blob = WZ3_MALLOC(sizeof(wz3__fj_walk));
    if (!blob) return WZ3_ERR_OUT_OF_MEMORY;
    WZ3_FREE(inst->blob);
    memset(blob, 0, sizeof(wz3__fj_walk));
    inst->blob = blob;
    inst->blob_size = sizeof(wz3__fj_walk);
    inst->reset = 1;
  }
  wz3__fj_walk *mem = (wz3__fj_walk *)inst->blob;
  float sum[4], foot[8][4], spline[8][4], v[4], dir[16], mat[16];
  if (inst->reset || (flags & 0x100) || env->time_reset) {
    memset(sum, 0, sizeof sum);
    memset(mem->foot_center, 0, sizeof mem->foot_center);
    mem->init_steps = foot_count * 2;
    for (int32_t i = 0; i < foot_count; i++) {
      mem->foot_delta[i][0] = (float)wz3__fj_or0(wz3__fj_p(op, (uint32_t)(13 + 3 * i)));
      mem->foot_delta[i][1] = (float)wz3__fj_or0(wz3__fj_p(op, (uint32_t)(14 + 3 * i)));
      mem->foot_delta[i][2] = (float)wz3__fj_or0(wz3__fj_p(op, (uint32_t)(15 + 3 * i)));
      mem->foot_delta[i][3] = 0.0f;
      for (int c = 0; c < 3; c++) v[c] = matrix[12 + c] + mem->foot_delta[i][c];
      v[3] = 0.0f;
      memcpy(mem->foot_old[i], v, sizeof v);
      memcpy(mem->foot_new[i], v, sizeof v);
      mem->step_time[i] = step_speed[0];
      mem->foot_center[0] += mem->foot_delta[i][0] / (float)foot_count;
      mem->foot_center[2] += mem->foot_delta[i][2] / (float)foot_count;
    }
    mem->last_time = env->current_time;
    memcpy(mem->new_pos, matrix + 12, 4 * sizeof(float));
    mem->state = 0;
    mem->last_leg = 0;
    for (int32_t i = 0; i < foot_count; i++)
      for (int c = 0; c < 3; c++) mem->foot_delta[i][c] -= mem->foot_center[c];
  }
  int32_t state = mem->state, last_leg = mem->last_leg, next_leg = mem->next_leg;

  if (flags & 0x30) { /* fake animation */
    state = ((flags & 0x30) == 0x20);
    const int32_t j = foot_group[state];
    float f = (step_tresh + run_low) / 2.0f;
    f = (run_low + run_high) / 2.0f;
    f = f / (float)j;
    if (flags & 0x40) f *= 2.0f;
    const int32_t time = env->current_time % (step_next[state] * j);
    const int32_t k = env->current_time / (step_next[state] * j);
    for (int32_t i = 0; i < foot_count; i++) {
      mem->step_time[i] = time - step_next[state] * (i % j);
      mem->foot_old[i][0] = 0.0f;
      mem->foot_old[i][1] = 0.0f;
      mem->foot_old[i][2] =
        (flags & 0x80) ? f * ((float)(i % j) - ((float)time * 1.0f / (float)step_next[state]) - (float)(j - 1) * 0.5f) : f * (float)(k * j + (i % j));
      mem->foot_old[i][3] = 0.0f;
      for (int c = 0; c < 3; c++) mem->foot_old[i][c] += mem->foot_delta[i][c];
      memcpy(mem->foot_new[i], mem->foot_old[i], sizeof mem->foot_new[i]);
      mem->foot_new[i][2] += f * (float)j;
      if (mem->step_time[i] < step_speed[state] - step_next[state] * j) {
        mem->step_time[i] += step_next[state] * j;
        mem->foot_old[i][2] -= f * (float)j;
        mem->foot_new[i][2] -= f * (float)j;
      }
      if (mem->step_time[i] < 0) {
        memcpy(mem->foot_new[i], mem->foot_old[i], sizeof mem->foot_new[i]);
        mem->step_time[i] = step_speed[state];
      }
      if (mem->step_time[i] > step_speed[state]) mem->step_time[i] = step_speed[state];
    }
  }

  /* animate */
  memset(sum, 0, sizeof sum);
  float f = 0.0f;
  for (int32_t i = 0; i < foot_count; i++) {
    const float t = wz3__range(1.0f * (float)mem->step_time[i] / (float)step_speed[state], 1.0f, 0.0f);
    if (i < foot_group[state]) f += t;
    float sine, cosine;
    wz3_sincos(t * 3.14159265358979323846f, &sine, &cosine);
    spline[i][0] = 0.0f;
    spline[i][1] = sine * 0.25f;
    spline[i][2] = 0.0f;
    spline[i][3] = t;
    for (int c = 0; c < 3; c++) foot[i][c] = mem->foot_old[i][c] + (mem->foot_new[i][c] - mem->foot_old[i][c]) * spline[i][3];
    foot[i][3] = 1.0f;
    for (int c = 0; c < 3; c++) sum[c] += foot[i][c];
  }
  for (int c = 0; c < 3; c++) sum[c] *= 1.0f / (float)foot_count;
  memset(mat, 0, sizeof mat);
  mat[12] = sum[0];
  mat[13] = sum[1];
  mat[14] = sum[2];
  mat[15] = 1.0f;
  for (int32_t i = 0; i < foot_count; i++) {
    for (int c = 0; c < 3; c++) v[c] = foot[i][c] - sum[c];
    for (int c = 0; c < 3; c++) {
      mat[c] += v[c] * mem->foot_delta[i][0];
      mat[4 + c] += v[c] * mem->foot_delta[i][1];
      mat[8 + c] += v[c] * mem->foot_delta[i][2];
    }
  }
  switch ((flags >> 2) & 3) {
  case 0: memcpy(mat, matrix, 12 * sizeof(float)); break;
  case 2:
    wz3__v_unit3(mat);
    mat[4] = 0.0f;
    mat[5] = 1.0f;
    mat[6] = 0.0f;
    mat[7] = 0.0f;
    wz3__v_cross4(mat + 8, mat, mat + 4);
    wz3__v_unit3(mat + 8);
    wz3__v_cross4(mat + 4, mat + 8, mat);
    wz3__v_unit3(mat + 4);
    break;
  case 1:
    memcpy(mat + 8, matrix + 8, 4 * sizeof(float));
    /* fall through */
  case 3:
    wz3__v_unit3(mat + 8);
    wz3__v_cross4(mat + 4, mat + 8, mat);
    wz3__v_unit3(mat + 4);
    wz3__v_cross4(mat, mat + 4, mat + 8);
    wz3__v_unit3(mat);
    break;
  }

  float t = ((f + (float)last_leg) / (float)foot_group[state]);
  if (t >= 2.0f) t -= 2.0f;
  if (t >= 1.0f) t -= 1.0f;
  t = (t + (float)state) * 0.5f;
  float save_time[4];
  memcpy(save_time, env->vars[WZ3__FJ_KV_LEG_TIMES], sizeof save_time);
  env->vars[WZ3__FJ_KV_LEG_TIMES][0] = t;
  env->vars[WZ3__FJ_KV_LEG_TIMES][1] = env->vars[WZ3__FJ_KV_LEG_TIMES][2] = env->vars[WZ3__FJ_KV_LEG_TIMES][3] = 0.0f;

  /* advance */
  int32_t time = env->current_time - mem->last_time;
  mem->last_time = env->current_time;
  if (time < 0) time = 0;
  memcpy(dir, matrix, sizeof dir);
  if (mem->deviation_flag > 0)
    for (int c = 0; c < 3; c++) dir[12 + c] = mat[12 + c] + mem->deviation[c];
  dir[9] = 0.0f; /* dir.k.y = 0 */
  wz3__v_unit3(dir + 8);
  dir[4] = 0.0f;
  dir[5] = 1.0f;
  dir[6] = 0.0f;
  dir[7] = 0.0f;
  wz3__v_cross4(dir, dir + 4, dir + 8);
  for (int32_t i = 0; i < foot_count; i++)
    if (mem->step_time[i] < step_speed[state]) mem->step_time[i] += time;

  /* find distance */
  float dist = 0.0f;
  float save_var[8][4];
  next_leg = (last_leg + 1) % foot_group[state];
  int32_t best_leg = next_leg;
  for (int32_t i = 0; i < foot_count; i++) {
    wz3__v_rotate34(v, dir, mem->foot_delta[i]);
    for (int c = 0; c < 3; c++) v[c] -= foot[i][c];
    v[1] = 0.0f;
    const float len = sqrtf(wz3__v_dot3(v));
    if (len > dist && mem->step_time[i] >= step_speed[state]) {
      dist = len;
      best_leg = i;
    }
    memcpy(save_var[i], env->vars[WZ3__FJ_KV_LEG_L0 + i], sizeof save_var[i]);
    wz3__v_rotate3(v, dir, spline[i]);
    for (int c = 0; c < 3; c++) env->vars[WZ3__FJ_KV_LEG_L0 + i][c] = foot[i][c] + v[c];
    env->vars[WZ3__FJ_KV_LEG_L0 + i][3] = spline[i][3];
  }

  /* change walking/running */
  int32_t change_state = 1;
  for (int32_t i = 0; i < foot_count; i++)
    if (mem->step_time[i] < step_speed[state]) change_state = 0;
  if ((state == 0 && dist > run_high) || (state == 1 && dist < run_low)) change_state += 2;
  if (change_state == 3) {
    if ((state == 0 && dist > run_high) || (state == 1 && dist < run_low)) {
      state = 1 - state;
      change_state = 0;
      last_leg = last_leg % foot_group[state];
      for (int32_t i = 0; i < foot_count; i++) mem->step_time[i] = step_speed[state];
      next_leg = best_leg % foot_group[state];
    }
  }
  best_leg = best_leg % foot_group[state];
  if (mem->idle && change_state == 1) next_leg = best_leg;

  /* do the next step */
  mem->idle = 0;
  if (change_state != 2 && mem->step_time[last_leg] >= step_next[state] && mem->step_time[next_leg] >= step_speed[state]) {
    if (mem->init_steps > 0) {
      mem->init_steps--;
      dist = 1024.0f;
    }
    if (mem->deviation_flag > 0) mem->deviation_flag--;
    if (dist > step_tresh) {
      for (int c = 0; c < 3; c++) v[c] = dir[12 + c] - mat[12 + c];
      const float len = sqrtf(wz3__v_dot3(v));
      if ((flags & 1) && mem->init_steps == 0) next_leg = best_leg;
      if (len > step_length[state]) {
        for (int c = 0; c < 3; c++) mem->new_pos[c] = mat[12 + c] + v[c] * (step_length[state] / len);
      } else {
        for (int c = 0; c < 3; c++) mem->new_pos[c] = dir[12 + c];
      }
      for (int c = 0; c < 3; c++) dir[12 + c] = mem->new_pos[c];
      const int32_t j = mem->step_time[last_leg] - step_next[state];
      for (int32_t i = next_leg; i < foot_count; i += foot_group[state]) {
        memcpy(mem->foot_old[i], foot[i], sizeof mem->foot_old[i]);
        float d[4] = { mem->foot_delta[i][0], 0.0f, mem->foot_delta[i][2], mem->foot_delta[i][3] };
        wz3__v_rotate34(mem->foot_new[i], dir, d);
        mem->step_time[i] = j;
      }
      last_leg = next_leg;
    } else {
      mem->idle = 1;
    }
  }

  r = wz3__fj_stack_push(env, mat);
  if (r == WZ3_OK) {
    r = wz3__rt_exec_inputs(rt, op);
    wz3__rt_stack_pop(env);
  }
  mem->state = state;
  mem->last_leg = last_leg;
  mem->next_leg = next_leg;
  for (int32_t i = 0; i < foot_count; i++) memcpy(env->vars[WZ3__FJ_KV_LEG_L0 + i], save_var[i], sizeof save_var[i]);
  memcpy(env->vars[WZ3__FJ_KV_LEG_TIMES], save_time, sizeof save_time);
  return r;
}

static wz3_result wz3__fj_exec_light(wz3_rt *rt, wz3__rt_op *op)
{
  float srt[9] = { 1, 1, 1, 0, 0, 0, 0, 0, 0 }, matrix[16];
  for (uint32_t i = 0; i < 6; i++) srt[3 + i] = (float)wz3__fj_or0(wz3__fj_p(op, i));
  wz3_mat4_srt(srt, matrix);
  wz3__rt_env *env = &rt->env;
  wz3_result r = wz3__rt_stack_push_mul(env, matrix);
  if (r != WZ3_OK) return r;
  const float *top = wz3__fj_top(env);
  const uint32_t flags = wz3__fj_pu(op, 6);
  const int directional = (flags & 4u) != 0;
  const double range = wz3__fj_or0(wz3__fj_p(op, 10));
  wz3_light_job *job;
  r = wz3__fj_push(&rt->fj.light, sizeof *job, 1, (void **)&job);
  if (r == WZ3_OK) {
    job->directional = (uint8_t)directional;
    job->op = op->id;
    for (int k = 0; k < 3; k++) {
      job->direction[k] = top[8 + k];
      job->position[k] = (directional && range == 0.0) ? (float)((double)top[8 + k] * 1e6) : top[12 + k];
    }
    job->flags = flags;
    job->color = wz3__fj_pu(op, 8);
    job->amplify = (float)wz3__fj_or0(wz3__fj_p(op, 9));
    job->range = range != 0.0 ? (double)(float)range : 1e15;
    job->event = env->current_event;
  }
  wz3__rt_stack_pop(env);
  return r;
}

static wz3_result wz3__fj_exec_marker(wz3_rt *rt, wz3__rt_op *op)
{
  const int32_t marker = wz3__fj_pi(op, 0);
  if (marker < 0 || marker >= 32) return WZ3_ERR_OPERATOR; /* RangeError */
  wz3_result r = wz3__fj_ensure_table(rt);
  if (r != WZ3_OK) return r;
  memcpy(rt->fj.markers[marker], wz3__fj_top(&rt->env), 16 * sizeof(float));
  memcpy((float *)rt->fj.marker_tables.p + (size_t)rt->fj.cur_table * 512 + (size_t)marker * 16, rt->fj.markers[marker], 16 * sizeof(float));
  return wz3__fj_exec_scene_inputs(rt, op);
}

static void wz3__fj_add_ambient(wz3__fj *fj, uint32_t color)
{
  const uint32_t old = fj->ambient;
  uint32_t r = ((old >> 16) & 255u) + ((color >> 16) & 255u);
  uint32_t g = ((old >> 8) & 255u) + ((color >> 8) & 255u);
  uint32_t b = (old & 255u) + (color & 255u);
  if (r > 255) r = 255;
  if (g > 255) g = 255;
  if (b > 255) b = 255;
  fj->ambient = (r << 16) | (g << 8) | b;
}

/* ---- Particles ---- */

typedef struct wz3__fj_pmem {
  int32_t mode, count, seed;
  float rand[3], rand_rot[3], rand_forw, rand_rot_speed[3], come_out;
} wz3__fj_pmem;

static size_t wz3__fj_pmem_size(int32_t mode, int32_t count)
{
  return sizeof(wz3__fj_pmem) + ((size_t)count * 8 + ((mode & 0x10) ? (size_t)count * 6 : 0)) * sizeof(float);
}
static float *wz3__fj_pmem_pos(wz3__fj_pmem *m) { return (float *)(m + 1); }
static float *wz3__fj_pmem_speed(wz3__fj_pmem *m) { return wz3__fj_pmem_pos(m) + (size_t)m->count * 4; }
static float *wz3__fj_pmem_rot(wz3__fj_pmem *m) { return wz3__fj_pmem_speed(m) + (size_t)m->count * 4; }
static float *wz3__fj_pmem_rot_speed(wz3__fj_pmem *m) { return wz3__fj_pmem_rot(m) + (size_t)m->count * 3; }

static float wz3__fj_rndf(wz3_rng *r, double max) { return (float)(((double)(wz3_rng_u32(r) & 0x3fffffffu) * max) / 1073741824.0); }

/* particleMemoryChanged */
static int wz3__fj_particles_changed(const wz3__fj_pmem *m, const wz3__rt_op *op)
{
  const int32_t mode = wz3__fj_pi(op, 0), count = wz3__fj_pi(op, 1) < 0 ? 0 : wz3__fj_pi(op, 1);
  return m->mode != mode || m->count != count || m->seed != wz3__fj_pi(op, 2) || m->rand[0] != (float)wz3__fj_p(op, 3) ||
         m->rand[1] != (float)wz3__fj_p(op, 4) || m->rand[2] != (float)wz3__fj_p(op, 5) || m->rand_rot[0] != (float)wz3__fj_p(op, 6) ||
         m->rand_rot[1] != (float)wz3__fj_p(op, 7) || m->rand_rot[2] != (float)wz3__fj_p(op, 8) || m->rand_forw != (float)wz3__fj_or0(wz3__fj_p(op, 22)) ||
         m->come_out != (float)wz3__fj_or0(wz3__fj_p(op, 23)) || m->rand_rot_speed[0] != (float)wz3__fj_p(op, 24) ||
         m->rand_rot_speed[1] != (float)wz3__fj_p(op, 25) || m->rand_rot_speed[2] != (float)wz3__fj_p(op, 26);
}

/* initParticleMemory: the memory block is replaced when the particle count or mode changed its size */
static wz3_result wz3__fj_particles_init(wz3__rt_instance *inst, const wz3__rt_op *op, wz3__fj_pmem **out)
{
  const int32_t mode = wz3__fj_pi(op, 0);
  const int32_t count = wz3__fj_pi(op, 1) < 0 ? 0 : wz3__fj_pi(op, 1);
  if (count > (1 << 24)) return WZ3_ERR_LIMIT;
  const size_t size = wz3__fj_pmem_size(mode, count);
  if (!inst->blob || inst->blob_size != size) {
    void *blob = WZ3_MALLOC(size);
    if (!blob) return WZ3_ERR_OUT_OF_MEMORY;
    WZ3_FREE(inst->blob);
    inst->blob = blob;
    inst->blob_size = (uint32_t)size;
  }
  wz3__fj_pmem *m = (wz3__fj_pmem *)inst->blob;
  memset(m, 0, size);
  m->mode = mode;
  m->count = count;
  m->seed = wz3__fj_pi(op, 2);
  for (int k = 0; k < 3; k++) {
    m->rand[k] = (float)wz3__fj_p(op, 3 + k);
    m->rand_rot[k] = (float)wz3__fj_p(op, 6 + k);
    m->rand_rot_speed[k] = (float)wz3__fj_p(op, 24 + k);
  }
  m->rand_forw = (float)wz3__fj_or0(wz3__fj_p(op, 22));
  m->come_out = (float)wz3__fj_or0(wz3__fj_p(op, 23));
  float *pos_all = wz3__fj_pmem_pos(m), *speed_all = wz3__fj_pmem_speed(m);
  float *rot_all = wz3__fj_pmem_rot(m), *rot_speed_all = wz3__fj_pmem_rot_speed(m);
  wz3_rng random;
  wz3_rng_init(&random);
  wz3_rng_seed(&random, m->seed);
  for (int32_t i = 0; i < count; i++) {
    float pos[4], speed[4];
    do {
      pos[0] = (float)((double)wz3__fj_rndf(&random, 2) - 1);
      pos[1] = (float)((double)wz3__fj_rndf(&random, 2) - 1);
      pos[2] = (float)((double)wz3__fj_rndf(&random, 2) - 1);
      if ((mode & 12) == 4) {
        const double v[3] = { pos[0], pos[1], pos[2] };
        wz3__fj_normalize(v, pos);
      }
    } while ((mode & 12) == 8 && (double)pos[0] * pos[0] + (double)pos[1] * pos[1] + (double)pos[2] * pos[2] > 1.0);
    pos[3] = (mode & 0x20) ? (float)((double)i / (double)count) : (float)((double)wz3__fj_rndf(&random, 1.0 - (double)m->come_out) + (double)m->come_out);
    pos[0] = (float)((double)pos[0] * m->rand[0] * 0.5);
    pos[1] = (float)((double)pos[1] * m->rand[1] * 0.5);
    pos[2] = (float)((double)pos[2] * m->rand[2] * 0.5);
    memcpy(pos_all + (size_t)i * 4, pos, sizeof pos);
    do {
      speed[0] = (float)((double)wz3__fj_rndf(&random, 2) - 1);
      speed[1] = (float)((double)wz3__fj_rndf(&random, 2) - 1);
      speed[2] = (float)((double)wz3__fj_rndf(&random, 2) - 1);
    } while ((double)speed[0] * speed[0] + (double)speed[1] * speed[1] + (double)speed[2] * speed[2] > 1.0);
    speed[3] = wz3__fj_rndf(&random, 1.0);
    memcpy(speed_all + (size_t)i * 4, speed, sizeof speed);
    if (mode & 0x10) {
      float *rot = rot_all + (size_t)i * 3, *rs = rot_speed_all + (size_t)i * 3;
      rot[0] = wz3__fj_rndf(&random, m->rand_rot[0]);
      rot[1] = wz3__fj_rndf(&random, m->rand_rot[1]);
      rot[2] = wz3__fj_rndf(&random, m->rand_rot[2]);
      rs[0] = wz3__fj_rndf(&random, m->rand_rot_speed[0]);
      rs[1] = wz3__fj_rndf(&random, m->rand_rot_speed[1]);
      rs[2] = wz3__fj_rndf(&random, m->rand_rot_speed[2]);
    }
  }
  *out = m;
  return WZ3_OK;
}

/* evalGeneratedSpline: the matrix a generated (blob or shaker) spline gives at `time`, or 0 when it is none */
static int wz3__fj_eval_generated(const wz3_rt *rt, const wz3__rt_cache *spline, double time, float matrix[16])
{
  if (!wz3__rt_is_spline_cache(spline)) return 0;
  float zoom;
  wz3__rt_spline_eval(rt, spline, time, matrix, &zoom);
  return 1;
}

static void wz3__fj_eval_value_spline(const wz3_rt *rt, int32_t spline, double time, float out[4]) { wz3__rt_dspline_eval(&rt->splines[spline], time, out); }

static wz3_result wz3__fj_exec_particles(wz3_rt *rt, wz3__rt_op *op)
{
  wz3__rt_env *env = &rt->env;
  wz3_result r;
  wz3__rt_instance *inst;
  if ((r = wz3__rt_chain_instance(env, op->id, &inst)) != WZ3_OK) return r;
  wz3__fj_pmem *mem = (wz3__fj_pmem *)inst->blob;
  if (inst->reset || !mem || wz3__fj_particles_changed(mem, op))
    if ((r = wz3__fj_particles_init(inst, op, &mem)) != WZ3_OK) return r;
  const int32_t mode = mem->mode, count = mem->count;
  double anim = wz3__fj_or0(wz3__fj_p(op, 12));
  if (!(mode & 0x80)) anim = fmod(anim, 1.0);
  if (anim < 0) anim += 1;

  const int32_t doc_spline = op->src->n_splines ? op->src->splines[0] : -1;
  const wz3__rt_cache *generated = op->src->n_inputs >= 2 || op->input_slots >= 2 ? wz3__rt_input_cache(rt, op, 1) : NULL;
  const int instanced = (mode & 0x100) != 0;
  const uint32_t first_instance = rt->fj.instance.n;
  uint32_t n_instances = 0;
  float saved_select[4], saved_fraction[4];
  memcpy(saved_select, env->vars[WZ3__FJ_KV_SELECT], sizeof saved_select);
  memcpy(saved_fraction, env->vars[WZ3__FJ_KV_FRACTION], sizeof saved_fraction);
  const double random_speed = wz3__fj_p(op, 16);
  const double gravity[3] = { wz3__fj_p(op, 19), wz3__fj_p(op, 20), wz3__fj_p(op, 21) };
  const double line[3] = { wz3__fj_p(op, 13), wz3__fj_p(op, 14), wz3__fj_p(op, 15) };
  const double pulse_rate = wz3__fj_p(op, 27), pulse_phase = wz3__fj_p(op, 28), pulse_amount = wz3__fj_p(op, 29);
  const double rotation_rate[3] = { wz3__fj_p(op, 9), wz3__fj_p(op, 10), wz3__fj_p(op, 11) };
  float *pos_all = wz3__fj_pmem_pos(mem), *speed_all = wz3__fj_pmem_speed(mem);
  float *rot_all = (mode & 0x10) ? wz3__fj_pmem_rot(mem) : NULL, *rot_speed_all = (mode & 0x10) ? wz3__fj_pmem_rot_speed(mem) : NULL;

  for (int32_t i = 0; i < count && r == WZ3_OK; i++) {
    const float *pos = pos_all + (size_t)i * 4, *speed = speed_all + (size_t)i * 4;
    double fraction = anim + (double)pos[3] + (double)mem->rand_forw * (double)speed[3] * anim;
    if (fraction < 1 && (mode & 64)) continue;
    while (fraction >= 1 && fraction < 9007199254740992.0) fraction -= 1;
    float matrix[16], position[3];
    wz3_mat4_identity(matrix);
    const double g = fraction * fraction;
    for (int k = 0; k < 3; k++) position[k] = (float)((double)pos[k] + (double)speed[k] * fraction * random_speed + g * gravity[k]);
    if (pulse_amount != 0.0 && pulse_amount == pulse_amount) {
      const double scale = wz3__sin(anim * 3.141592653589793 * 2 * pulse_rate + fraction * 3.141592653589793 * 2 * pulse_phase) * pulse_amount + 1;
      for (int k = 0; k < 3; k++) position[k] = (float)((double)position[k] * scale);
    }
    if ((mode & 1) && doc_spline >= 0) {
      float value[4], ahead[4];
      wz3__fj_eval_value_spline(rt, doc_spline, fraction, value);
      if (mode & 0x10) {
        wz3__fj_eval_value_spline(rt, doc_spline, fraction + 0.01, ahead);
        const double direction[3] = { (double)ahead[0] - value[0], (double)ahead[1] - value[1], (double)ahead[2] - value[2] };
        wz3__rt_init_direction(direction, matrix);
      }
      for (int k = 0; k < 3; k++) matrix[12 + k] = (float)((double)matrix[12 + k] + ((double)position[k] + (double)value[k]));
    }
    if (wz3__fj_eval_generated(rt, generated, fraction, matrix)) {
      const double x = (double)matrix[0] * position[0] + (double)matrix[4] * position[1] + (double)matrix[8] * position[2];
      const double y = (double)matrix[1] * position[0] + (double)matrix[5] * position[1] + (double)matrix[9] * position[2];
      const double z = (double)matrix[2] * position[0] + (double)matrix[6] * position[1] + (double)matrix[10] * position[2];
      matrix[12] = (float)((double)matrix[12] + x);
      matrix[13] = (float)((double)matrix[13] + y);
      matrix[14] = (float)((double)matrix[14] + z);
    } else {
      for (int k = 0; k < 3; k++) matrix[12 + k] = (float)((double)matrix[12 + k] + ((double)position[k] + line[k] * fraction));
    }
    if (rot_all) {
      double angle[3];
      for (int k = 0; k < 3; k++) {
        angle[k] = fraction * rotation_rate[k];
        angle[k] += (double)rot_all[(size_t)i * 3 + k];
        angle[k] += (double)rot_speed_all[(size_t)i * 3 + k] * fraction;
      }
      float rotation[16], composed[16];
      wz3_mat4_euler((float)angle[0], (float)angle[1], (float)angle[2], rotation);
      wz3_mat4_mul_a(rotation, matrix, composed);
      memcpy(matrix, composed, sizeof matrix);
    }
    if (instanced) {
      float *dest;
      r = wz3__fj_push(&rt->fj.instance, 16 * sizeof(float), 1, (void **)&dest);
      if (r == WZ3_OK) {
        memcpy(dest, matrix, sizeof matrix);
        n_instances++;
      }
    } else {
      for (int k = 0; k < 4; k++) {
        env->vars[WZ3__FJ_KV_SELECT][k] = (float)i;
        env->vars[WZ3__FJ_KV_FRACTION][k] = (float)fraction;
      }
      r = wz3__rt_stack_push_mul(env, matrix);
      if (r == WZ3_OK) {
        r = wz3__fj_exec_scene_input(rt, op, 0);
        wz3__rt_stack_pop(env);
      }
    }
  }
  if (r != WZ3_OK) return r;
  memcpy(env->vars[WZ3__FJ_KV_SELECT], saved_select, sizeof saved_select);
  memcpy(env->vars[WZ3__FJ_KV_FRACTION], saved_fraction, sizeof saved_fraction);
  if (instanced) {
    const wz3__fj_scene *scene = op->cache.kind == WZ3__RC_SCENE ? (const wz3__fj_scene *)op->cache.obj : NULL;
    int32_t mesh_op = -1;
    if (scene && scene->n_children && scene->child_mesh[0] >= 0)
      mesh_op = scene->child_mesh[0];
    else if (wz3__rt_input_cache(rt, op, 0))
      mesh_op = (int32_t)op->src->inputs[0];
    if (mesh_op >= 0) {
      /* call.inputs[0] is whatever the input made; only a mesh or minmesh is geometry (a Scene input draws nothing) */
      const int kind = wz3__fj_cache_kind(&rt->ops[mesh_op].cache);
      r = wz3__fj_add_mesh_job(rt, (kind == WZ3__FJ_K_MESH || kind == WZ3__FJ_K_MINMESH) ? mesh_op : -1, op->src->n_inputs ? (int32_t)op->src->inputs[0] : -1,
                               1, first_instance, n_instances, 0.0f, 0);
    }
  }
  return r;
}

/* ---- IPP: Viewport and Layer2D ---- */

static void wz3__fj_cross(const double a[3], const double b[3], float out[3])
{
  out[0] = (float)(a[1] * b[2] - a[2] * b[1]);
  out[1] = (float)(a[2] * b[0] - a[0] * b[2]);
  out[2] = (float)(a[0] * b[1] - a[1] * b[0]);
}

/* buildCamera */
static void wz3__fj_build_camera(wz3_rt *rt, const wz3__rt_op *op, wz3_camera *camera)
{
  const uint32_t flags = wz3__fj_pu(op, 1);
  wz3__fj_camera_defaults(camera);
  camera->far_clip = wz3__fj_p(op, 9);
  camera->near_clip = wz3__fj_p(op, 10);
  camera->center_x = wz3__fj_p(op, 11);
  camera->center_y = wz3__fj_p(op, 12);
  camera->zoom_x = wz3__fj_p(op, 13);
  camera->zoom_y = wz3__fj_p(op, 14);
  const double rotation[3] = { wz3__fj_p(op, 3), wz3__fj_p(op, 4), wz3__fj_p(op, 5) };
  const double position[3] = { wz3__fj_p(op, 6), wz3__fj_p(op, 7), wz3__fj_p(op, 8) };
  const wz3_camera *game = &rt->fj.game_camera;
  float *m = camera->camera_space;
  if (flags & 4u) {
    memcpy(m, game->camera_space, 16 * sizeof(float));
    if ((flags & 0x300u) == 0) {
      camera->center_x = game->center_x;
      camera->center_y = game->center_y;
      camera->zoom_x = game->zoom_x;
      camera->zoom_y = game->zoom_y;
      camera->far_clip = game->far_clip;
      camera->near_clip = game->near_clip;
    }
  } else {
    wz3__rt_euler_turns(rotation[0], rotation[1], rotation[2], m);
    m[12] = (float)position[0];
    m[13] = (float)position[1];
    m[14] = (float)position[2];
  }
  if ((flags & 0x300u) == 0x100u) {
    const double up[3] = { 0, 1, 0 };
    const double k[3] = { m[8], m[9], m[10] };
    float i[3], next_k[3];
    double crossed[3], id[3];
    float c[3];
    wz3__fj_cross(up, k, c);
    crossed[0] = c[0];
    crossed[1] = c[1];
    crossed[2] = c[2];
    wz3__fj_normalize(crossed, i);
    id[0] = i[0];
    id[1] = i[1];
    id[2] = i[2];
    wz3__fj_cross(id, up, next_k);
    m[0] = i[0];
    m[1] = i[1];
    m[2] = i[2];
    m[4] = 0;
    m[5] = 1;
    m[6] = 0;
    m[8] = next_k[0];
    m[9] = next_k[1];
    m[10] = next_k[2];
    m[12] = (float)((double)next_k[0] * position[2]);
    m[13] = (float)((double)next_k[1] * position[2]);
    m[14] = (float)((double)next_k[2] * position[2]);
  } else if ((flags & 0x300u) == 0x200u) {
    m[12] = (float)((double)m[8] * position[2]);
    m[13] = (float)((double)m[9] * position[2]);
    m[14] = (float)((double)m[10] * position[2]);
  }
  m[15] = 1;
  camera->fog_color = wz3__fj_pu(op, 15);
  camera->fog_end = wz3__fj_p(op, 16);
  camera->fog_start = wz3__fj_p(op, 17);
  if (!(flags & 0x1000u)) camera->zoom_y *= rt->fj.aspect;
  const wz3__rt_cache *spline = wz3__rt_input_cache(rt, op, 1);
  if (wz3__rt_is_spline_cache(spline)) {
    float zoom = 1, evaluated[16];
    memset(evaluated, 0, sizeof evaluated); /* spline.eval(time, 0) allocates a fresh matrix */
    wz3__rt_spline_eval(rt, spline, wz3__fj_or0(rt->env.vars[WZ3__FJ_KV_TIME][0]), evaluated, &zoom);
    memcpy(camera->camera_space, evaluated, sizeof evaluated);
    camera->zoom_x *= zoom;
    camera->zoom_y *= zoom;
  }
}

static wz3_result wz3__fj_add_node(wz3_rt *rt, wz3_ipp_node **node)
{
  const wz3_result r = wz3__fj_push(&rt->fj.nodes, sizeof **node, 1, (void **)node);
  if (r == WZ3_OK) memset(*node, 0, sizeof **node);
  return r;
}

static wz3_result wz3__fj_exec_viewport(wz3_rt *rt, wz3__rt_op *op)
{
  wz3__fj *fj = &rt->fj;
  if (!op->src->n_inputs) return WZ3_OK;
  if (fj->ipp_node[op->id] >= 0) {
    fj->last_output = fj->ipp_node[op->id];
    return WZ3_OK;
  }
  wz3_result r = wz3__fj_begin_render_frame(rt);
  if (r != WZ3_OK) return r;
  wz3_camera camera;
  wz3__fj_build_camera(rt, op, &camera);
  fj->current_camera = camera;
  r = wz3__rt_exec_op(rt, op->src->inputs[0]);
  if (r != WZ3_OK) return r;
  wz3_light_job fallback;
  memset(&fallback, 0, sizeof fallback);
  memcpy(fallback.position, camera.camera_space + 12, 3 * sizeof(float));
  fallback.direction[2] = 1.0f;
  fallback.color = 0x78787878u;
  fallback.amplify = 1.0f;
  fallback.range = 32.0;
  fallback.event = -1;
  if (fj->light.n == fj->b_light) {
    wz3_light_job *job;
    if ((r = wz3__fj_push(&fj->light, sizeof *job, 1, (void **)&job)) != WZ3_OK) return r;
    *job = fallback;
  }
  wz3_ipp_node *node;
  if ((r = wz3__fj_add_node(rt, &node)) != WZ3_OK) return r;
  node->type = WZ3_IPP_VIEWPORT;
  node->size = wz3__fj_pi(op, 0);
  node->flags = wz3__fj_pu(op, 1);
  node->clear_color = wz3__fj_pu(op, 2);
  node->camera = camera;
  for (int k = 0; k < 4; k++) node->crop[k] = (float)wz3__fj_p(op, 20 + k);
  node->eye_distance = (float)wz3__fj_p(op, 18);
  node->focal_distance = (float)wz3__fj_p(op, 19);
  node->paint_mode = (int32_t)((wz3__fj_pu(op, 1) >> 4) & 3u);
  node->first_mesh = fj->b_mesh;
  node->n_mesh = fj->mesh.n - fj->b_mesh;
  node->first_effect = fj->b_effect;
  node->n_effect = fj->effect.n - fj->b_effect;
  node->first_light = fj->b_light;
  node->n_light = fj->light.n - fj->b_light;
  node->marker_table = (uint32_t)fj->cur_table;
  node->ambient = fj->ambient;
  node->default_light = fallback;
  node->input = -1;
  node->material_op = -1;
  fj->last_output = (int32_t)fj->nodes.n - 1;
  fj->ipp_node[op->id] = fj->last_output;
  return WZ3_OK;
}

static void wz3__fj_centered_rect(const wz3__rt_op *op, uint32_t first, int centered, double out[4])
{
  const double v[4] = { wz3__fj_p(op, first), wz3__fj_p(op, first + 1), wz3__fj_p(op, first + 2), wz3__fj_p(op, first + 3) };
  if (!centered) {
    memcpy(out, v, sizeof v);
    return;
  }
  out[0] = v[0] - v[2] * 0.5;
  out[1] = v[1] - v[3] * 0.5;
  out[2] = v[0] + v[2] * 0.5;
  out[3] = v[1] + v[3] * 0.5;
}

static wz3_result wz3__fj_exec_layer2d(wz3_rt *rt, wz3__rt_op *op)
{
  wz3__fj *fj = &rt->fj;
  if (fj->ipp_node[op->id] >= 0) {
    fj->last_output = fj->ipp_node[op->id];
    return WZ3_OK;
  }
  fj->last_output = -1;
  wz3_result r = wz3__rt_exec_inputs(rt, op);
  if (r != WZ3_OK) return r;
  const uint32_t flags = wz3__fj_pu(op, 10);
  const int32_t input = fj->last_output;
  wz3_ipp_node *node;
  if ((r = wz3__fj_add_node(rt, &node)) != WZ3_OK) return r;
  node->type = WZ3_IPP_LAYER2D;
  node->input = input;
  node->material_op = op->src->n_links ? op->src->links[0] : -1;
  node->size = input >= 0 ? ((const wz3_ipp_node *)fj->nodes.p)[input].size : wz3__fj_pi(op, 0);
  node->stages = wz3__fj_pi(op, 0); /* the size class of its own (the renderer's: node->size is the input's) */
  wz3__fj_centered_rect(op, 1, (flags & 0x10u) != 0, node->screen);
  wz3__fj_centered_rect(op, 5, (flags & 0x20u) != 0, node->uv);
  node->z = (float)wz3__fj_p(op, 9);
  node->flags = flags;
  node->clear_flags = (int32_t)(flags & 3u);
  fj->last_output = (int32_t)fj->nodes.n - 1;
  fj->ipp_node[op->id] = fj->last_output;
  return WZ3_OK;
}

/* ---- post-processing (Exec_IPP_Color ... Exec_IPP_RenderTarget): recorded as nodes, drawn by the renderer ---- */

static void wz3__fj_node_defaults(wz3_ipp_node *node) { node->input = node->input2 = node->rt_op = node->material_op = -1; }

/* The operator's inputs and links as Op.execInputs, LastOutput being whatever the last IPP input left */
static wz3_result wz3__fj_exec_post(wz3_rt *rt, wz3__rt_op *op)
{
  wz3__fj *fj = &rt->fj;
  if (fj->ipp_node[op->id] >= 0) {
    fj->last_output = fj->ipp_node[op->id];
    return WZ3_OK;
  }
  const uint32_t cls = op->class_id;
  wz3_result r = WZ3_OK;
  if (cls == 0xea) { /* Select: every input draws, the last one is the output */
    const uint32_t first = fj->selects.n;
    for (uint32_t i = 0; i < op->src->n_inputs && r == WZ3_OK; i++) {
      fj->last_output = -1;
      r = wz3__rt_exec_op(rt, op->src->inputs[i]);
      if (r == WZ3_OK && fj->last_output >= 0) {
        uint32_t *slot;
        if ((r = wz3__fj_push(&fj->selects, sizeof *slot, 1, (void **)&slot)) == WZ3_OK) *slot = (uint32_t)fj->last_output;
      }
    }
    if (r != WZ3_OK) return r;
    const uint32_t n = fj->selects.n - first;
    if (n == 0) {
      fj->last_output = -1;
      return WZ3_OK;
    }
    if (n == 1) { /* nothing to order: the input itself */
      fj->last_output = (int32_t)((const uint32_t *)fj->selects.p)[first];
      fj->selects.n = first;
      fj->ipp_node[op->id] = fj->last_output;
      return WZ3_OK;
    }
    wz3_ipp_node *node;
    if ((r = wz3__fj_add_node(rt, &node)) != WZ3_OK) return r;
    wz3__fj_node_defaults(node);
    node->type = WZ3_IPP_SELECT;
    node->first_select = first;
    node->n_select = n;
    node->size = 5;
    fj->last_output = (int32_t)fj->nodes.n - 1;
    fj->ipp_node[op->id] = fj->last_output;
    return WZ3_OK;
  }
  int32_t in[2] = { -1, -1 };
  if (cls == 0xe7) { /* Merge: two inputs, each its own LastOutput */
    for (uint32_t i = 0; i < 2; i++) {
      fj->last_output = -1;
      if (i < op->src->n_inputs && (r = wz3__rt_exec_op(rt, op->src->inputs[i])) != WZ3_OK) return r;
      in[i] = fj->last_output;
      if (in[i] < 0) {
        fj->last_output = -1;
        return WZ3_OK;
      }
    }
  } else {
    fj->last_output = -1;
    if ((r = wz3__rt_exec_inputs(rt, op)) != WZ3_OK) return r;
    in[0] = fj->last_output;
    if (in[0] < 0) return WZ3_OK; /* no input: nothing is drawn and nothing is output */
  }
  wz3_ipp_node *node;
  if ((r = wz3__fj_add_node(rt, &node)) != WZ3_OK) return r;
  wz3__fj_node_defaults(node);
  node->input = in[0];
  node->input2 = in[1];
  switch (cls) {
  case 0xe6: /* Color: size, color, operation, amplify */
    node->type = WZ3_IPP_COLOR;
    node->size = wz3__fj_pi(op, 0);
    node->color = wz3__fj_pu(op, 1);
    node->operation = wz3__fj_pi(op, 2);
    node->amplify = wz3__fj_pu(op, 3);
    break;
  case 0xe3: /* Blur: size, radius, amplify, flags, stages */
    node->type = WZ3_IPP_BLUR;
    node->size = wz3__fj_pi(op, 0);
    node->radius = (float)wz3__fj_p(op, 1);
    node->factor = (float)wz3__fj_p(op, 2);
    node->operation = wz3__fj_pi(op, 3);
    node->stages = wz3__fj_pi(op, 4);
    break;
  case 0xe7: /* Merge: size, operation, alpha, amplify */
    node->type = WZ3_IPP_MERGE;
    node->size = wz3__fj_pi(op, 0);
    node->operation = wz3__fj_pi(op, 1);
    node->color = (uint32_t)wz3__fj_pi(op, 2) << 24;
    node->amplify = wz3__fj_pu(op, 3);
    break;
  case 0xe4: /* Crashzoom: size, steps, zoom, amplify, center */
    node->type = WZ3_IPP_CRASHZOOM;
    node->size = wz3__fj_pi(op, 0);
    node->stages = wz3__fj_pi(op, 1);
    node->radius = (float)wz3__fj_p(op, 2);
    node->factor = (float)wz3__fj_p(op, 3);
    node->center[0] = (float)wz3__fj_p(op, 4);
    node->center[1] = (float)wz3__fj_p(op, 5);
    break;
  default: { /* Rendertarget: the input is drawn into the linked Render Target bitmap */
    node->type = WZ3_IPP_RENDERTARGET;
    node->size = 5;
    const int32_t link = op->src->n_links ? op->src->links[0] : -1;
    node->rt_op = (link >= 0 && (uint32_t)link < rt->n_ops && rt->ops[link].class_id == 0x26) ? link : -1;
    break;
  }
  }
  fj->last_output = (int32_t)fj->nodes.n - 1;
  fj->ipp_node[op->id] = fj->last_output;
  return WZ3_OK;
}

static void wz3__fj_ipp_branch_begin(wz3_rt *rt)
{
  rt->fj.last_output = -1;
  for (uint32_t i = 0; i < rt->n_ops; i++) rt->fj.ipp_node[i] = -1;
}

static wz3_result wz3__fj_ipp_branch_end(wz3_rt *rt)
{
  if (rt->fj.last_output < 0) return WZ3_OK;
  uint32_t *slot;
  const wz3_result r = wz3__fj_push(&rt->fj.outputs, sizeof *slot, 1, (void **)&slot);
  if (r == WZ3_OK) *slot = (uint32_t)rt->fj.last_output;
  return r;
}

WZ3_API const wz3_frame *wz3_rt_frame_jobs(const wz3_rt *rt) { return rt ? &rt->fj.view : NULL; }
WZ3_API void wz3_rt_set_aspect(wz3_rt *rt, double aspect)
{
  if (rt) rt->fj.aspect = aspect;
}
