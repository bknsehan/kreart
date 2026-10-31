#include "kreart/render/gl_renderer.h"

#include <QOpenGLContext>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <vector>
#include "kreart/core/selection_mask.h"

#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif
#ifndef GL_R8
#define GL_R8 0x8229
#endif
#ifndef GL_RED
#define GL_RED 0x1903
#endif
// GLES2 headers (what Qt is built against) do not declare the GLES3 framebuffer
// blit targets, although glBlitFramebuffer itself is available through
// QOpenGLExtraFunctions at runtime.
#ifndef GL_READ_FRAMEBUFFER
#define GL_READ_FRAMEBUFFER 0x8CA8
#endif
#ifndef GL_DRAW_FRAMEBUFFER
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#endif

namespace kreart::render {

static const char *kCommonVertexShader = R"(
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_texcoord;

uniform mat3 u_transform;

out vec2 v_texcoord;
out vec2 v_doc_pos;

void main() {
    v_texcoord = a_texcoord;
    v_doc_pos = a_pos;
    vec3 ndc = u_transform * vec3(a_pos, 1.0);
    gl_Position = vec4(ndc.xy, 0.0, 1.0);
}
)";

static const char *kCommonFragmentShader = R"(
in vec2 v_texcoord;
in vec2 v_doc_pos;

uniform sampler2D u_texture;
uniform vec2 u_doc_size;
uniform bool u_show_grid;
uniform bool u_is_floating;
uniform bool u_is_dark_theme;
uniform int u_transparency_mode;
uniform vec3 u_transparency_solid_color;
uniform float u_grid_size;
uniform vec4 u_grid_color;
out vec4 FragColor;

void main() {
    vec4 tex = texture(u_texture, v_texcoord);
    if (u_is_floating) {
        FragColor = tex;
        return;
    }
    vec2 check_coord = floor(v_doc_pos / 12.0);
    float check = mod(check_coord.x + check_coord.y, 2.0);
    vec4 check_color;
    if (u_transparency_mode == 1) { // Dark checkerboard
        check_color = (check > 0.5) ? vec4(0.22, 0.23, 0.26, 1.0) : vec4(0.16, 0.17, 0.19, 1.0);
    } else if (u_transparency_mode == 2) { // Solid color
        check_color = vec4(u_transparency_solid_color, 1.0);
    } else { // Light checkerboard (0)
        check_color = (check > 0.5) ? vec4(0.98, 0.98, 0.99, 1.0) : vec4(0.88, 0.89, 0.91, 1.0);
    }

    vec3 blended = tex.rgb * tex.a + check_color.rgb * (1.0 - tex.a);
    vec4 col = vec4(blended, 1.0);
    if (u_show_grid && u_grid_size >= 4.0) {
        vec2 coord = v_doc_pos / u_grid_size;
        vec2 dcoord = max(fwidth(coord), vec2(1e-5));
        vec2 grid = abs(fract(coord - 0.5) - 0.5) / dcoord;
        float line = min(grid.x, grid.y);
        float line_alpha = 1.0 - min(line, 1.0);
        float cell_screen_px = 1.0 / max(dcoord.x, 1e-5);
        float fade = clamp((cell_screen_px - 8.0) / 10.0, 0.0, 1.0);
        col = mix(col, u_grid_color, line_alpha * fade * u_grid_color.a);
    }

    FragColor = col;
}
)";

static const char *kDabVertexShader = R"(
layout(location = 0) in vec2 a_unit_pos;

layout(location = 1) in vec2 i_pos;
layout(location = 2) in vec2 i_radii;
layout(location = 3) in vec3 i_params;
layout(location = 4) in vec4 i_color;

uniform vec2 u_doc_size;

out vec2 v_doc_pos;
out vec2 v_center;
out vec3 v_radii;
out float v_angle;
out float v_flow;
out vec4 v_color;

void main() {
    float major = i_radii.x;
    float minor = i_radii.y;
    float angle = i_params.x;
    float inner = i_params.y;
    float flow = i_params.z;

    vec2 local_offset = vec2(a_unit_pos.x * (major + 1.5), a_unit_pos.y * (minor + 1.5));
    float cos_a = cos(angle);
    float sin_a = sin(angle);
    vec2 rotated_offset = vec2(local_offset.x * cos_a - local_offset.y * sin_a,
                               local_offset.x * sin_a + local_offset.y * cos_a);
    vec2 doc_pos = i_pos + rotated_offset;

    v_doc_pos = doc_pos;
    v_center = i_pos;
    v_radii = vec3(major, minor, inner);
    v_angle = angle;
    v_flow = flow;
    v_color = i_color;

    vec2 ndc = (doc_pos / u_doc_size) * 2.0 - 1.0;
    gl_Position = vec4(ndc.x, ndc.y, 0.0, 1.0);
}
)";

static const char *kDabFragmentShader = R"(
in vec2 v_doc_pos;
in vec2 v_center;
in vec3 v_radii;
in float v_angle;
in float v_flow;
in vec4 v_color;

uniform vec2 u_doc_size;
uniform bool u_has_tip;
uniform sampler2D u_tip_texture;
uniform bool u_has_texture;
uniform sampler2D u_paper_texture;
uniform float u_texture_scale;
uniform float u_texture_strength;
uniform float u_texture_contrast;
uniform float u_texture_brightness;
uniform bool u_texture_invert;
uniform vec2 u_texture_offset;
uniform bool u_has_selection;
uniform sampler2D u_selection_texture;

out vec4 FragColor;

void main() {
    float dx = v_doc_pos.x - v_center.x;
    float dy = v_doc_pos.y - v_center.y;
    float cos_a = cos(v_angle);
    float sin_a = sin(v_angle);
    float along = dx * cos_a + dy * sin_a;
    float across = -dx * sin_a + dy * cos_a;

    float major = max(0.001, v_radii.x);
    float minor = max(0.001, v_radii.y);
    float inner = v_radii.z;

    float norm_sq = (along * along) / (major * major) + (across * across) / (minor * minor);
    if (norm_sq > 1.0) {
        discard;
    }

    float norm = sqrt(norm_sq);
    float dist = norm * major;
    float coverage = 1.0;
    if (dist > inner) {
        float inv_fade = 1.0 / max(0.0001, major - inner);
        coverage = clamp(1.0 - (dist - inner) * inv_fade, 0.0, 1.0);
    }

    if (u_has_tip) {
        vec2 tip_uv = vec2((along / major) * 0.5 + 0.5, (across / minor) * 0.5 + 0.5);
        if (tip_uv.x >= 0.0 && tip_uv.x <= 1.0 && tip_uv.y >= 0.0 && tip_uv.y <= 1.0) {
            coverage *= texture(u_tip_texture, tip_uv).r;
        } else {
            coverage = 0.0;
        }
    }

    if (u_has_texture) {
        vec2 paper_uv = (v_doc_pos + u_texture_offset) / max(1.0, u_texture_scale * 128.0);
        float raw_t = texture(u_paper_texture, paper_uv).r;
        float t = (raw_t - 0.5) * u_texture_contrast + 0.5 + u_texture_brightness;
        t = clamp(t, 0.0, 1.0);
        if (u_texture_invert) {
            t = 1.0 - t;
        }
        coverage *= mix(1.0, t, u_texture_strength);
    }

    if (u_has_selection) {
        vec2 sel_uv = v_doc_pos / u_doc_size;
        float sel_mask = texture(u_selection_texture, sel_uv).r;
        coverage *= sel_mask;
    }

    float alpha = v_color.a * v_flow * coverage;
    if (alpha <= 0.0001) {
        discard;
    }

    FragColor = vec4(v_color.rgb * alpha, alpha);
}
)";

static const char *kFullscreenVertexShader = R"(
layout(location = 0) in vec2 a_pos;
out vec2 v_texcoord;

void main() {
    v_texcoord = a_pos * 0.5 + 0.5;
    gl_Position = vec4(a_pos, 0.0, 1.0);
}
)";

static const char *kCompositeFragmentShader = R"(
in vec2 v_texcoord;
out vec4 FragColor;

uniform sampler2D u_dst;
uniform sampler2D u_layer;
uniform sampler2D u_stroke;
uniform bool u_is_first_layer;
uniform bool u_has_stroke;
uniform int u_stroke_mode;
uniform float u_stroke_opacity;
uniform int u_stroke_blend_mode;
uniform float u_layer_opacity;
uniform int u_blend_mode;
uniform bool u_clipping_mask;
uniform bool u_screentone;
uniform float u_screentone_freq;
uniform float u_screentone_angle;
uniform int u_screentone_pat;
uniform vec2 u_doc_size;

vec4 apply_screentone(vec4 src, vec2 doc_pos) {
    if (src.a <= 0.0) return vec4(0.0);
    float rad = radians(u_screentone_angle);
    float cos_a = cos(rad);
    float sin_a = sin(rad);
    float u = doc_pos.x * cos_a - doc_pos.y * sin_a;
    float v = doc_pos.x * sin_a + doc_pos.y * cos_a;

    float eff_freq = u_screentone_freq;
    if (u_screentone_pat == 1) eff_freq = 50.0;
    else if (u_screentone_pat == 2) eff_freq = 40.0;
    else if (u_screentone_pat == 3) eff_freq = 80.0;
    else if (u_screentone_pat == 0) eff_freq = 60.0;

    float period = max(2.0, 100.0 / max(5.0, eff_freq));
    float gray = 1.0 - (0.299 * src.r + 0.587 * src.g + 0.114 * src.b);
    float weight = gray * src.a;

    if (u_screentone_pat == 4) { // LINE_45
        float cu = mod(mod(u, period) + period, period);
        if (cu <= period * clamp(weight, 0.0, 1.0)) {
            return vec4(src.rgb, 1.0);
        }
        return vec4(0.0);
    }
    if (u_screentone_pat == 5) { // CROSSHATCH
        float cu = mod(mod(u, period) + period, period);
        float cv = mod(mod(v, period) + period, period);
        float w_half = period * clamp(weight * 0.5, 0.0, 0.5);
        if (cu <= w_half || cv <= w_half) {
            return vec4(src.rgb, 1.0);
        }
        return vec4(0.0);
    }

    // DOT patterns
    float cu = mod(mod(u, period) + period, period) - period * 0.5;
    float cv = mod(mod(v, period) + period, period) - period * 0.5;
    float dist = length(vec2(cu, cv));
    float max_r = period * 0.70710678;
    float dot_r = max_r * sqrt(clamp(weight, 0.0, 1.0));
    if (dist <= dot_r) {
        return vec4(src.rgb, 1.0);
    }
    return vec4(0.0);
}

void main() {
    vec4 dst = u_is_first_layer ? vec4(0.0) : texture(u_dst, v_texcoord);
    vec4 src = texture(u_layer, v_texcoord);

    if (u_has_stroke) {
        vec4 stroke = texture(u_stroke, v_texcoord);
        stroke.a *= clamp(u_stroke_opacity, 0.0, 1.0);
        if (u_stroke_mode == 1) { // ERASE
            src.rgb *= (1.0 - stroke.a);
            src.a *= (1.0 - stroke.a);
        } else { // PAINT or SMUDGE
            if (stroke.a > 0.001) {
                if (src.a <= 0.001) {
                    src = vec4(stroke.rgb, stroke.a);
                } else {
                    vec3 br = stroke.rgb;
                    if (u_stroke_blend_mode == 1) br = stroke.rgb * src.rgb;
                    else if (u_stroke_blend_mode == 2) br = 1.0 - (1.0 - stroke.rgb) * (1.0 - src.rgb);
                    else if (u_stroke_blend_mode == 3) br = min(vec3(1.0), stroke.rgb + src.rgb);
                    else if (u_stroke_blend_mode == 4) {
                        for (int i = 0; i < 3; ++i) br[i] = (src.rgb[i] < 0.5) ? (2.0 * stroke.rgb[i] * src.rgb[i]) : (1.0 - 2.0 * (1.0 - stroke.rgb[i]) * (1.0 - src.rgb[i]));
                    }
                    else if (u_stroke_blend_mode == 5) {
                        for (int i = 0; i < 3; ++i) br[i] = (1.0 - 2.0 * stroke.rgb[i]) * src.rgb[i] * src.rgb[i] + 2.0 * stroke.rgb[i] * src.rgb[i];
                    }
                    else if (u_stroke_blend_mode == 6) br = min(stroke.rgb, src.rgb);
                    else if (u_stroke_blend_mode == 7) br = max(stroke.rgb, src.rgb);
                    else if (u_stroke_blend_mode == 8) {
                        for (int i = 0; i < 3; ++i) br[i] = (stroke.rgb[i] <= 0.0) ? 0.0 : max(0.0, 1.0 - (1.0 - src.rgb[i]) / stroke.rgb[i]);
                    }
                    else if (u_stroke_blend_mode == 9) {
                        for (int i = 0; i < 3; ++i) br[i] = (stroke.rgb[i] >= 1.0) ? 1.0 : min(1.0, src.rgb[i] / (1.0 - stroke.rgb[i]));
                    }
                    float out_a = stroke.a + src.a * (1.0 - stroke.a);
                    if (out_a > 0.001) {
                        vec3 blended_rgb = (br * stroke.a + src.rgb * src.a * (1.0 - stroke.a)) / out_a;
                        src = vec4(clamp(blended_rgb, 0.0, 1.0), clamp(out_a, 0.0, 1.0));
                    }
                }
            }
        }
    }

    if (u_screentone) {
        src = apply_screentone(src, v_texcoord * u_doc_size);
    }

    float clip_alpha = 1.0;
    if (u_clipping_mask) {
        clip_alpha = dst.a;
    }

    float effective_sa = src.a * u_layer_opacity * clip_alpha;
    if (effective_sa <= 0.001) {
        FragColor = dst;
        return;
    }

    if (dst.a <= 0.001) {
        FragColor = vec4(src.rgb, effective_sa);
        return;
    }

    float da = dst.a;
    float out_a = effective_sa + da * (1.0 - effective_sa);
    if (out_a <= 0.001) {
        FragColor = vec4(0.0);
        return;
    }

    vec3 sr = src.rgb;
    vec3 dr = dst.rgb;
    vec3 br = sr;

    if (u_blend_mode == 1) { // multiply
        br = sr * dr;
    } else if (u_blend_mode == 2) { // screen
        br = 1.0 - (1.0 - sr) * (1.0 - dr);
    } else if (u_blend_mode == 3) { // add / linear_dodge
        br = min(vec3(1.0), sr + dr);
    } else if (u_blend_mode == 4) { // overlay
        for (int i = 0; i < 3; ++i) {
            br[i] = (dr[i] < 0.5) ? (2.0 * sr[i] * dr[i]) : (1.0 - 2.0 * (1.0 - sr[i]) * (1.0 - dr[i]));
        }
    } else if (u_blend_mode == 5) { // soft_light
        for (int i = 0; i < 3; ++i) {
            br[i] = (1.0 - 2.0 * sr[i]) * dr[i] * dr[i] + 2.0 * sr[i] * dr[i];
        }
    } else if (u_blend_mode == 6) { // darken
        br = min(sr, dr);
    } else if (u_blend_mode == 7) { // lighten
        br = max(sr, dr);
    } else if (u_blend_mode == 8) { // color_burn
        for (int i = 0; i < 3; ++i) {
            br[i] = (sr[i] <= 0.0) ? 0.0 : max(0.0, 1.0 - (1.0 - dr[i]) / sr[i]);
        }
    } else if (u_blend_mode == 9) { // color_dodge
        for (int i = 0; i < 3; ++i) {
            br[i] = (sr[i] >= 1.0) ? 1.0 : min(1.0, dr[i] / (1.0 - sr[i]));
        }
    }

    vec3 out_rgb = (br * effective_sa + dr * da * (1.0 - effective_sa)) / out_a;
    FragColor = vec4(clamp(out_rgb, 0.0, 1.0), clamp(out_a, 0.0, 1.0));
}
)";

static const char *kBakeFragmentShader = R"(
in vec2 v_texcoord;
out vec4 FragColor;

uniform sampler2D u_layer;
uniform sampler2D u_stroke;
uniform int u_stroke_mode;
uniform float u_stroke_opacity;
uniform int u_stroke_blend_mode;

void main() {
    vec4 src = texture(u_layer, v_texcoord);
    vec4 stroke = texture(u_stroke, v_texcoord);
    stroke.a *= clamp(u_stroke_opacity, 0.0, 1.0);

    if (u_stroke_mode == 1) { // ERASE
        src.rgb *= (1.0 - stroke.a);
        src.a *= (1.0 - stroke.a);
        FragColor = src;
    } else { // PAINT or SMUDGE
        if (stroke.a <= 0.001) {
            FragColor = src;
            return;
        }
        if (src.a <= 0.001) {
            FragColor = vec4(stroke.rgb, stroke.a);
            return;
        }

        vec3 br = stroke.rgb;
        if (u_stroke_blend_mode == 1) br = stroke.rgb * src.rgb;
        else if (u_stroke_blend_mode == 2) br = 1.0 - (1.0 - stroke.rgb) * (1.0 - src.rgb);
        else if (u_stroke_blend_mode == 3) br = min(vec3(1.0), stroke.rgb + src.rgb);
        else if (u_stroke_blend_mode == 4) {
            for (int i = 0; i < 3; ++i) br[i] = (src.rgb[i] < 0.5) ? (2.0 * stroke.rgb[i] * src.rgb[i]) : (1.0 - 2.0 * (1.0 - stroke.rgb[i]) * (1.0 - src.rgb[i]));
        }
        else if (u_stroke_blend_mode == 5) {
            for (int i = 0; i < 3; ++i) br[i] = (1.0 - 2.0 * stroke.rgb[i]) * src.rgb[i] * src.rgb[i] + 2.0 * stroke.rgb[i] * src.rgb[i];
        }
        else if (u_stroke_blend_mode == 6) br = min(stroke.rgb, src.rgb);
        else if (u_stroke_blend_mode == 7) br = max(stroke.rgb, src.rgb);
        else if (u_stroke_blend_mode == 8) {
            for (int i = 0; i < 3; ++i) br[i] = (stroke.rgb[i] <= 0.0) ? 0.0 : max(0.0, 1.0 - (1.0 - src.rgb[i]) / stroke.rgb[i]);
        }
        else if (u_stroke_blend_mode == 9) {
            for (int i = 0; i < 3; ++i) br[i] = (stroke.rgb[i] >= 1.0) ? 1.0 : min(1.0, src.rgb[i] / (1.0 - stroke.rgb[i]));
        }

        float out_a = stroke.a + src.a * (1.0 - stroke.a);
        if (out_a > 0.001) {
            vec3 blended_rgb = (br * stroke.a + src.rgb * src.a * (1.0 - stroke.a)) / out_a;
            FragColor = vec4(clamp(blended_rgb, 0.0, 1.0), clamp(out_a, 0.0, 1.0));
        } else {
            FragColor = vec4(0.0);
        }
    }
}
)";

int OpenGLRenderer::blend_mode_to_int(const std::string &op) {
  if (op == "multiply") return 1;
  if (op == "screen") return 2;
  if (op == "add" || op == "linear_dodge") return 3;
  if (op == "overlay") return 4;
  if (op == "soft_light") return 5;
  if (op == "darken") return 6;
  if (op == "lighten") return 7;
  if (op == "color_burn") return 8;
  if (op == "color_dodge") return 9;
  return 0; // normal
}

static int screentone_pattern_to_int(core::ScreentonePattern pat) {
  switch (pat) {
    case core::ScreentonePattern::DOT_60: return 0;
    case core::ScreentonePattern::DOT_50: return 1;
    case core::ScreentonePattern::DOT_40: return 2;
    case core::ScreentonePattern::DOT_80: return 3;
    case core::ScreentonePattern::LINE_45: return 4;
    case core::ScreentonePattern::CROSSHATCH: return 5;
  }
  return 0;
}

OpenGLRenderer::~OpenGLRenderer() {
  shutdown();
}

bool OpenGLRenderer::init() {
  if (initialized_) {
    return true;
  }
  initializeOpenGLFunctions();
  if (!compile_shaders()) {
    return false;
  }
  init_quad();
  init_stroke_resources();
  init_composite_resources();

  glGenTextures(1, &texture_id_);
  glBindTexture(GL_TEXTURE_2D, texture_id_);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glBindTexture(GL_TEXTURE_2D, 0);

  full_reload_ = true;
  initialized_ = true;
  return true;
}

void OpenGLRenderer::shutdown() {
  if (!initialized_) {
    return;
  }
  if (QOpenGLContext::currentContext()) {
    initializeOpenGLFunctions();
    if (texture_id_ != 0) {
      glDeleteTextures(1, &texture_id_);
    }
    if (vbo_ != 0) {
      glDeleteBuffers(1, &vbo_);
    }
    if (vao_ != 0) {
      glDeleteVertexArrays(1, &vao_);
    }
    if (program_ != 0) {
      glDeleteProgram(program_);
    }
    if (floating_texture_id_ != 0) {
      glDeleteTextures(1, &floating_texture_id_);
    }
    if (floating_vbo_ != 0) {
      glDeleteBuffers(1, &floating_vbo_);
    }
    if (stroke_fbo_ != 0) {
      glDeleteFramebuffers(1, &stroke_fbo_);
    }
    if (stroke_texture_ != 0) {
      glDeleteTextures(1, &stroke_texture_);
    }
    if (stroke_vbo_ != 0) {
      glDeleteBuffers(1, &stroke_vbo_);
    }
    if (stroke_instance_vbo_ != 0) {
      glDeleteBuffers(1, &stroke_instance_vbo_);
    }
    if (stroke_vao_ != 0) {
      glDeleteVertexArrays(1, &stroke_vao_);
    }
    if (dab_program_ != 0) {
      glDeleteProgram(dab_program_);
    }
    if (composite_program_ != 0) {
      glDeleteProgram(composite_program_);
    }
    if (bake_program_ != 0) {
      glDeleteProgram(bake_program_);
    }
    if (fullscreen_vbo_ != 0) {
      glDeleteBuffers(1, &fullscreen_vbo_);
    }
    if (fullscreen_vao_ != 0) {
      glDeleteVertexArrays(1, &fullscreen_vao_);
    }
    for (int i = 0; i < 2; ++i) {
      if (comp_fbo_[i] != 0) glDeleteFramebuffers(1, &comp_fbo_[i]);
      if (comp_tex_[i] != 0) glDeleteTextures(1, &comp_tex_[i]);
      comp_fbo_[i] = 0;
      comp_tex_[i] = 0;
    }
    if (blit_fbo_ != 0) {
      glDeleteFramebuffers(1, &blit_fbo_);
      blit_fbo_ = 0;
    }
    if (capture_fbo_ != 0) {
      glDeleteFramebuffers(1, &capture_fbo_);
      capture_fbo_ = 0;
    }
    if (capture_tex_ != 0) {
      glDeleteTextures(1, &capture_tex_);
      capture_tex_ = 0;
      capture_tex_w_ = 0;
      capture_tex_h_ = 0;
    }
    if (bake_fbo_ != 0) {
      glDeleteFramebuffers(1, &bake_fbo_);
      bake_fbo_ = 0;
    }
    if (tip_texture_ != 0) {
      glDeleteTextures(1, &tip_texture_);
      tip_texture_ = 0;
    }
    if (paper_texture_ != 0) {
      glDeleteTextures(1, &paper_texture_);
      paper_texture_ = 0;
    }
    if (selection_texture_ != 0) {
      glDeleteTextures(1, &selection_texture_);
      selection_texture_ = 0;
    }
    for (auto &kv : layer_textures_) {
      if (kv.second.texture != 0) {
        glDeleteTextures(1, &kv.second.texture);
      }
    }
    layer_textures_.clear();
  }
  texture_id_ = 0;
  vbo_ = 0;
  vao_ = 0;
  program_ = 0;
  floating_texture_id_ = 0;
  floating_vbo_ = 0;
  floating_uploaded_ = false;
  floating_width_ = 0;
  floating_height_ = 0;
  texture_width_ = 0;
  texture_height_ = 0;
  stroke_fbo_ = 0;
  stroke_texture_ = 0;
  stroke_vbo_ = 0;
  stroke_instance_vbo_ = 0;
  stroke_vao_ = 0;
  stroke_width_ = 0;
  stroke_height_ = 0;
  dab_program_ = 0;
  composite_program_ = 0;
  bake_program_ = 0;
  fullscreen_vao_ = 0;
  fullscreen_vbo_ = 0;
  comp_width_ = 0;
  comp_height_ = 0;
  tip_width_ = 0;
  tip_height_ = 0;
  paper_width_ = 0;
  paper_height_ = 0;
  selection_width_ = 0;
  selection_height_ = 0;
  initialized_ = false;
}

bool OpenGLRenderer::compile_shaders() {
  auto compile_single = [this](GLenum type, const char *src) -> GLuint {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    GLint status = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (!status) {
      char log[512];
      glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
      std::fprintf(stderr, "Kreart GL shader compile error: %s\n", log);
      glDeleteShader(shader);
      return 0;
    }
    return shader;
  };

  QOpenGLContext *ctx = QOpenGLContext::currentContext();
  const bool is_gles = ctx ? ctx->isOpenGLES() : false;
  const std::string header = is_gles ? "#version 300 es\nprecision highp float;\n" : "#version 330 core\n";
  const std::string vs_full = header + kCommonVertexShader;
  const std::string fs_full = header + kCommonFragmentShader;

  GLuint vs = compile_single(GL_VERTEX_SHADER, vs_full.c_str());
  if (vs == 0) return false;
  GLuint fs = compile_single(GL_FRAGMENT_SHADER, fs_full.c_str());
  if (fs == 0) {
    glDeleteShader(vs);
    return false;
  }

  program_ = glCreateProgram();
  glAttachShader(program_, vs);
  glAttachShader(program_, fs);
  glLinkProgram(program_);
  glDeleteShader(vs);
  glDeleteShader(fs);

  GLint link_status = 0;
  glGetProgramiv(program_, GL_LINK_STATUS, &link_status);
  if (!link_status) {
    char log[512];
    glGetProgramInfoLog(program_, sizeof(log), nullptr, log);
    std::fprintf(stderr, "Kreart GL program link error: %s\n", log);
    glDeleteProgram(program_);
    program_ = 0;
    return false;
  }

  u_transform_loc_ = glGetUniformLocation(program_, "u_transform");
  u_doc_size_loc_ = glGetUniformLocation(program_, "u_doc_size");
  u_show_grid_loc_ = glGetUniformLocation(program_, "u_show_grid");
  u_grid_size_loc_ = glGetUniformLocation(program_, "u_grid_size");
  u_grid_color_loc_ = glGetUniformLocation(program_, "u_grid_color");
  u_texture_loc_ = glGetUniformLocation(program_, "u_texture");
  u_is_floating_loc_ = glGetUniformLocation(program_, "u_is_floating");
  u_is_dark_theme_loc_ = glGetUniformLocation(program_, "u_is_dark_theme");
  u_transparency_mode_loc_ = glGetUniformLocation(program_, "u_transparency_mode");
  u_transparency_solid_color_loc_ = glGetUniformLocation(program_, "u_transparency_solid_color");

  // Compile Dab shader program
  const std::string dab_vs_full = header + kDabVertexShader;
  const std::string dab_fs_full = header + kDabFragmentShader;
  GLuint dab_vs = compile_single(GL_VERTEX_SHADER, dab_vs_full.c_str());
  if (dab_vs == 0) return false;
  GLuint dab_fs = compile_single(GL_FRAGMENT_SHADER, dab_fs_full.c_str());
  if (dab_fs == 0) {
    glDeleteShader(dab_vs);
    return false;
  }
  dab_program_ = glCreateProgram();
  glAttachShader(dab_program_, dab_vs);
  glAttachShader(dab_program_, dab_fs);
  glLinkProgram(dab_program_);
  glDeleteShader(dab_vs);
  glDeleteShader(dab_fs);
  GLint dab_link = 0;
  glGetProgramiv(dab_program_, GL_LINK_STATUS, &dab_link);
  if (!dab_link) {
    char log[512];
    glGetProgramInfoLog(dab_program_, sizeof(log), nullptr, log);
    std::fprintf(stderr, "Kreart GL dab program link error: %s\n", log);
    glDeleteProgram(dab_program_);
    dab_program_ = 0;
    return false;
  }
  dab_u_doc_size_loc_ = glGetUniformLocation(dab_program_, "u_doc_size");
  dab_u_has_tip_loc_ = glGetUniformLocation(dab_program_, "u_has_tip");
  dab_u_tip_texture_loc_ = glGetUniformLocation(dab_program_, "u_tip_texture");
  dab_u_has_texture_loc_ = glGetUniformLocation(dab_program_, "u_has_texture");
  dab_u_paper_texture_loc_ = glGetUniformLocation(dab_program_, "u_paper_texture");
  dab_u_texture_scale_loc_ = glGetUniformLocation(dab_program_, "u_texture_scale");
  dab_u_texture_strength_loc_ = glGetUniformLocation(dab_program_, "u_texture_strength");
  dab_u_texture_contrast_loc_ = glGetUniformLocation(dab_program_, "u_texture_contrast");
  dab_u_texture_brightness_loc_ = glGetUniformLocation(dab_program_, "u_texture_brightness");
  dab_u_texture_invert_loc_ = glGetUniformLocation(dab_program_, "u_texture_invert");
  dab_u_texture_offset_loc_ = glGetUniformLocation(dab_program_, "u_texture_offset");
  dab_u_has_selection_loc_ = glGetUniformLocation(dab_program_, "u_has_selection");
  dab_u_selection_texture_loc_ = glGetUniformLocation(dab_program_, "u_selection_texture");

  // Compile Fullscreen composite program
  const std::string fs_vert_full = header + kFullscreenVertexShader;
  const std::string comp_fs_full = header + kCompositeFragmentShader;
  GLuint comp_vs = compile_single(GL_VERTEX_SHADER, fs_vert_full.c_str());
  if (comp_vs == 0) return false;
  GLuint comp_fs = compile_single(GL_FRAGMENT_SHADER, comp_fs_full.c_str());
  if (comp_fs == 0) {
    glDeleteShader(comp_vs);
    return false;
  }
  composite_program_ = glCreateProgram();
  glAttachShader(composite_program_, comp_vs);
  glAttachShader(composite_program_, comp_fs);
  glLinkProgram(composite_program_);
  glDeleteShader(comp_vs);
  glDeleteShader(comp_fs);
  GLint comp_link = 0;
  glGetProgramiv(composite_program_, GL_LINK_STATUS, &comp_link);
  if (!comp_link) {
    char log[512];
    glGetProgramInfoLog(composite_program_, sizeof(log), nullptr, log);
    std::fprintf(stderr, "Kreart GL composite program link error: %s\n", log);
    glDeleteProgram(composite_program_);
    composite_program_ = 0;
    return false;
  }
  comp_u_dst_loc_ = glGetUniformLocation(composite_program_, "u_dst");
  comp_u_layer_loc_ = glGetUniformLocation(composite_program_, "u_layer");
  comp_u_stroke_loc_ = glGetUniformLocation(composite_program_, "u_stroke");
  comp_u_is_first_layer_loc_ = glGetUniformLocation(composite_program_, "u_is_first_layer");
  comp_u_has_stroke_loc_ = glGetUniformLocation(composite_program_, "u_has_stroke");
  comp_u_stroke_mode_loc_ = glGetUniformLocation(composite_program_, "u_stroke_mode");
  comp_u_layer_opacity_loc_ = glGetUniformLocation(composite_program_, "u_layer_opacity");
  comp_u_blend_mode_loc_ = glGetUniformLocation(composite_program_, "u_blend_mode");
  comp_u_clipping_mask_loc_ = glGetUniformLocation(composite_program_, "u_clipping_mask");
  comp_u_screentone_loc_ = glGetUniformLocation(composite_program_, "u_screentone");
  comp_u_screentone_freq_loc_ = glGetUniformLocation(composite_program_, "u_screentone_freq");
  comp_u_screentone_angle_loc_ = glGetUniformLocation(composite_program_, "u_screentone_angle");
  comp_u_screentone_pat_loc_ = glGetUniformLocation(composite_program_, "u_screentone_pat");
  comp_u_doc_size_loc_ = glGetUniformLocation(composite_program_, "u_doc_size");
  comp_u_stroke_opacity_loc_ = glGetUniformLocation(composite_program_, "u_stroke_opacity");
  comp_u_stroke_blend_mode_loc_ = glGetUniformLocation(composite_program_, "u_stroke_blend_mode");

  // Compile Bake program
  const std::string bake_fs_full = header + kBakeFragmentShader;
  GLuint bake_vs = compile_single(GL_VERTEX_SHADER, fs_vert_full.c_str());
  if (bake_vs == 0) return false;
  GLuint bake_fs = compile_single(GL_FRAGMENT_SHADER, bake_fs_full.c_str());
  if (bake_fs == 0) {
    glDeleteShader(bake_vs);
    return false;
  }
  bake_program_ = glCreateProgram();
  glAttachShader(bake_program_, bake_vs);
  glAttachShader(bake_program_, bake_fs);
  glLinkProgram(bake_program_);
  glDeleteShader(bake_vs);
  glDeleteShader(bake_fs);
  GLint bake_link = 0;
  glGetProgramiv(bake_program_, GL_LINK_STATUS, &bake_link);
  if (!bake_link) {
    char log[512];
    glGetProgramInfoLog(bake_program_, sizeof(log), nullptr, log);
    std::fprintf(stderr, "Kreart GL bake program link error: %s\n", log);
    glDeleteProgram(bake_program_);
    bake_program_ = 0;
    return false;
  }
  bake_u_layer_loc_ = glGetUniformLocation(bake_program_, "u_layer");
  bake_u_stroke_loc_ = glGetUniformLocation(bake_program_, "u_stroke");
  bake_u_stroke_mode_loc_ = glGetUniformLocation(bake_program_, "u_stroke_mode");
  bake_u_stroke_opacity_loc_ = glGetUniformLocation(bake_program_, "u_stroke_opacity");
  bake_u_stroke_blend_mode_loc_ = glGetUniformLocation(bake_program_, "u_stroke_blend_mode");

  return true;
}

void OpenGLRenderer::init_quad() {
  glGenVertexArrays(1, &vao_);
  glGenBuffers(1, &vbo_);

  glBindVertexArray(vao_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  // Allocate buffer for 4 vertices x 4 floats (pos_x, pos_y, tex_u, tex_v)
  glBufferData(GL_ARRAY_BUFFER, 16 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);

  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void *>(0));

  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                        reinterpret_cast<void *>(2 * sizeof(float)));

  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindVertexArray(0);
  glGenBuffers(1, &floating_vbo_);
  glBindBuffer(GL_ARRAY_BUFFER, floating_vbo_);
  glBufferData(GL_ARRAY_BUFFER, 16 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
  glBindBuffer(GL_ARRAY_BUFFER, 0);

  glGenTextures(1, &floating_texture_id_);
  glBindTexture(GL_TEXTURE_2D, floating_texture_id_);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glBindTexture(GL_TEXTURE_2D, 0);
  floating_uploaded_ = false;
}

void OpenGLRenderer::sync(core::TiledImage &image, bool pixelated) {
  if (!initialized_ || texture_id_ == 0) {
    return;
  }

  glBindTexture(GL_TEXTURE_2D, texture_id_);

  if (pixelated != last_pixelated_) {
    last_pixelated_ = pixelated;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, pixelated ? GL_NEAREST : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, pixelated ? GL_NEAREST : GL_LINEAR);
  }

  const bool size_changed = (texture_width_ != image.width() || texture_height_ != image.height());
  if (size_changed || full_reload_) {
    texture_width_ = image.width();
    texture_height_ = image.height();
    full_reload_ = false;

    // Allocate initial empty texture storage
    std::vector<uint32_t> blank(static_cast<std::size_t>(texture_width_) * texture_height_, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, texture_width_, texture_height_, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, blank.data());

    // Upload all present tiles
    for (const auto &[key, tile] : image.tiles()) {
      const int tx = core::TiledImage::key_x(key);
      const int ty = core::TiledImage::key_y(key);
      const int tw = std::min(core::kTileSize, image.width() - tx * core::kTileSize);
      const int th = std::min(core::kTileSize, image.height() - ty * core::kTileSize);
      if (tw <= 0 || th <= 0) continue;

      if (tw == core::kTileSize && th == core::kTileSize) {
        glTexSubImage2D(GL_TEXTURE_2D, 0, tx * core::kTileSize, ty * core::kTileSize,
                        core::kTileSize, core::kTileSize, GL_RGBA, GL_UNSIGNED_BYTE,
                        tile->pixels.data());
      } else {
        std::vector<core::Rgba8> cropped(static_cast<std::size_t>(tw) * th);
        for (int ly = 0; ly < th; ++ly) {
          for (int lx = 0; lx < tw; ++lx) {
            cropped[ly * tw + lx] = tile->pixels[ly * core::kTileSize + lx];
          }
        }
        glTexSubImage2D(GL_TEXTURE_2D, 0, tx * core::kTileSize, ty * core::kTileSize,
                        tw, th, GL_RGBA, GL_UNSIGNED_BYTE, cropped.data());
      }
    }
  } else {
    // Incremental upload: only push dirty tiles
    for (std::uint64_t key : image.dirty_tiles()) {
      const int tx = core::TiledImage::key_x(key);
      const int ty = core::TiledImage::key_y(key);
      const int tw = std::min(core::kTileSize, image.width() - tx * core::kTileSize);
      const int th = std::min(core::kTileSize, image.height() - ty * core::kTileSize);
      if (tw <= 0 || th <= 0) continue;

      const auto *tile = image.tile_at(tx, ty);
      if (tile != nullptr) {
        if (tw == core::kTileSize && th == core::kTileSize) {
          glTexSubImage2D(GL_TEXTURE_2D, 0, tx * core::kTileSize, ty * core::kTileSize,
                          core::kTileSize, core::kTileSize, GL_RGBA, GL_UNSIGNED_BYTE,
                          tile->pixels.data());
        } else {
          std::vector<core::Rgba8> cropped(static_cast<std::size_t>(tw) * th);
          for (int ly = 0; ly < th; ++ly) {
            for (int lx = 0; lx < tw; ++lx) {
              cropped[ly * tw + lx] = tile->pixels[ly * core::kTileSize + lx];
            }
          }
          glTexSubImage2D(GL_TEXTURE_2D, 0, tx * core::kTileSize, ty * core::kTileSize,
                          tw, th, GL_RGBA, GL_UNSIGNED_BYTE, cropped.data());
        }
      } else {
        std::vector<std::uint32_t> blank(static_cast<std::size_t>(tw) * th, 0);
        glTexSubImage2D(GL_TEXTURE_2D, 0, tx * core::kTileSize, ty * core::kTileSize,
                        tw, th, GL_RGBA, GL_UNSIGNED_BYTE, blank.data());
      }
    }
  }

  // Dirty tiles are consumed by the active display backend. Leaving them set
  // makes every later GL frame re-upload tiles that have not changed.
  image.clear_dirty();

  glBindTexture(GL_TEXTURE_2D, 0);
}
void OpenGLRenderer::sync_floating_selection(bool active, int width, int height,
                                             const core::Rgba8 *pixels, const std::uint8_t *mask,
                                             bool pixelated) {
  if (!initialized_) return;
  if (!active || pixels == nullptr || width <= 0 || height <= 0) {
    floating_uploaded_ = false;
    return;
  }

  if (floating_texture_id_ == 0) {
    glGenTextures(1, &floating_texture_id_);
    glBindTexture(GL_TEXTURE_2D, floating_texture_id_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, pixelated ? GL_NEAREST : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, pixelated ? GL_NEAREST : GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
  }

  // Update filter if pixelated mode changed, even without re-upload
  if (floating_uploaded_ && floating_width_ == width && floating_height_ == height) {
    // still update filter
    glBindTexture(GL_TEXTURE_2D, floating_texture_id_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, pixelated ? GL_NEAREST : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, pixelated ? GL_NEAREST : GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
    return;
  }

  floating_width_ = width;
  floating_height_ = height;

  std::vector<core::Rgba8> masked_pixels(static_cast<std::size_t>(width) * height);
  for (std::size_t i = 0; i < masked_pixels.size(); ++i) {
    masked_pixels[i] = pixels[i];
    if (mask != nullptr) {
      masked_pixels[i].a = static_cast<std::uint8_t>(
          (static_cast<int>(pixels[i].a) * mask[i]) / 255);
    }
  }

  glBindTexture(GL_TEXTURE_2D, floating_texture_id_);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, pixelated ? GL_NEAREST : GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, pixelated ? GL_NEAREST : GL_LINEAR);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               masked_pixels.data());
  glBindTexture(GL_TEXTURE_2D, 0);

  floating_uploaded_ = true;
}


void OpenGLRenderer::render(int viewport_w, int viewport_h,
                            double pivot_x, double pivot_y,
                            double view_scale, double view_rotation,
                            bool mirror_h, bool mirror_v,
                            int doc_w, int doc_h,
                            bool pixelated,
                            bool show_grid,
                            int grid_size,
                            core::Rgba8 grid_color,
                            const FloatingSelectionRenderInfo &fs_info,
                            bool is_dark_theme,
                            int transparency_mode,
                            core::Rgba8 transparency_solid_color) {
  if (!initialized_ || program_ == 0 || vao_ == 0 || texture_id_ == 0) {
    return;
  }
  if (viewport_w <= 0 || viewport_h <= 0 || doc_w <= 0 || doc_h <= 0) {
    return;
  }
  if (pixelated != last_pixelated_) {
    last_pixelated_ = pixelated;
    glBindTexture(GL_TEXTURE_2D, texture_id_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, pixelated ? GL_NEAREST : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, pixelated ? GL_NEAREST : GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
  }
  glViewport(0, 0, viewport_w, viewport_h);

  // Clear workspace background to Kreart's neutral dark theme #111215
  if (is_dark_theme) {
    glClearColor(0.078f, 0.082f, 0.094f, 1.0f); // #141518
  } else {
    glClearColor(0.886f, 0.894f, 0.910f, 1.0f); // #e2e4e8
  }
  glClear(GL_COLOR_BUFFER_BIT);

  glUseProgram(program_);

  // Update vertex buffer with document coordinates
  const float w = static_cast<float>(doc_w);
  const float h = static_cast<float>(doc_h);
  const float vertices[16] = {
      // pos_x, pos_y, tex_u, tex_v
      0.0f, 0.0f, 0.0f, 0.0f,
      w,    0.0f, 1.0f, 0.0f,
      w,    h,    1.0f, 1.0f,
      0.0f, h,    0.0f, 1.0f
  };

  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
  glBindBuffer(GL_ARRAY_BUFFER, 0);

  // Compute 3x3 Affine MVP matrix
  const double mult_x = mirror_h ? -1.0 : 1.0;
  const double mult_y = mirror_v ? -1.0 : 1.0;
  const double cos_r = std::cos(view_rotation);
  const double sin_r = std::sin(view_rotation);

  const float A = static_cast<float>((view_scale * mult_x * cos_r * 2.0) / viewport_w);
  const float B = static_cast<float>(-(view_scale * mult_y * sin_r * 2.0) / viewport_w);
  const float C = static_cast<float>((pivot_x * 2.0) / viewport_w - 1.0 - A * (doc_w * 0.5) - B * (doc_h * 0.5));

  const float D = static_cast<float>(-(view_scale * mult_x * sin_r * 2.0) / viewport_h);
  const float E = static_cast<float>(-(view_scale * mult_y * cos_r * 2.0) / viewport_h);
  const float F = static_cast<float>(1.0 - (pivot_y * 2.0) / viewport_h - D * (doc_w * 0.5) - E * (doc_h * 0.5));

  const float mat[9] = {
      A,    D,    0.0f,
      B,    E,    0.0f,
      C,    F,    1.0f
  };

  glUniform1i(u_is_floating_loc_, 0);
  glUniform1i(u_is_dark_theme_loc_, is_dark_theme ? 1 : 0);
  glUniformMatrix3fv(u_transform_loc_, 1, GL_FALSE, mat);
  glUniform1i(u_transparency_mode_loc_, transparency_mode);
  glUniform3f(u_transparency_solid_color_loc_,
              transparency_solid_color.r / 255.0f,
              transparency_solid_color.g / 255.0f,
              transparency_solid_color.b / 255.0f);
  glUniform2f(u_doc_size_loc_, w, h);
  glUniform1i(u_show_grid_loc_, (show_grid && (grid_size * view_scale >= 4.0)) ? 1 : 0);
  glUniform1f(u_grid_size_loc_, static_cast<float>(grid_size));
  glUniform4f(u_grid_color_loc_, grid_color.r / 255.0f, grid_color.g / 255.0f,
              grid_color.b / 255.0f, grid_color.a / 255.0f);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture_id_);
  glUniform1i(u_texture_loc_, 0);

  glBindVertexArray(vao_);
  glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

  if (fs_info.active && floating_uploaded_ && floating_texture_id_ != 0 &&
      fs_info.width > 0 && fs_info.height > 0) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUniform1i(u_is_floating_loc_, 1);

    const float fw = static_cast<float>(fs_info.width);
    const float fh = static_cast<float>(fs_info.height);
    const float f_vertices[16] = {
        0.0f, 0.0f, 0.0f, 0.0f,
        fw,   0.0f, 1.0f, 0.0f,
        fw,   fh,   1.0f, 1.0f,
        0.0f, fh,   0.0f, 1.0f
    };
    glBindBuffer(GL_ARRAY_BUFFER, floating_vbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(f_vertices), f_vertices);

    const double cos_fr = std::cos(fs_info.rotation);
    const double sin_fr = std::sin(fs_info.rotation);
    const double rel_ox = fs_info.origin_x - fs_info.pivot_x;
    const double rel_oy = fs_info.origin_y - fs_info.pivot_y;

    const double l2d_00 = cos_fr * fs_info.scale_x;
    const double l2d_01 = -sin_fr * fs_info.scale_y;
    const double l2d_02 = fs_info.pivot_x + fs_info.offset_x + cos_fr * rel_ox * fs_info.scale_x - sin_fr * rel_oy * fs_info.scale_y;

    const double l2d_10 = sin_fr * fs_info.scale_x;
    const double l2d_11 = cos_fr * fs_info.scale_y;
    const double l2d_12 = fs_info.pivot_y + fs_info.offset_y + sin_fr * rel_ox * fs_info.scale_x + cos_fr * rel_oy * fs_info.scale_y;

    const float f_mat[9] = {
        static_cast<float>(A * l2d_00 + B * l2d_10),
        static_cast<float>(D * l2d_00 + E * l2d_10),
        0.0f,

        static_cast<float>(A * l2d_01 + B * l2d_11),
        static_cast<float>(D * l2d_01 + E * l2d_11),
        0.0f,

        static_cast<float>(A * l2d_02 + B * l2d_12 + C),
        static_cast<float>(D * l2d_02 + E * l2d_12 + F),
        1.0f
    };

    glUniformMatrix3fv(u_transform_loc_, 1, GL_FALSE, f_mat);
    glBindTexture(GL_TEXTURE_2D, floating_texture_id_);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void *>(0));
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void *>(2 * sizeof(float)));

    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void *>(0));
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void *>(2 * sizeof(float)));

    glDisable(GL_BLEND);
  }

  glBindVertexArray(0);
  glBindTexture(GL_TEXTURE_2D, 0);
  glUseProgram(0);
  full_reload_ = false;
}

void OpenGLRenderer::init_stroke_resources() {
  glGenVertexArrays(1, &stroke_vao_);
  glGenBuffers(1, &stroke_vbo_);
  glGenBuffers(1, &stroke_instance_vbo_);

  glBindVertexArray(stroke_vao_);

  const float kUnitQuadVertices[12] = {
      -1.0f, -1.0f,
       1.0f, -1.0f,
       1.0f,  1.0f,
      -1.0f, -1.0f,
       1.0f,  1.0f,
      -1.0f,  1.0f,
  };
  glBindBuffer(GL_ARRAY_BUFFER, stroke_vbo_);
  glBufferData(GL_ARRAY_BUFFER, sizeof(kUnitQuadVertices), kUnitQuadVertices, GL_STATIC_DRAW);

  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), reinterpret_cast<void *>(0));
  glVertexAttribDivisor(0, 0);

  glBindBuffer(GL_ARRAY_BUFFER, stroke_instance_vbo_);

  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(brush::BrushDab),
                        reinterpret_cast<void *>(offsetof(brush::BrushDab, x)));
  glVertexAttribDivisor(1, 1);

  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(brush::BrushDab),
                        reinterpret_cast<void *>(offsetof(brush::BrushDab, major_radius)));
  glVertexAttribDivisor(2, 1);

  glEnableVertexAttribArray(3);
  glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(brush::BrushDab),
                        reinterpret_cast<void *>(offsetof(brush::BrushDab, angle)));
  glVertexAttribDivisor(3, 1);

  glEnableVertexAttribArray(4);
  glVertexAttribPointer(4, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(brush::BrushDab),
                        reinterpret_cast<void *>(offsetof(brush::BrushDab, color)));
  glVertexAttribDivisor(4, 1);

  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindVertexArray(0);
}

void OpenGLRenderer::init_composite_resources() {
  glGenVertexArrays(1, &fullscreen_vao_);
  glGenBuffers(1, &fullscreen_vbo_);

  glBindVertexArray(fullscreen_vao_);
  glBindBuffer(GL_ARRAY_BUFFER, fullscreen_vbo_);
  const float kFullscreenVertices[8] = {
      -1.0f, -1.0f,
       1.0f, -1.0f,
       1.0f,  1.0f,
      -1.0f,  1.0f,
  };
  glBufferData(GL_ARRAY_BUFFER, sizeof(kFullscreenVertices), kFullscreenVertices, GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), reinterpret_cast<void *>(0));
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindVertexArray(0);
}

void OpenGLRenderer::ensure_stroke_fbo(int width, int height) {
  if (width <= 0 || height <= 0) return;
  if (stroke_fbo_ == 0) {
    glGenFramebuffers(1, &stroke_fbo_);
    glGenTextures(1, &stroke_texture_);
  }
  if (stroke_width_ != width || stroke_height_ != height) {
    stroke_width_ = width;
    stroke_height_ = height;
    glBindTexture(GL_TEXTURE_2D, stroke_texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, stroke_fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, stroke_texture_, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    clear_stroke();
  }
}

void OpenGLRenderer::clear_stroke() {
  if (!initialized_ || stroke_fbo_ == 0 || stroke_width_ <= 0 || stroke_height_ <= 0) {
    return;
  }
  glBindFramebuffer(GL_FRAMEBUFFER, stroke_fbo_);
  glViewport(0, 0, stroke_width_, stroke_height_);
  glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void OpenGLRenderer::render_brush_dabs(const std::vector<brush::BrushDab> &dabs, int doc_w, int doc_h,
                                       const brush::Settings &settings, const core::SelectionMask *selection) {
  if (!initialized_ || dabs.empty() || doc_w <= 0 || doc_h <= 0) {
    return;
  }
  ensure_stroke_fbo(doc_w, doc_h);

  bool has_tip = false;
  if (settings.tip && !settings.tip->empty()) {
    has_tip = true;
    if (tip_texture_ == 0) {
      glGenTextures(1, &tip_texture_);
    }
    glBindTexture(GL_TEXTURE_2D, tip_texture_);
    if (tip_width_ != settings.tip->width() || tip_height_ != settings.tip->height()) {
      tip_width_ = settings.tip->width();
      tip_height_ = settings.tip->height();
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, tip_width_, tip_height_, 0, GL_RED,
                   GL_UNSIGNED_BYTE, settings.tip->mask().data());
    } else {
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, tip_width_, tip_height_, GL_RED,
                      GL_UNSIGNED_BYTE, settings.tip->mask().data());
    }
    glBindTexture(GL_TEXTURE_2D, 0);
  }

  bool has_texture = false;
  if (settings.texture_enabled && settings.texture && !settings.texture->empty()) {
    has_texture = true;
    if (paper_texture_ == 0) {
      glGenTextures(1, &paper_texture_);
    }
    glBindTexture(GL_TEXTURE_2D, paper_texture_);
    if (paper_width_ != settings.texture->width() || paper_height_ != settings.texture->height()) {
      paper_width_ = settings.texture->width();
      paper_height_ = settings.texture->height();
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, paper_width_, paper_height_, 0, GL_RED,
                   GL_UNSIGNED_BYTE, settings.texture->data().data());
    } else {
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, paper_width_, paper_height_, GL_RED,
                      GL_UNSIGNED_BYTE, settings.texture->data().data());
    }
    glBindTexture(GL_TEXTURE_2D, 0);
  }

  bool has_selection = false;
  if (selection && selection->active() && !selection->data().empty()) {
    has_selection = true;
    if (selection_texture_ == 0) {
      glGenTextures(1, &selection_texture_);
    }
    glBindTexture(GL_TEXTURE_2D, selection_texture_);
    if (selection_width_ != doc_w || selection_height_ != doc_h) {
      selection_width_ = doc_w;
      selection_height_ = doc_h;
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, doc_w, doc_h, 0, GL_RED,
                   GL_UNSIGNED_BYTE, selection->data().data());
    } else {
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, doc_w, doc_h, GL_RED,
                      GL_UNSIGNED_BYTE, selection->data().data());
    }
    glBindTexture(GL_TEXTURE_2D, 0);
  }

  glBindFramebuffer(GL_FRAMEBUFFER, stroke_fbo_);
  glViewport(0, 0, doc_w, doc_h);

  glEnable(GL_BLEND);
  glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  glBlendEquation(GL_FUNC_ADD);

  glUseProgram(dab_program_);
  glUniform2f(dab_u_doc_size_loc_, static_cast<float>(doc_w), static_cast<float>(doc_h));

  if (has_tip) {
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, tip_texture_);
    glUniform1i(dab_u_tip_texture_loc_, 1);
  }
  glUniform1i(dab_u_has_tip_loc_, has_tip ? 1 : 0);

  if (has_texture) {
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, paper_texture_);
    glUniform1i(dab_u_paper_texture_loc_, 2);
    glUniform1f(dab_u_texture_scale_loc_, static_cast<float>(settings.texture_scale));
    glUniform1f(dab_u_texture_strength_loc_, static_cast<float>(settings.texture_strength));
    glUniform1f(dab_u_texture_contrast_loc_, static_cast<float>(settings.texture_contrast));
    glUniform1f(dab_u_texture_brightness_loc_, static_cast<float>(settings.texture_brightness));
    glUniform1i(dab_u_texture_invert_loc_, settings.texture_invert ? 1 : 0);
    glUniform2f(dab_u_texture_offset_loc_, static_cast<float>(settings.texture_offset_x),
                static_cast<float>(settings.texture_offset_y));
  }
  glUniform1i(dab_u_has_texture_loc_, has_texture ? 1 : 0);

  if (has_selection) {
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, selection_texture_);
    glUniform1i(dab_u_selection_texture_loc_, 3);
  }
  glUniform1i(dab_u_has_selection_loc_, has_selection ? 1 : 0);

  glBindBuffer(GL_ARRAY_BUFFER, stroke_instance_vbo_);
  glBufferData(GL_ARRAY_BUFFER, dabs.size() * sizeof(brush::BrushDab), dabs.data(), GL_DYNAMIC_DRAW);
  glBindBuffer(GL_ARRAY_BUFFER, 0);

  glBindVertexArray(stroke_vao_);
  glDrawArraysInstanced(GL_TRIANGLES, 0, 6, static_cast<GLsizei>(dabs.size()));
  glBindVertexArray(0);

  glDisable(GL_BLEND);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, 0);
  glUseProgram(0);
}

void OpenGLRenderer::ensure_comp_fbos(int width, int height) {
  if (width <= 0 || height <= 0) return;
  if (comp_width_ == width && comp_height_ == height && comp_fbo_[0] != 0) {
    return;
  }
  comp_width_ = width;
  comp_height_ = height;
  for (int i = 0; i < 2; ++i) {
    if (comp_fbo_[i] == 0) {
      glGenFramebuffers(1, &comp_fbo_[i]);
      glGenTextures(1, &comp_tex_[i]);
    }
    glBindTexture(GL_TEXTURE_2D, comp_tex_[i]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, comp_fbo_[i]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, comp_tex_[i], 0);
  }
  if (blit_fbo_ == 0) {
    glGenFramebuffers(1, &blit_fbo_);
  }
  if (bake_fbo_ == 0) {
    glGenFramebuffers(1, &bake_fbo_);
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glBindTexture(GL_TEXTURE_2D, 0);
}

void OpenGLRenderer::sync_layer_tiles(std::size_t layer_id, core::TiledImage &image) {
  if (!initialized_) return;

  auto &gl_layer = layer_textures_[layer_id];
  if (gl_layer.texture == 0) {
    glGenTextures(1, &gl_layer.texture);
    glBindTexture(GL_TEXTURE_2D, gl_layer.texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
  }

  glBindTexture(GL_TEXTURE_2D, gl_layer.texture);

  const bool size_changed = (gl_layer.width != image.width() || gl_layer.height != image.height());
  if (size_changed || full_reload_) {
    gl_layer.width = image.width();
    gl_layer.height = image.height();

    std::vector<uint32_t> blank(static_cast<std::size_t>(gl_layer.width) * gl_layer.height, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, gl_layer.width, gl_layer.height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, blank.data());

    for (const auto &[key, tile] : image.tiles()) {
      const int tx = core::TiledImage::key_x(key);
      const int ty = core::TiledImage::key_y(key);
      const int tw = std::min(core::kTileSize, image.width() - tx * core::kTileSize);
      const int th = std::min(core::kTileSize, image.height() - ty * core::kTileSize);
      if (tw <= 0 || th <= 0) continue;

      if (tw == core::kTileSize && th == core::kTileSize) {
        glTexSubImage2D(GL_TEXTURE_2D, 0, tx * core::kTileSize, ty * core::kTileSize,
                        core::kTileSize, core::kTileSize, GL_RGBA, GL_UNSIGNED_BYTE,
                        tile->pixels.data());
      } else {
        std::vector<core::Rgba8> cropped(static_cast<std::size_t>(tw) * th);
        for (int ly = 0; ly < th; ++ly) {
          for (int lx = 0; lx < tw; ++lx) {
            cropped[ly * tw + lx] = tile->pixels[ly * core::kTileSize + lx];
          }
        }
        glTexSubImage2D(GL_TEXTURE_2D, 0, tx * core::kTileSize, ty * core::kTileSize,
                        tw, th, GL_RGBA, GL_UNSIGNED_BYTE, cropped.data());
      }
    }
  } else {
    for (std::uint64_t key : image.gpu_dirty_tiles()) {
      const int tx = core::TiledImage::key_x(key);
      const int ty = core::TiledImage::key_y(key);
      const int tw = std::min(core::kTileSize, image.width() - tx * core::kTileSize);
      const int th = std::min(core::kTileSize, image.height() - ty * core::kTileSize);
      if (tw <= 0 || th <= 0) continue;

      const auto *tile = image.tile_at(tx, ty);
      if (tile != nullptr) {
        if (tw == core::kTileSize && th == core::kTileSize) {
          glTexSubImage2D(GL_TEXTURE_2D, 0, tx * core::kTileSize, ty * core::kTileSize,
                          core::kTileSize, core::kTileSize, GL_RGBA, GL_UNSIGNED_BYTE,
                          tile->pixels.data());
        } else {
          std::vector<core::Rgba8> cropped(static_cast<std::size_t>(tw) * th);
          for (int ly = 0; ly < th; ++ly) {
            for (int lx = 0; lx < tw; ++lx) {
              cropped[ly * tw + lx] = tile->pixels[ly * core::kTileSize + lx];
            }
          }
          glTexSubImage2D(GL_TEXTURE_2D, 0, tx * core::kTileSize, ty * core::kTileSize,
                          tw, th, GL_RGBA, GL_UNSIGNED_BYTE, cropped.data());
        }
      } else {
        std::vector<std::uint32_t> blank(static_cast<std::size_t>(tw) * th, 0);
        glTexSubImage2D(GL_TEXTURE_2D, 0, tx * core::kTileSize, ty * core::kTileSize,
                        tw, th, GL_RGBA, GL_UNSIGNED_BYTE, blank.data());
      }
    }
  }

  image.clear_dirty();
  image.clear_gpu_dirty();
  glBindTexture(GL_TEXTURE_2D, 0);
}

void OpenGLRenderer::composite_document(const core::Document &doc,
                                        bool stroke_active,
                                        std::size_t active_layer_id,
                                        brush::Mode stroke_mode,
                                        double stroke_opacity,
                                        int stroke_blend_mode) {
  if (!initialized_ || doc.width() <= 0 || doc.height() <= 0) return;

  ensure_comp_fbos(doc.width(), doc.height());
  ensure_stroke_fbo(doc.width(), doc.height());

  if (texture_width_ != doc.width() || texture_height_ != doc.height() || texture_id_ == 0) {
    if (texture_id_ == 0) {
      glGenTextures(1, &texture_id_);
    }
    texture_width_ = doc.width();
    texture_height_ = doc.height();
    glBindTexture(GL_TEXTURE_2D, texture_id_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, texture_width_, texture_height_, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, last_pixelated_ ? GL_NEAREST : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, last_pixelated_ ? GL_NEAREST : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
  }

  struct Item {
    std::size_t id;
    double opacity;
    int blend_mode;
    bool is_clipped;
    bool screentone;
    float screentone_freq;
    float screentone_angle;
    int screentone_pat;
    bool has_stroke;
  };
  std::vector<Item> items;

  for (std::size_t i = 0; i < doc.layer_count(); ++i) {
    const auto &layer = doc.layer(i);
    if (layer.is_group()) continue;

    bool ancestor_hidden = false;
    bool ancestor_clipped = false;
    std::size_t pid = layer.parent_id();
    std::size_t guard = 0;
    double parent_opacity = 1.0;
    while (pid != 0 && guard++ < doc.layer_count()) {
      const auto *p = doc.find_layer_by_id(pid);
      if (!p) break;
      if (!p->visible() || p->opacity() <= 0.0) {
        ancestor_hidden = true;
        break;
      }
      parent_opacity *= p->opacity();
      if (p->clipping_mask()) ancestor_clipped = true;
      pid = p->parent_id();
    }
    if (ancestor_hidden) continue;
    if (!layer.visible() || layer.opacity() <= 0.0) continue;

    Item it;
    it.id = layer.id();
    it.opacity = layer.opacity() * parent_opacity;
    it.blend_mode = blend_mode_to_int(layer.composite_op());
    it.is_clipped = layer.clipping_mask() || ancestor_clipped;
    it.screentone = layer.screentone();
    it.screentone_freq = static_cast<float>(layer.screentone_frequency());
    it.screentone_angle = static_cast<float>(layer.screentone_angle());
    it.screentone_pat = screentone_pattern_to_int(layer.screentone_pattern());
    it.has_stroke = (stroke_active && layer.id() == active_layer_id);
    items.push_back(it);
  }

  if (items.empty()) {
    glBindFramebuffer(GL_FRAMEBUFFER, comp_fbo_[0]);
    glViewport(0, 0, doc.width(), doc.height());
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, blit_fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture_id_, 0);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, comp_fbo_[0]);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, blit_fbo_);
    glBlitFramebuffer(0, 0, doc.width(), doc.height(),
                      0, 0, doc.width(), doc.height(),
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return;
  }

  int src_idx = 0;
  int dst_idx = 1;
  glDisable(GL_BLEND);
  glViewport(0, 0, doc.width(), doc.height());
  glUseProgram(composite_program_);
  glBindVertexArray(fullscreen_vao_);
  glUniform2f(comp_u_doc_size_loc_, static_cast<float>(doc.width()), static_cast<float>(doc.height()));

  for (std::size_t i = 0; i < items.size(); ++i) {
    const auto &item = items[i];
    glBindFramebuffer(GL_FRAMEBUFFER, comp_fbo_[dst_idx]);

    glUniform1i(comp_u_is_first_layer_loc_, (i == 0) ? 1 : 0);

    if (i > 0) {
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, comp_tex_[src_idx]);
      glUniform1i(comp_u_dst_loc_, 0);
    }

    auto layer_tex_it = layer_textures_.find(item.id);
    GLuint ltex = (layer_tex_it != layer_textures_.end()) ? layer_tex_it->second.texture : 0;
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, ltex);
    glUniform1i(comp_u_layer_loc_, 1);

    if (item.has_stroke) {
      glActiveTexture(GL_TEXTURE2);
      glBindTexture(GL_TEXTURE_2D, stroke_texture_);
      glUniform1i(comp_u_stroke_loc_, 2);
    }
    glUniform1i(comp_u_has_stroke_loc_, item.has_stroke ? 1 : 0);
    glUniform1i(comp_u_stroke_mode_loc_, stroke_mode == brush::Mode::ERASE ? 1 : 0);
    glUniform1f(comp_u_stroke_opacity_loc_, static_cast<float>(stroke_opacity));
    glUniform1i(comp_u_stroke_blend_mode_loc_, stroke_blend_mode);
    glUniform1f(comp_u_layer_opacity_loc_, static_cast<float>(item.opacity));
    glUniform1i(comp_u_blend_mode_loc_, item.blend_mode);
    glUniform1i(comp_u_clipping_mask_loc_, item.is_clipped ? 1 : 0);
    glUniform1i(comp_u_screentone_loc_, item.screentone ? 1 : 0);
    glUniform1f(comp_u_screentone_freq_loc_, item.screentone_freq);
    glUniform1f(comp_u_screentone_angle_loc_, item.screentone_angle);
    glUniform1i(comp_u_screentone_pat_loc_, item.screentone_pat);

    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    std::swap(src_idx, dst_idx);
  }

  glBindVertexArray(0);
  glUseProgram(0);

  glBindFramebuffer(GL_FRAMEBUFFER, blit_fbo_);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture_id_, 0);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, comp_fbo_[src_idx]);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, blit_fbo_);
  glBlitFramebuffer(0, 0, doc.width(), doc.height(),
                    0, 0, doc.width(), doc.height(),
                    GL_COLOR_BUFFER_BIT, GL_NEAREST);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, 0);
}

void OpenGLRenderer::bake_and_readback_stroke(std::size_t layer_id, brush::Mode mode,
                                              int bx, int by, int bw, int bh,
                                              std::vector<core::Rgba8> &out_pixels,
                                              double stroke_opacity,
                                              int stroke_blend_mode) {
  if (!initialized_ || stroke_texture_ == 0 || stroke_width_ <= 0 || stroke_height_ <= 0) {
    return;
  }
  auto it = layer_textures_.find(layer_id);
  if (it == layer_textures_.end() || it->second.texture == 0) {
    return;
  }
  ensure_comp_fbos(stroke_width_, stroke_height_);

  glBindFramebuffer(GL_FRAMEBUFFER, comp_fbo_[0]);
  glViewport(0, 0, stroke_width_, stroke_height_);
  glDisable(GL_BLEND);

  glUseProgram(bake_program_);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, it->second.texture);
  glUniform1i(bake_u_layer_loc_, 0);

  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, stroke_texture_);
  glUniform1i(bake_u_stroke_loc_, 1);

  glUniform1i(bake_u_stroke_mode_loc_, mode == brush::Mode::ERASE ? 1 : 0);
  glUniform1f(bake_u_stroke_opacity_loc_, static_cast<float>(stroke_opacity));
  glUniform1i(bake_u_stroke_blend_mode_loc_, stroke_blend_mode);

  glBindVertexArray(fullscreen_vao_);
  glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
  glBindVertexArray(0);

  glBindFramebuffer(GL_FRAMEBUFFER, bake_fbo_);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, it->second.texture, 0);

  glBindFramebuffer(GL_READ_FRAMEBUFFER, comp_fbo_[0]);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, bake_fbo_);
  glBlitFramebuffer(0, 0, stroke_width_, stroke_height_,
                    0, 0, stroke_width_, stroke_height_,
                    GL_COLOR_BUFFER_BIT, GL_NEAREST);

  if (bw > 0 && bh > 0 && bx >= 0 && by >= 0 &&
      bx + bw <= stroke_width_ && by + bh <= stroke_height_) {
    out_pixels.resize(static_cast<std::size_t>(bw) * bh);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, comp_fbo_[0]);
    glReadPixels(bx, by, bw, bh, GL_RGBA, GL_UNSIGNED_BYTE, out_pixels.data());
  }

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, 0);
  glUseProgram(0);
}

void OpenGLRenderer::clear_layer_textures() {
  if (QOpenGLContext::currentContext()) {
    for (auto &kv : layer_textures_) {
      if (kv.second.texture != 0) {
        glDeleteTextures(1, &kv.second.texture);
      }
    }
  }
  layer_textures_.clear();
}

void OpenGLRenderer::remove_layer_texture(std::size_t layer_id) {
  auto it = layer_textures_.find(layer_id);
  if (it != layer_textures_.end()) {
    if (QOpenGLContext::currentContext() && it->second.texture != 0) {
      glDeleteTextures(1, &it->second.texture);
    }
    layer_textures_.erase(it);
  }
}

void OpenGLRenderer::retain_layer_textures(const std::unordered_set<std::size_t> &live_ids) {
  const bool has_context = QOpenGLContext::currentContext() != nullptr;
  for (auto it = layer_textures_.begin(); it != layer_textures_.end();) {
    if (live_ids.count(it->first) != 0) {
      ++it;
      continue;
    }
    if (has_context && it->second.texture != 0) {
      glDeleteTextures(1, &it->second.texture);
    }
    it = layer_textures_.erase(it);
  }
}
void OpenGLRenderer::capture_downscaled_composite(int max_dimension,
                                                  std::vector<core::Rgba8> &out_pixels,
                                                  int &out_w, int &out_h) {
  out_w = 0;
  out_h = 0;
  out_pixels.clear();

  if (!initialized_ || texture_id_ == 0 || texture_width_ <= 0 || texture_height_ <= 0) {
    return;
  }

  int target_w = texture_width_;
  int target_h = texture_height_;

  if (max_dimension > 0 && (target_w > max_dimension || target_h > max_dimension)) {
    if (target_w >= target_h) {
      target_w = max_dimension;
      target_h = std::max(1, static_cast<int>(std::round(static_cast<double>(texture_height_) * max_dimension / texture_width_)));
    } else {
      target_h = max_dimension;
      target_w = std::max(1, static_cast<int>(std::round(static_cast<double>(texture_width_) * max_dimension / texture_height_)));
    }
  }

  // Ensure even dimensions for standard video codecs
  if (target_w > 2 && target_w % 2 != 0) target_w -= 1;
  if (target_h > 2 && target_h % 2 != 0) target_h -= 1;

  out_w = target_w;
  out_h = target_h;

  if (capture_fbo_ == 0) {
    glGenFramebuffers(1, &capture_fbo_);
  }
  if (capture_tex_ == 0 || capture_tex_w_ != out_w || capture_tex_h_ != out_h) {
    if (capture_tex_ != 0) {
      glDeleteTextures(1, &capture_tex_);
    }
    glGenTextures(1, &capture_tex_);
    glBindTexture(GL_TEXTURE_2D, capture_tex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, out_w, out_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    capture_tex_w_ = out_w;
    capture_tex_h_ = out_h;
  }

  glBindFramebuffer(GL_FRAMEBUFFER, capture_fbo_);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, capture_tex_, 0);

  if (blit_fbo_ == 0) {
    glGenFramebuffers(1, &blit_fbo_);
  }
  glBindFramebuffer(GL_FRAMEBUFFER, blit_fbo_);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture_id_, 0);

  glBindFramebuffer(GL_READ_FRAMEBUFFER, blit_fbo_);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, capture_fbo_);
  glBlitFramebuffer(0, 0, texture_width_, texture_height_,
                    0, 0, out_w, out_h,
                    GL_COLOR_BUFFER_BIT, GL_LINEAR);

  out_pixels.resize(static_cast<std::size_t>(out_w) * out_h);
  glBindFramebuffer(GL_FRAMEBUFFER, capture_fbo_);
  glReadPixels(0, 0, out_w, out_h, GL_RGBA, GL_UNSIGNED_BYTE, out_pixels.data());
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

} // namespace kreart::render
