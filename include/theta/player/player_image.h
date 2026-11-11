/* player_image.h: the picture decoder of the player. The one Import operator of
 * theta carries a JPEG; the core library has no decoder, the player hands it
 * stb_image (JPEG only).
 *
 *   #define PLAYER_IMAGE_IMPLEMENTATION in exactly one source file.
 */
#ifndef PLAYER_IMAGE_H
#define PLAYER_IMAGE_H

#include <stddef.h>
#include <stdint.h>

#ifdef PLAYER_IMAGE_STATIC
#define PLAYER_IMAGE_API static
#else
#define PLAYER_IMAGE_API extern
#endif

/* wz3_import_fn: decodes the blob to w x h RGBA8 pixels; the pixels come from malloc and pass to the library, which frees them
   with WZ3_FREE (free unless the includer replaced both); returns 1 on success */
PLAYER_IMAGE_API int pimage_import(void *user, const uint8_t *blob, size_t size, const char *filename, int32_t *w, int32_t *h, uint8_t **rgba);

#endif

#ifdef PLAYER_IMAGE_IMPLEMENTATION
#ifndef PLAYER_IMAGE_IMPLEMENTED
#define PLAYER_IMAGE_IMPLEMENTED

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wshadow"
#endif
#include "../../../third_party/stb/stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

PLAYER_IMAGE_API int pimage_import(void *user, const uint8_t *blob, size_t size, const char *filename, int32_t *w, int32_t *h, uint8_t **rgba)
{
  (void)user;
  (void)filename;
  int channels;
  stbi_uc *pixels = stbi_load_from_memory(blob, (int)size, w, h, &channels, 4);
  *rgba = pixels;
  return pixels != NULL;
}

#endif
#endif
