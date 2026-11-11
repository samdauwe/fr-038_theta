/* player_core.h - the SDL-free halves of the theta player (stb-style single header)

   pclock  The visual master clock. It turns "frames the audio device has played" (put - queued - device latency, read
           from any audio API) into a song position that advances smoothly between polls, never runs backwards and
           never leads the audio that is actually audible.
   paudio  The loader tune -> party song hand-over on top of v2.h: one synth alive at a time, silence while paused,
           a frame counter of the party song for the clock. The party song is either a V2 module (a document
           with its own song) or an Ogg Vorbis stream (theta: the document has no song, the Ogg is a separate file; ogg.h).

   Neither part touches SDL, a thread or a clock of its own: main.c supplies the time and the locking, the tests
   supply a simulated device.

     #define PLAYER_CORE_IMPLEMENTATION
     #include <theta/player/player_core.h>
*/
#ifndef PLAYER_CORE_H_INCLUDED
#define PLAYER_CORE_H_INCLUDED

#include <stdint.h>
#include <stddef.h>

#ifndef V2_H_INCLUDED
#include <theta/core/v2.h>
#endif
#ifndef OGG_H_INCLUDED
#include <theta/audio/ogg.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifndef PC_API
#ifdef PLAYER_CORE_STATIC
#define PC_API static
#else
#define PC_API extern
#endif
#endif

/* ---- clock ---- */

typedef struct pclock {
  double rate;           /* song sample rate in Hz */
  int64_t anchor_sample; /* the last played-frame count the device reported ... */
  uint64_t anchor_ns;    /* ... and the time at which the caller noticed it */
  int64_t reported;      /* last reported value (a change re-anchors) */
  int64_t last;          /* last value handed out: the clock never goes below it */
  uint64_t max_extrapolate_ns;
  int running;
} pclock;

/* max_extrapolate_ms bounds how far the clock may run past the last change of the reported value (an underrunning
   stream must not let the picture race ahead of the sound). */
PC_API void pclock_init(pclock *c, uint32_t sample_rate, uint32_t max_extrapolate_ms);
/* Start or seek: the song position is `sample` now. */
PC_API void pclock_reset(pclock *c, int64_t sample, uint64_t now_ns);
/* played = frames put to the device - frames still queued - device latency, clamped at 0 by the caller or not. */
PC_API void pclock_report(pclock *c, int64_t played, uint64_t now_ns);
PC_API void pclock_set_running(pclock *c, int running, uint64_t now_ns);
/* The song position in frames. Monotonic between resets; while stopped it returns the last value. */
PC_API int64_t pclock_now(pclock *c, uint64_t now_ns);

/* ---- audio ---- */

typedef enum paudio_stage { PAUDIO_SILENT = 0, PAUDIO_LOADER, PAUDIO_PARTY } paudio_stage;

typedef struct paudio {
  v2_synth *synth;
  ogg *ogg; /* the party song when it is an Ogg stream (then synth is NULL); the bytes belong to the caller */
  paudio_stage stage;
  uint32_t rate;
  int paused;
  uint64_t party_frames;  /* frames of the party song rendered so far (the clock's "put") */
  uint64_t party_base;    /* ... including the start offset */
  uint64_t length_frames; /* length of the song that was started, from its sequence timing */
} paudio;

PC_API void paudio_init(paudio *a, uint32_t sample_rate);
PC_API void paudio_destroy(paudio *a);
/* The previous synth is destroyed before the next one exists (the reference player does the same to bound memory). */
PC_API v2_result paudio_play_loader(paudio *a, const void *v2m, size_t size);
PC_API v2_result paudio_play_party(paudio *a, const void *v2m, size_t size, uint32_t from_ms);
/* The party song is an Ogg stream that must stay in memory until paudio_stop; its rate has to be the one of paudio_init. */
PC_API ogg_result paudio_play_party_ogg(paudio *a, const void *ogg_bytes, size_t size, int64_t from_sample);
PC_API void paudio_stop(paudio *a);
PC_API void paudio_set_paused(paudio *a, int paused);
/* Interleaved stereo float. Silence when nothing plays or while paused. */
PC_API void paudio_render(paudio *a, float *out, uint32_t frames);
/* Next frame boundary of the deterministic replay cadence (reference player): the first
   floor(frame * rate / fps) > sample, capped at target. Used to step the runtime during a seek. */
PC_API int64_t pseek_next_sample(int64_t sample, int64_t target, int64_t rate, int64_t fps);

/* Seeks the playing party song (restarts the sequencer at from_ms). */
PC_API v2_result paudio_seek(paudio *a, uint32_t from_ms);
/* The same to a sample: exact for an Ogg song, to the millisecond for a V2 module. Returns 0, or -1 when no party song plays or the seek failed. */
PC_API int paudio_seek_sample(paudio *a, int64_t sample);

#ifdef __cplusplus
}
#endif

#endif /* PLAYER_CORE_H_INCLUDED */

#ifdef PLAYER_CORE_IMPLEMENTATION
#ifndef PLAYER_CORE_IMPLEMENTATION_DONE
#define PLAYER_CORE_IMPLEMENTATION_DONE

#include <string.h>

PC_API void pclock_init(pclock *c, uint32_t sample_rate, uint32_t max_extrapolate_ms)
{
  memset(c, 0, sizeof *c);
  c->rate = (double)sample_rate;
  c->max_extrapolate_ns = (uint64_t)max_extrapolate_ms * 1000000ull;
}

PC_API void pclock_reset(pclock *c, int64_t sample, uint64_t now_ns)
{
  c->anchor_sample = c->reported = c->last = sample;
  c->anchor_ns = now_ns;
}

PC_API void pclock_report(pclock *c, int64_t played, uint64_t now_ns)
{
  if (played == c->reported) return;
  c->reported = played;
  c->anchor_sample = played;
  c->anchor_ns = now_ns;
}

PC_API void pclock_set_running(pclock *c, int running, uint64_t now_ns)
{
  if (running && !c->running) { /* extrapolate from where the picture stopped */
    c->anchor_sample = c->last;
    c->anchor_ns = now_ns;
  }
  c->running = running != 0;
}

PC_API int64_t pclock_now(pclock *c, uint64_t now_ns)
{
  if (!c->running) return c->last;
  uint64_t elapsed = now_ns > c->anchor_ns ? now_ns - c->anchor_ns : 0;
  if (elapsed > c->max_extrapolate_ns) elapsed = c->max_extrapolate_ns;
  const int64_t estimate = c->anchor_sample + (int64_t)((double)elapsed * c->rate / 1e9);
  if (estimate > c->last) c->last = estimate;
  return c->last;
}

PC_API void paudio_init(paudio *a, uint32_t sample_rate)
{
  memset(a, 0, sizeof *a);
  a->rate = sample_rate;
}

PC_API void paudio_stop(paudio *a)
{
  v2_destroy(a->synth);
  ogg_close(a->ogg);
  a->synth = NULL;
  a->ogg = NULL;
  a->stage = PAUDIO_SILENT;
}

PC_API void paudio_destroy(paudio *a) { paudio_stop(a); }

static v2_result paudio__start(paudio *a, paudio_stage stage, const void *v2m, size_t size, uint32_t from_ms)
{
  paudio_stop(a);
  v2_synth *s = v2_create(a->rate);
  if (!s) return V2_ERR_OUT_OF_MEMORY;
  v2_result res = v2_open(s, v2m, size);
  if (res != V2_OK) {
    v2_destroy(s);
    return res;
  }
  a->length_frames = v2_length_frames(s); /* implies a reset: before v2_play */
  v2_play(s, from_ms);
  a->synth = s;
  a->stage = stage;
  a->party_frames = 0;
  a->party_base = (uint64_t)((double)from_ms * a->rate / 1000.0);
  return V2_OK;
}

PC_API v2_result paudio_play_loader(paudio *a, const void *v2m, size_t size) { return paudio__start(a, PAUDIO_LOADER, v2m, size, 0); }

PC_API v2_result paudio_play_party(paudio *a, const void *v2m, size_t size, uint32_t from_ms) { return paudio__start(a, PAUDIO_PARTY, v2m, size, from_ms); }

PC_API ogg_result paudio_play_party_ogg(paudio *a, const void *ogg_bytes, size_t size, int64_t from_sample)
{
  paudio_stop(a);
  ogg *o = NULL;
  ogg_result res = ogg_open(ogg_bytes, size, &o);
  if (res != OGG_OK) return res;
  if (ogg_rate(o) != a->rate) {
    ogg_close(o);
    return OGG_ERR_FORMAT;
  }
  if (from_sample > 0 && (res = ogg_seek(o, (uint64_t)from_sample)) != OGG_OK) {
    ogg_close(o);
    return res;
  }
  a->ogg = o;
  a->length_frames = ogg_length_frames(o);
  a->stage = PAUDIO_PARTY;
  a->party_frames = 0;
  a->party_base = from_sample > 0 ? (uint64_t)from_sample : 0;
  return OGG_OK;
}

PC_API v2_result paudio_seek(paudio *a, uint32_t from_ms)
{
  if (a->stage != PAUDIO_PARTY || (!a->synth && !a->ogg)) return V2_ERR_BAD_ARGUMENT;
  if (a->ogg) return paudio_seek_sample(a, (int64_t)((double)from_ms * a->rate / 1000.0)) == 0 ? V2_OK : V2_ERR_BAD_ARGUMENT;
  v2_play(a->synth, from_ms);
  a->party_frames = 0;
  a->party_base = (uint64_t)((double)from_ms * a->rate / 1000.0);
  return V2_OK;
}

PC_API int paudio_seek_sample(paudio *a, int64_t sample)
{
  if (a->stage != PAUDIO_PARTY || (!a->synth && !a->ogg)) return -1;
  if (sample < 0) sample = 0;
  if (a->ogg) {
    if (ogg_seek(a->ogg, (uint64_t)sample) != OGG_OK) return -1;
    a->party_frames = 0;
    a->party_base = (uint64_t)sample;
    return 0;
  }
  if (paudio_seek(a, (uint32_t)((double)sample * 1000.0 / a->rate)) != V2_OK) return -1;
  a->party_base = (uint64_t)sample;
  return 0;
}

PC_API int64_t pseek_next_sample(int64_t sample, int64_t target, int64_t rate, int64_t fps)
{
  int64_t frame = sample * fps / rate + 1;
  int64_t boundary = frame * rate / fps;
  while (boundary <= sample) boundary = ++frame * rate / fps;
  return boundary < target ? boundary : target;
}

PC_API void paudio_set_paused(paudio *a, int paused) { a->paused = paused != 0; }

PC_API void paudio_render(paudio *a, float *out, uint32_t frames)
{
  if ((!a->synth && !a->ogg) || a->paused) {
    memset(out, 0, (size_t)frames * 2 * sizeof(float));
    return;
  }
  if (a->ogg)
    ogg_render(a->ogg, out, frames);
  else
    v2_render(a->synth, out, frames);
  if (a->stage == PAUDIO_PARTY) a->party_frames += frames;
}

#endif /* PLAYER_CORE_IMPLEMENTATION_DONE */
#endif /* PLAYER_CORE_IMPLEMENTATION */
