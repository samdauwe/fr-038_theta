/* theta_scene: what a frame of the document draws, as the runtime plans it.
 *
 *   theta_scene [--time SECONDS] DOCUMENT.kx
 *
 * Runs the frames of the replay cadence (30 per second of song) up to the time and prints the IPP nodes of the last one:
 * viewport cameras, clear colours, lights, and every mesh job with the class of the operator that scheduled it and the
 * position of its matrix. Geometry is not generated (the placeholder policy), so it is cheap: it answers "what is in the picture". */
#define _POSIX_C_SOURCE 200809L
#define WZ3_IMPLEMENTATION
#include <theta/core/wz3.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *class_name(uint16_t id)
{
  const wz3_class_info *info = wz3_class_lookup(id);
  return info ? info->name : "?";
}

int main(int argc, char **argv)
{
  const char *path = NULL;
  double seconds = 0.0;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--time") && i + 1 < argc)
      seconds = atof(argv[++i]);
    else if (argv[i][0] == '-' || path)
      path = NULL, i = argc;
    else
      path = argv[i];
  }
  if (!path) {
    fprintf(stderr, "usage: theta_scene [--time SECONDS] DOCUMENT.kx\n");
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
  if (wz3_kx_parse(&kx, bytes, (size_t)n, NULL) != WZ3_OK) return 1;
  free(bytes);
  wz3_rt_options o;
  memset(&o, 0, sizeof o);
  o.generic_exec = 1;
  o.release_consumed = 1;
  o.frame_jobs = 1;
  o.geometry = WZ3_GEOMETRY_PLACEHOLDER;
  wz3_result res;
  wz3_rt *rt = wz3_rt_create(kx, &o, &res);
  if (!rt) return 1;
  wz3_rt_set_aspect(rt, 5.0 / 3.0);
  res = wz3_rt_precalc(rt);
  if (res != WZ3_OK) {
    fprintf(stderr, "precalc: %s\n", wz3_result_string(res));
    return 1;
  }
  const int rate = 44100;
  const int64_t target = (int64_t)(seconds * rate);
  int64_t sample = 0;
  do {
    sample += rate / 30;
    if (sample > target) sample = target;
    res = wz3_rt_frame_at_sample(rt, (int32_t)sample, rate);
    if (res != WZ3_OK) break;
    uint32_t ran;
    res = wz3_rt_effects_advance(rt, &ran);
  } while (res == WZ3_OK && sample < target);
  if (res != WZ3_OK) {
    fprintf(stderr, "frame: %s\n", wz3_result_string(res));
    return 1;
  }
  const wz3_frame *fr = wz3_rt_frame_jobs(rt);
  printf("t = %.2f s, %u outputs, %u nodes\n", seconds, fr->n_outputs, fr->n_nodes);
  for (uint32_t i = 0; i < fr->n_nodes; i++) {
    const wz3_ipp_node *nd = &fr->nodes[i];
    if (nd->type == WZ3_IPP_LAYER2D) {
      printf("node %u: Layer2D input %d material %d flags 0x%x size %d z %.3f screen %.3f %.3f %.3f %.3f uv %.3f %.3f %.3f %.3f\n", i, nd->input,
             nd->material_op, nd->flags, nd->size, nd->z, nd->screen[0], nd->screen[1], nd->screen[2], nd->screen[3], nd->uv[0], nd->uv[1], nd->uv[2],
             nd->uv[3]);
      continue;
    }
    if (nd->type != WZ3_IPP_VIEWPORT) {
      static const char *const names[] = { "?", "Viewport", "Layer2D", "Color", "Blur", "Merge", "Crashzoom", "Select", "RenderTarget" };
      printf("node %u: %s size %d input %d input2 %d operation %d color 0x%08x amplify 0x%08x radius %.3f factor %.3f stages %d rt_op %d select", i,
             nd->type < 9 ? names[nd->type] : "?", nd->size, nd->input, nd->input2, nd->operation, nd->color, nd->amplify, nd->radius, nd->factor, nd->stages,
             nd->rt_op);
      for (uint32_t k = 0; k < nd->n_select; k++) printf(" %u", fr->select_inputs[nd->first_select + k]);
      printf("\n");
      continue;
    }
    const float *c = nd->camera.camera_space;
    printf("node %u: Viewport size %d flags 0x%x clear 0x%08x ambient 0x%08x fog 0x%08x [%.1f..%.1f] zoom %.3f %.3f clip %.2f..%.2f\n", i, nd->size, nd->flags,
           nd->clear_color, nd->ambient, nd->camera.fog_color, nd->camera.fog_start, nd->camera.fog_end, nd->camera.zoom_x, nd->camera.zoom_y,
           nd->camera.near_clip, nd->camera.far_clip);
    printf("  camera at %.2f %.2f %.2f looking %.2f %.2f %.2f, %u meshes, %u effects, %u lights\n", c[12], c[13], c[14], c[8], c[9], c[10], nd->n_mesh,
           nd->n_effect, nd->n_light);
    for (uint32_t k = 0; k < nd->n_light; k++) {
      const wz3_light_job *l = &fr->light_jobs[nd->first_light + k];
      printf("  light %u: op %u %s color 0x%08x amplify %.2f range %.1f at %.2f %.2f %.2f dir %.2f %.2f %.2f flags 0x%x\n", k, l->op,
             l->directional ? "directional" : "point", l->color, l->amplify, l->range, l->position[0], l->position[1], l->position[2], l->direction[0],
             l->direction[1], l->direction[2], l->flags);
    }
    for (uint32_t k = 0; k < nd->n_mesh && (k < 60 || getenv("THETA_SCENE_ALL")); k++) {
      const wz3_mesh_job *j = &fr->mesh_jobs[nd->first_mesh + k];
      const uint16_t cid = j->source_op >= 0 ? kx->ops[j->source_op].class_id : 0;
      printf("  mesh %u: op %d (0x%02x %s) from op %d (%s) at %.2f %.2f %.2f\n", k, j->mesh_op, j->mesh_op >= 0 ? kx->ops[j->mesh_op].class_id : 0,
             j->mesh_op >= 0 ? class_name(kx->ops[j->mesh_op].class_id) : "-", j->source_op, class_name(cid), j->matrix[12], j->matrix[13], j->matrix[14]);
    }
    for (uint32_t k = 0; k < nd->n_effect; k++) {
      const wz3_effect_job *j = &fr->effect_jobs[nd->first_effect + k];
      const wz3_effect_output *out = NULL;
      if (kx->ops[j->effect_op].class_id != 0x61 || wz3_rt_effect_run(rt, j, &out) != WZ3_OK || !out || out->kind != WZ3_EFFECT_OUT_PRINT) continue;
      float lo[2] = { 1e9f, 1e9f }, hi[2] = { -1e9f, -1e9f };
      for (uint32_t v = 0; v < out->n_print_quads * 4; v++)
        for (int axis = 0; axis < 2; axis++) {
          const float x = out->print_vertices[v * 5 + axis];
          if (x < lo[axis]) lo[axis] = x;
          if (x > hi[axis]) hi[axis] = x;
        }
      printf("  effect %u: op %u (Print) %u letters, x %.3f .. %.3f, y %.3f .. %.3f at %.2f %.2f %.2f\n", k, j->effect_op, out->n_print_quads, lo[0], hi[0],
             lo[1], hi[1], out->matrix[12], out->matrix[13], out->matrix[14]);
    }
  }
  if (getenv("THETA_MATERIAL")) {
    const wz3_material *m = wz3_rt_material(rt, (uint32_t)atoi(getenv("THETA_MATERIAL")));
    if (m) {
      printf("material: system %u multi_flags 0x%x shader_mask 0x%x textures", m->system, m->multi_flags, m->shader_mask);
      for (int s = 0; s < 7; s++) printf(" %d", m->textures[s]);
      printf("\n");
      for (uint32_t k = 0; k < m->n_passes; k++)
        printf("  pass %u: usage %u program %u state %u render_pass %d\n", k, m->passes[k].usage, m->passes[k].program, m->passes[k].state,
               m->passes[k].render_pass);
    }
  }
  wz3_rt_destroy(rt);
  wz3_kx_destroy(kx);
  return 0;
}
