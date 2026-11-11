/* The player executable, started headless (SDL offscreen video driver, dummy audio) and judged by its exit status and report.
 * Which case to run is argv[1]; the cases are the table below. A case is skipped when the machine has no OpenGL ES context. */

#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "support/suite.h"

#if !defined(_WIN32) && defined(THETA_PLAYER_EXE)

#include <sys/stat.h>
#include <sys/wait.h>

#define WORK THETA_TEST_WORK_DIR
/* the production data of the repository, whatever the player would find next to itself */
#define THETA_ARGS "--kx \"" THETA_ASSETS_DIR "/theta.kx\" --ogg \"" THETA_ASSETS_DIR "/theta.ogg\""

typedef struct cli_case {
  const char *name;
  const char *args;            /* the command line after the executable */
  int must_fail;               /* the player must exit with a non-zero status */
  int needs_gl;                /* an OpenGL ES context is created: skip when that is impossible */
  const char *const *contains; /* fragments of the output, in this order; NULL-terminated */
  int (*verify)(const char *output);
  int timing;                  /* judged against wall-clock time: skipped in a sanitizer build, where the player is several times slower */
} cli_case;

/* the trailing `timing` flag of a case is optional */
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

/* the fragment after `marker`, or NULL */
static const char *after(const char *text, const char *marker)
{
  const char *p = strstr(text, marker);
  return p ? p + strlen(marker) : NULL;
}

/* "screenshot: <size>, <N> lit pixels, <M> colours": the picture is not black (lit pixels and several hundred colours) */
static int verify_picture(const char *output, const char *size)
{
  char marker[64];
  snprintf(marker, sizeof marker, "screenshot: %s, ", size);
  const char *p = after(output, marker);
  unsigned lit = 0, colours = 0;
  if (!p || sscanf(p, "%u lit pixels, %u colours", &lit, &colours) != 2) {
    printf("no '%s<N> lit pixels, <M> colours' line\n", marker);
    return 0;
  }
  if (lit < 10000 || colours < 100) {
    printf("the picture looks empty: %u lit pixels, %u colours\n", lit, colours);
    return 0;
  }
  return 1;
}

/* the soundtrack played: a stream that fails to open would leave the picture on the wall clock and the clock checks would still pass */
static int verify_ogg_played(const char *output)
{
  if (strstr(output, "the soundtrack failed")) {
    printf("the soundtrack did not play\n");
    return 0;
  }
  return 1;
}
static int verify_picture_400x300(const char *output) { return verify_picture(output, "400x300"); }
static int verify_overlay(const char *output) { return verify_picture(output, "640x360"); }

/* the window was resized at least four times, and the GL accounting balances */
static int verify_window(const char *output)
{
  const char *p = after(output, "self-check: 0 violations, position ");
  unsigned frames = 0, resizes = 0;
  if (!p || sscanf(p, "%*f s after %u frames, %u resizes", &frames, &resizes) != 2) {
    printf("no 'self-check: 0 violations, position <s> after <N> frames, <K> resizes' line\n");
    return 0;
  }
  if (resizes < 4) {
    printf("only %u resizes were seen\n", resizes);
    return 0;
  }
  return 1;
}

static const char *const k_gl_balanced[] = { "gl-check: 0 GL errors in ", " polls; after glr_destroy: 0 live objects", NULL };
static const char *const k_licenses[] = { "paniq concept, music & direction",
                                          "werkkzeug3 operator library",
                                          "V2 synthesizer",
                                          "GLU tessellator",
                                          "Arimo and Gelasio",
                                          "Creative Commons Attribution-ShareAlike 3.0",
                                          "SDL3, zlib license",
                                          "Ogg Vorbis decoder",
                                          NULL };
static const char *const k_bad_time[] = { "theta: --time wants seconds from 0 to 3600, got 'nonsense'", NULL };
static const char *const k_missing_kx[] = { "cannot read the KX document", NULL };
static const char *const k_missing_ogg[] = { "cannot read the soundtrack", NULL };
static const char *const k_bad_ogg[] = { "the soundtrack failed (not an Ogg Vorbis stream), continuing silent", "self-check: 0 violations", NULL };
static const char *const k_cancelled[] = { "load: cancelled at ", " operators", NULL };
static const char *const k_soak[] = {
  "soak: ", " of 1 loops in ", "0 GL errors, 0 violations", "gl-check: 0 GL errors in ", " polls; after glr_destroy: 0 live objects", NULL
};
#ifdef THETA_TEST_SOAK
static const char *const k_soak_long[] = { "soak: ",
                                           " of 1000 loops in ",
                                           "(stopped by --soak-limit)",
                                           "0 GL errors, 0 violations",
                                           "gl-check: 0 GL errors in ",
                                           " polls; after glr_destroy: 0 live objects",
                                           NULL };
#endif

static const cli_case k_cases[] = {
  /* credits and licences: nothing to load, no window */
  { "licenses", "--licenses", 0, 0, k_licenses, NULL },
  /* the command line reports errors; it does not crash */
  { "bad_args", "--time nonsense", 1, 0, k_bad_time, NULL },
  { "missing_kx", "--kx \"" WORK "/does-not-exist.kx\" --no-audio --time 1", 1, 0, k_missing_kx, NULL },
  { "missing_ogg", "--kx \"" THETA_ASSETS_DIR "/theta.kx\" --ogg \"" WORK "/does-not-exist.ogg\" --self-check --play-for 1", 1, 0, k_missing_ogg, NULL },
  { "bad_ogg", "--kx \"" THETA_ASSETS_DIR "/theta.kx\" --ogg \"" THETA_ASSETS_DIR "/theta.kx\" --self-check --no-vsync --size 320x240 --play-for 2", 0, 1,
    k_bad_ogg, NULL },
  /* the picture at a given time is not empty */
  { "picture", "--kx \"" THETA_ASSETS_DIR "/theta.kx\" --no-audio --time 14 --aspect 4:3 --size 400x300 --screenshot \"" WORK "/picture.ppm\"", 0, 1, NULL,
    verify_picture_400x300 },
  /* restoring a snapshot and replaying reproduces linear playback */
  { "seek_replay", "--kx \"" THETA_ASSETS_DIR "/theta.kx\" --no-audio --time 20 --seek-check --size 320x180", 0, 1, NULL, NULL },
  /* the credits and the statistics screens change the pixels a plain frame has */
  { "overlay", THETA_ARGS " --no-audio --time 100 --size 640x360 --texture-quality -2 --overlay credits,stats --screenshot \"" WORK "/overlay.ppm\"", 0, 1,
    NULL, verify_overlay },
  /* the GL accounting: nothing is left once the renderer is destroyed */
  { "gl_check", THETA_ARGS " --no-audio --gl-check --time 60 --size 320x180 --texture-quality -2", 0, 1, k_gl_balanced, NULL },
  /* deterministic stepping through the demo with seeks, resizes and screens */
  { "soak_short", "--kx \"" THETA_ASSETS_DIR "/theta.kx\" --soak 1 --soak-limit 25 --size 320x240 --texture-quality -2", 0, 1, k_soak, NULL },
  /* quitting while the runtime is still loading cancels the worker at its next operator */
  { "cancel_loading", THETA_ARGS " --self-check --no-audio --no-vsync --exit-after 0.05", 0, 1, k_cancelled, NULL },
  /* the web's loader: time slices on the main thread; cancelling, finishing and playing work the same as with the worker */
  { "cancel_loading_single", THETA_ARGS " --single-thread --self-check --no-audio --no-vsync --exit-after 0.05", 0, 1, k_cancelled, NULL },
  { "live_single_thread", THETA_ARGS " --single-thread --self-check --no-audio --no-vsync --play-for 3 --script 1:space,2:space --expect-position 2", 0, 1,
    NULL, NULL, 1 },
  /* audio clock, pause (rewinds the audio), forward and backward seek through the real SDL audio pipeline; the script times
     count from the party start, so the load time does not matter */
  { "live_session",
    THETA_ARGS " --self-check --gl-check --no-vsync --size 320x240 --play-for 6 --script 2:space,2.5:space,3:right,4:left --expect-position 5.5", 0, 1,
    k_gl_balanced, verify_ogg_played, 1 },
  { "live_no_audio", THETA_ARGS " --self-check --no-audio --no-vsync --play-for 4 --script 1:space,2:space --expect-position 3", 0, 1, NULL, NULL, 1 },
  /* the window: other shapes (the picture stays centred), statistics and credits, fullscreen where the driver allows it;
     the GL objects balance and glGetError stays clear after every frame */
  { "window",
    THETA_ARGS " --self-check --gl-check --no-audio --no-vsync --size 320x180 --texture-quality -2 --play-for 7 "
               "--script 1:resize,1.5:resize,2:resize,2.5:resize,3:stats,3.5:credits,4:fullscreen,5:fullscreen,6:f11,6.5:stats",
    0, 1, k_gl_balanced, verify_window, 1 },
#ifdef THETA_TEST_SOAK
  /* ten minutes of wall time: the whole production in deterministic steps, over and over, with seeks, resizes and the player's screens in every
     loop; a GL error, GL objects that differ between loops or memory growth fails it */
  { "soak_long", "--kx \"" THETA_ASSETS_DIR "/theta.kx\" --soak 1000 --soak-limit 600 --size 320x240 --texture-quality -2", 0, 1, k_soak_long, NULL },
#endif
};

static char *run(const char *command, int *exit_status)
{
  FILE *p = popen(command, "r");
  if (!p) return NULL;
  size_t size = 0, cap = 1 << 16;
  char *buf = (char *)malloc(cap);
  size_t n;
  while (buf && (n = fread(buf + size, 1, cap - size - 1, p)) > 0) {
    size += n;
    if (cap - size < 4096) {
      char *bigger = (char *)realloc(buf, cap * 2);
      if (!bigger) {
        free(buf);
        buf = NULL;
        break;
      }
      buf = bigger;
      cap *= 2;
    }
  }
  const int status = pclose(p);
  if (buf) buf[size] = 0;
  *exit_status = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  return buf;
}

int suite_player_cli(int argc, char **argv)
{
  if (argc < 2) {
    printf("usage: player_cli <case>\n");
    return 2;
  }
  const cli_case *c = NULL;
  for (size_t i = 0; i < sizeof k_cases / sizeof k_cases[0]; i++)
    if (!strcmp(k_cases[i].name, argv[1])) c = &k_cases[i];
  if (!c) {
    printf("unknown case %s\n", argv[1]);
    return 2;
  }

#ifdef THETA_TEST_SLOW_BUILD
  if (c->timing) {
    printf("judged against wall-clock time: skipped in a sanitizer build\n");
    return THETA_TEST_SKIP;
  }
#endif

  mkdir(WORK, 0777);
  setenv("SDL_VIDEODRIVER", "offscreen", 1);
  setenv("SDL_AUDIODRIVER", "dummy", 1);
#ifdef THETA_TEST_LSAN_SUPP
  setenv("LSAN_OPTIONS", "suppressions=" THETA_TEST_LSAN_SUPP, 1);
#endif

  char command[2048];
  snprintf(command, sizeof command, "\"%s\" %s 2>&1", THETA_PLAYER_EXE, c->args);
  int status = 0;
  char *output = run(command, &status);
  if (!output) {
    printf("cannot run %s\n", command);
    return 1;
  }

  int rc = 0;
  if (c->needs_gl && status != 0 && (strstr(output, "OpenGL context") || strstr(output, "window:"))) {
    printf("no OpenGL ES context: %s\n", output);
    rc = THETA_TEST_SKIP;
  } else {
    if (c->must_fail ? status == 0 : status != 0) {
      printf("exit status %d\n", status);
      rc = 1;
    }
    const char *p = output;
    for (size_t i = 0; rc == 0 && c->contains && c->contains[i]; i++) {
      const char *hit = strstr(p, c->contains[i]);
      if (!hit) {
        printf("the output lacks '%s'\n", c->contains[i]);
        rc = 1;
      } else
        p = hit + strlen(c->contains[i]);
    }
    if (rc == 0 && c->verify && !c->verify(output)) rc = 1;
  }
  if (rc == 1) printf("--- %s\n%s\n", command, output);
  free(output);
  return rc;
}

#else

int suite_player_cli(int argc, char **argv)
{
  (void)argc;
  (void)argv;
  printf("the player executable is not available\n");
  return THETA_TEST_SKIP;
}

#endif
