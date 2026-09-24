#pragma once

#include "stdint.h"
#include "stdbool.h"
#include "fast/ucodehandlers.h"
#include "ship/Api.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Sets the native (un-scaled) rendering resolution used by the graphics backend.
 *
 * @param width  Native framebuffer width in pixels.
 * @param height Native framebuffer height in pixels.
 */
API_EXPORT void GfxSetNativeDimensions(uint32_t width, uint32_t height);

/**
 * @brief Prepares the graphics backend to sample the pixel depth at screen coordinate (@p x, @p y).
 *
 * Call this before GfxGetPixelDepth() to ensure the depth value is ready.
 *
 * @param x Screen X coordinate in pixels.
 * @param y Screen Y coordinate in pixels.
 */
API_EXPORT void GfxGetPixelDepthPrepare(float x, float y);

/**
 * @brief Returns the pixel depth at screen coordinate (@p x, @p y).
 *
 * Must be called after GfxGetPixelDepthPrepare() for the same coordinates.
 *
 * @param x Screen X coordinate in pixels.
 * @param y Screen Y coordinate in pixels.
 * @return Depth value in the range [0, 65535] (16-bit fixed-point).
 */
API_EXPORT uint16_t GfxGetPixelDepth(float x, float y);

/**
 * @brief Creates an offscreen framebuffer and returns its id.
 *
 * Declared here, once, for C and C++ alike. A C file that wrote its own prototype without
 * the last parameter compiled natively but not for wasm, whose calls are typed: wasm-ld
 * replaced every call with a trap.
 *
 * @param width            Framebuffer width in pixels.
 * @param height           Framebuffer height in pixels.
 * @param native_width     Native (un-scaled) width in pixels.
 * @param native_height    Native (un-scaled) height in pixels.
 * @param resize           Non-zero to resize with the window.
 * @param forceFixedAspect True to keep the native aspect ratio when resizing. C callers must
 *                         pass it; the default exists only for C++.
 * @return The new framebuffer's id.
 */
API_EXPORT int gfx_create_framebuffer(uint32_t width, uint32_t height, uint32_t native_width, uint32_t native_height,
                                      uint8_t resize,
#ifdef __cplusplus
                                      bool forceFixedAspect = false);
#else
                                      bool forceFixedAspect);
#endif

#ifdef __cplusplus
}
#endif
