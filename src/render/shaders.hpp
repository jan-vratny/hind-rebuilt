#pragma once

namespace render::shaders {

// Lit, fogged, flat-shaded geometry (terrain, objects, aircraft).
inline const char* kSceneVS = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec3 aTex;
uniform mat4 uViewProj;
uniform mat4 uModel;
out vec3 vNormal;
out vec4 vColor;
out vec3 vWorld;
out vec3 vTex;
out vec3 vModel, vModelNormal;  // on the model (the livery's markings)
void main() {
  vModel = aPos;
  vModelNormal = aNormal;
  vec4 w = uModel * vec4(aPos, 1.0);
  vWorld = w.xyz;
  vNormal = mat3(uModel) * aNormal;
  vColor = aColor;
  vTex = aTex;
  gl_Position = uViewProj * w;
}
)";

inline const char* kSceneFS = R"(#version 330 core
in vec3 vNormal;
in vec4 vColor;
in vec3 vWorld;
in vec3 vTex;
in vec3 vModel, vModelNormal;
uniform sampler2D uAtlas;  // the original's object textures (render::uploadAtlas)
// 1: the Ukrainian missions' Hind (render/livery.hpp): the Czech camouflage of the atlas's last
// tile for the original's (tile 2), and Ukraine's colours painted on by the place on the airframe
// (x to the right, y up, z back, the main rotor's mast at 0).
uniform float uLivery;
uniform vec3 uLiveryOffset;  // into the player's Hind's axes (the original's other Hind objects)
uniform vec3 uSunDir;
uniform vec3 uFogColor;
uniform float uFogDist;
uniform vec3 uCamPos;
uniform float uUnlit;  // 1: flat colours (cockpit instruments)
uniform vec3 uLight;   // the light's colour and strength (render::Palette)
uniform float uAmbient, uDiffuse;
// Shadows (render/shadow.hpp): 1 the airframe's, all dark; 2 the rotor disc's, every other
// pixel; drawn multiplying what is under them by uShadowDark (fading into the fog).
uniform float uShadow;
uniform float uShadowDark;
out vec4 fragColor;
void main() {
  if (uShadow > 0.5) {
    if (uShadow > 1.5 && ((int(gl_FragCoord.x) + int(gl_FragCoord.y)) & 1) != 0) discard;
    float d = length(vWorld - uCamPos) / uFogDist;
    float fog = clamp(1.0 - exp(-d * d * 2.2), 0.0, 1.0);
    fragColor = vec4(vec3(mix(uShadowDark, 1.0, fog)), 1.0);
    return;
  }
  vec3 n = normalize(vNormal);
  if (!gl_FrontFacing) n = -n;
  float diff = max(dot(n, uSunDir), 0.0);
  vec2 uv = vTex.xy;
  if (uLivery > 0.5 && vTex.z >= 0.0 && floor(uv * 8.0) == vec2(2.0, 0.0)) uv += vec2(5.0, 7.0) / 8.0;
  vec3 base = vTex.z < 0.0 ? vColor.rgb : texture(uAtlas, uv).rgb * vColor.rgb;
  // Painted over the camouflage and the light grey, not the glass (bluish), the exhausts, the black.
  bool glass = vTex.z < 0.0 && base.b > base.r + 0.06;
  if (uLivery > 0.5 && !glass && (vTex.z >= 0.0 || dot(base, vec3(0.30, 0.59, 0.11)) > 0.35)) {
    const vec3 blue = vec3(0.10, 0.36, 0.72), yellow = vec3(0.95, 0.80, 0.12), white = vec3(0.86, 0.87, 0.84);
    vec3 p = vModel + uLiveryOffset, m = normalize(vModelNormal);
    bool side = abs(m.x) > 0.35;
    // Along the nose under the cockpits, rising a little to the cabin, and on the cabin's side
    // behind the door: blue over yellow.
    float band = p.z < -1.6 ? mix(-0.62, -0.30, clamp((p.z + 6.3) / 4.7, 0.0, 1.0)) : -0.30;
    if (side && ((p.z > -6.3 && p.z < -1.6) || (p.z > 0.7 && p.z < 3.0)) && abs(p.y - band) < 0.24)
      base = p.y > band ? blue : yellow;
    // White bands around the tail boom.
    if (p.z > 5.6 && p.z < 7.15 && mod(p.z - 5.6, 0.95) < 0.6) base = white;
    // The wing tips' plates: blue over yellow.
    if (abs(p.x) > 3.15 && abs(m.x) > 0.6) base = p.y > -0.57 ? blue : yellow;
  }
  vec3 col = mix(base * uLight * (uAmbient + uDiffuse * diff), base, uUnlit);
  float d = length(vWorld - uCamPos) / uFogDist;
  float fog = clamp(1.0 - exp(-d * d * 2.2), 0.0, 1.0);
  fragColor = vec4(mix(col, uFogColor, fog), vColor.a);
}
)";

// Full-screen sky gradient computed from the view ray.
inline const char* kSkyVS = R"(#version 330 core
out vec2 vNdc;
void main() {
  vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2) * 2.0 - 1.0;
  vNdc = p;
  gl_Position = vec4(p, 0.9999, 1.0);
}
)";

inline const char* kSkyFS = R"(#version 330 core
in vec2 vNdc;
uniform mat4 uInvViewProj;
uniform vec3 uHorizon;
uniform vec3 uZenith;
uniform vec3 uGround;
uniform vec3 uMoonDir;
uniform float uMoon;   // 0..1: the moon's crescent
out vec4 fragColor;
void main() {
  vec4 a = uInvViewProj * vec4(vNdc, -1.0, 1.0);
  vec4 b = uInvViewProj * vec4(vNdc, 1.0, 1.0);
  vec3 dir = normalize(b.xyz / b.w - a.xyz / a.w);
  float e = dir.y;
  vec3 col = e > 0.0 ? mix(uHorizon, uZenith, pow(clamp(e, 0.0, 1.0), 0.6))
                     : mix(uHorizon, uGround, clamp(-e * 6.0, 0.0, 1.0));
  if (uMoon > 0.0) {
    // The moon: a pale crescent, a disc less the same disc shifted to one side.
    const float r = 0.02;
    float a = acos(clamp(dot(dir, uMoonDir), -1.0, 1.0));
    if (a < r) {
      vec3 side = normalize(cross(uMoonDir, vec3(0.0, 1.0, 0.0)) + vec3(0.0, 0.3, 0.0));
      float b = acos(clamp(dot(dir, normalize(uMoonDir + side * r * 0.75)), -1.0, 1.0));
      float lit = b > r ? 1.0 : 0.08;
      col = mix(col, vec3(0.86, 0.85, 0.74), uMoon * lit * smoothstep(r, r * 0.85, a));
    }
  }
  fragColor = vec4(col, 1.0);
}
)";

// The stars of the catalogue (render/stars.inc) as points: where they stand in the sky at the
// sidereal time uLst from the latitude uLat, as big and bright as their magnitude, tinted by
// their colour index, dimmed low over the horizon.
inline const char* kStarVS = R"(#version 330 core
layout(location = 0) in vec4 aStar;  // right ascension, declination (rad), magnitude, B-V
uniform mat4 uViewProj;
uniform float uLst, uLat, uPixel, uBright;
out vec3 vColor;
out float vAlpha;
void main() {
  float h = uLst - aStar.x, d = aStar.y;
  float east = -cos(d) * sin(h);
  float north = sin(d) * cos(uLat) - cos(d) * cos(h) * sin(uLat);
  float up = sin(d) * sin(uLat) + cos(d) * cos(h) * cos(uLat);
  // World axes: x east, y up, z south. A direction (w 0): the stars are infinitely far.
  vec4 p = uViewProj * vec4(east, up, -north, 0.0);
  gl_Position = vec4(p.xy, p.w * 0.99999, p.w);
  float flux = pow(10.0, -0.4 * (aStar.z - 1.0));  // 1 for a star of the first magnitude
  gl_PointSize = clamp(1.8 + 2.2 * sqrt(flux), 1.8, 6.0) * uPixel;
  vAlpha = clamp(0.3 + 1.5 * flux, 0.0, 1.0) * uBright * smoothstep(-0.01, 0.08, up);
  float t = clamp(aStar.w, -0.3, 1.8);
  vColor = t < 0.4 ? mix(vec3(0.72, 0.82, 1.0), vec3(1.0), (t + 0.3) / 0.7) : mix(vec3(1.0), vec3(1.0, 0.75, 0.5), (t - 0.4) / 1.4);
}
)";

inline const char* kStarFS = R"(#version 330 core
in vec3 vColor;
in float vAlpha;
out vec4 fragColor;
void main() {
  float r = length(gl_PointCoord - vec2(0.5)) * 2.0;
  fragColor = vec4(vColor, vAlpha * (1.0 - smoothstep(0.55, 1.0, r)));
}
)";

// 2D overlay (HUD): pixel coordinates, per-vertex colour.
inline const char* kHudVS = R"(#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec4 aColor;
uniform vec2 uScreen;
out vec4 vColor;
void main() {
  vColor = aColor;
  gl_Position = vec4(aPos / uScreen * 2.0 - 1.0, 0.0, 1.0);
}
)";

inline const char* kHudFS = R"(#version 330 core
in vec4 vColor;
out vec4 fragColor;
void main() { fragColor = vColor; }
)";

// Cockpit picture with transparent windows over the 3D view (full-screen triangle of kSkyVS).
// The picture is uScale screen pixels per picture pixel, its left edge at uLeft, top at the top
// of the screen; outside it the edge columns repeat (clamp to edge).
inline const char* kOverlayFS = R"(#version 330 core
uniform sampler2D uImage;
uniform vec2 uImageSize;
uniform float uLeft, uScale, uScreenHeight;
uniform float uTop;  // the picture's row at the top of the screen, negative: none there
uniform vec3 uTint;  // the light in the cockpit (dim at night)
uniform vec3 uLit;   // the instruments' marks with their lamps on (L)
uniform float uLitFrom;  // the picture's row the instrument panel starts at
out vec4 fragColor;
void main() {
  vec2 p = vec2(gl_FragCoord.x - uLeft, uScreenHeight - gl_FragCoord.y) / uScale;
  p.y += uTop;
  if (p.y < 0.0) discard;  // over the picture: the sky
  fragColor = texture(uImage, p / uImageSize);
  // The dials' marks are the panel's light greys; the panel's blue and the black stay dim.
  vec3 c = fragColor.rgb;
  bool mark = p.y >= uLitFrom && max(c.r, max(c.g, c.b)) - min(c.r, min(c.g, c.b)) < 0.05 && c.g > 0.25;
  fragColor.rgb *= mark ? uLit : uTint;
}
)";

// Night vision over the finished frame (full-screen triangle of kSkyVS): brightness L turns
// (0, 40/255 + L, 40/255 + L), as on the original's screen.
// The lone bowman's fall: red before his eyes, as strong as uAmount (dark, not bright).
inline const char* kHurtFS = R"(#version 330 core
in vec2 vNdc;
uniform sampler2D uImage;
uniform float uAmount;
out vec4 fragColor;
void main() {
  vec3 c = texture(uImage, vNdc * 0.5 + 0.5).rgb;
  fragColor = vec4(mix(c, vec3(0.45, 0.0, 0.0) + c * vec3(0.5, 0.12, 0.12), uAmount * 0.8), 1.0);
}
)";

// The lone bowman hit: blood splashed over the picture from its edges, more the worse the hit
// (uAmount 0..1, fading), its shape by the hit (uTime: a seed, not a clock). Dark red, never bright.
inline const char* kBloodFS = R"(#version 330 core
in vec2 vNdc;
uniform sampler2D uImage;
uniform float uAmount;
uniform float uTime;
out vec4 fragColor;
float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float noise(vec2 p) {
  vec2 i = floor(p), f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  return mix(mix(hash(i), hash(i + vec2(1, 0)), f.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), f.x), f.y);
}
float fbm(vec2 p) { return 0.5 * noise(p) + 0.3 * noise(p * 2.1 + 3.7) + 0.2 * noise(p * 4.3 + 9.1); }
void main() {
  vec3 c = texture(uImage, vNdc * 0.5 + 0.5).rgb;
  vec2 seed = vec2(fract(uTime * 0.137) * 100.0, fract(uTime * 0.291) * 100.0);
  float edge = length(vNdc * vec2(1.0, 0.85));
  // Runs down from the splashes: streaks below the top edge.
  float run = smoothstep(0.62, 0.9, noise(vec2(vNdc.x * 18.0, seed.y))) * smoothstep(-0.2, 0.9, vNdc.y);
  float splash = edge + 0.75 * fbm(vNdc * 3.2 + seed) + 0.35 * run - 0.2;
  float reach = 1.45 - 0.8 * uAmount;
  float m = smoothstep(reach, reach + 0.08, splash) * min(1.0, uAmount * 2.5);
  vec3 blood = vec3(0.32, 0.01, 0.01) + c * vec3(0.18, 0.03, 0.03);
  vec3 tinted = mix(c, c * vec3(1.0, 0.72, 0.7), uAmount * 0.6);
  fragColor = vec4(mix(tinted, blood, m * 0.92), 1.0);
}
)";

inline const char* kNightVisionFS = R"(#version 330 core
in vec2 vNdc;
uniform sampler2D uImage;
out vec4 fragColor;
void main() {
  float l = dot(texture(uImage, vNdc * 0.5 + 0.5).rgb, vec3(0.30, 0.59, 0.11));
  float g = min(40.0 / 255.0 + l, 1.0);
  fragColor = vec4(0.0, g, g, 1.0);
}
)";

// An interceptor drone's camera over its analogue video link: a little soft and pale, colours
// slipping apart, grain, faint lines; with the range (uAmount 0..1) more grain, rows torn
// sideways now and then and thin dropouts. Never the whole picture flashing.
// The view without the cockpit picture, drawn back over it as much as the picture is fading
// (uAmount: how much of the view shows).
inline const char* kFadeFS = R"(#version 330 core
in vec2 vNdc;
uniform sampler2D uImage;
uniform float uAmount;
out vec4 fragColor;
void main() { fragColor = vec4(texture(uImage, vNdc * 0.5 + 0.5).rgb, uAmount); }
)";

inline const char* kDroneFeedFS = R"(#version 330 core
in vec2 vNdc;
uniform sampler2D uImage;
uniform float uTime, uAmount;
uniform vec2 uScreen;
out vec4 fragColor;
float hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }
void main() {
  vec2 uv = vNdc * 0.5 + 0.5;
  float frame = floor(uTime * 25.0);
  // Some 480 lines of picture.
  float row = floor(uv.y * 480.0);
  // Torn rows: a band now and then shifted sideways.
  float band = floor(row / 6.0);
  float tear = step(1.0 - 0.04 * uAmount - 0.004, hash(vec2(band, frame)));
  uv.x += tear * (hash(vec2(band + 7.0, frame)) - 0.5) * (0.01 + 0.04 * uAmount);
  // A slight wobble of the whole line.
  uv.x += sin(row * 0.11 + uTime * 9.0) * 0.0006 * (0.5 + uAmount);
  float px = 1.0 / uScreen.x;
  vec3 c;
  c.r = texture(uImage, uv + vec2(1.5 * px, 0.0)).r;
  c.g = texture(uImage, uv).g;
  c.b = texture(uImage, uv - vec2(1.5 * px, 0.0)).b;
  c = mix(c, texture(uImage, uv + vec2(px, 0.0)).rgb, 0.3);
  // Paler, flatter colours.
  float l = dot(c, vec3(0.30, 0.59, 0.11));
  c = mix(vec3(l), c, 0.7) * 0.9 + 0.04;
  // Grain, per pixel and frame.
  float g = hash(floor(gl_FragCoord.xy / 1.5) + frame * 1.37) - 0.5;
  c += g * (0.07 + 0.2 * uAmount);
  // Faint lines; thin dropouts with the range.
  c *= 0.94 + 0.06 * mod(row, 2.0);
  float drop = step(1.0 - 0.012 * uAmount, hash(vec2(row, frame + 3.0)));
  c = mix(c, vec3(0.5 + 0.3 * hash(vec2(gl_FragCoord.x, frame))), drop * 0.6);
  // Darker towards the corners (a small lens).
  vec2 v = uv - 0.5;
  c *= 1.0 - dot(v, v) * 0.5;
  fragColor = vec4(clamp(c, 0.0, 1.0), 1.0);
}
)";

}  // namespace render::shaders
