#ifndef FAST_TOON_SHADING_H
#define FAST_TOON_SHADING_H

// SOH [Enhancement] Shared definitions for the per-pixel toon-lighting effect.
//
// The effect lives in the toon variant of the Fast3D fragment shaders (a distinct shader compiled
// when the SHADER_OPT(TOON) bit is set). The game emits a gSPToon(true/false) marker around actor
// draws (gated by the Enabled CVar); the interpreter forwards the object-space vertex normal and the
// single dominant light, and the fragment shader ramps N·L. RampCenter/RampSoftness shape the ramp
// and are read straight from these CVars by each backend.

// CVar keys — must match CVAR_ENHANCEMENT("Graphics.ToonLighting.*") on the SoH side (prefix
// "gEnhancements"). Spelled out literally because libultraship has no access to SoH's CVar macros.
#define CVAR_TOON_SHADING_ENABLED "gEnhancements.Graphics.ToonLighting.Enabled"
#define CVAR_TOON_SHADING_RAMP_CENTER "gEnhancements.Graphics.ToonLighting.RampCenter"
#define CVAR_TOON_SHADING_RAMP_SOFTNESS "gEnhancements.Graphics.ToonLighting.RampSoftness"
#define CVAR_TOON_SHADING_HIGHLIGHT "gEnhancements.Graphics.ToonLighting.HighlightIntensity"
#define CVAR_TOON_SHADING_SHADOW "gEnhancements.Graphics.ToonLighting.ShadowIntensity"

// Defaults shared by every rendering backend (half-Lambert N·L mapped to 0..1).
#define TOON_SHADING_DEFAULT_RAMP_CENTER 0.5f
#define TOON_SHADING_DEFAULT_RAMP_SOFTNESS 0.1f
// Highlight = brightness of the lit band; Shadow = how dark the shadow band gets (1 = ambient).
// Both default to 1.0, which reproduces the plain "ambient + ramp*lightColor" two-tone.
#define TOON_SHADING_DEFAULT_HIGHLIGHT 1.0f
#define TOON_SHADING_DEFAULT_SHADOW 1.0f

#endif // FAST_TOON_SHADING_H
