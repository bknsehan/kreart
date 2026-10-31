#pragma once


#include <QOpenGLExtraFunctions>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "kreart/core/tiled_image.h"
#include "kreart/core/document.h"
#include "kreart/brush/round_brush.h"
namespace kreart::render {
struct FloatingSelectionRenderInfo {
  bool active = false;
  int origin_x = 0;
  int origin_y = 0;
  int width = 0;
  int height = 0;
  int offset_x = 0;
  int offset_y = 0;
  double rotation = 0.0;
  double scale_x = 1.0;
  double scale_y = 1.0;
  double pivot_x = 0.0;
  double pivot_y = 0.0;
};


class OpenGLRenderer : protected QOpenGLExtraFunctions {
public:
  OpenGLRenderer() = default;
  ~OpenGLRenderer();

  // Must be called with an active, current OpenGL context
  bool init();
  void shutdown();
  bool is_initialized() const {
    return initialized_;
  }

  // Upload dirty tiles to the GPU texture
  void sync(core::TiledImage &image, bool pixelated = false);
  void mark_all_dirty() {
    full_reload_ = true;
  }
  // Upload floating selection pixels to a dedicated GPU texture
  void sync_floating_selection(bool active, int width, int height,
                              const core::Rgba8 *pixels, const std::uint8_t *mask,
                              bool pixelated = false);


  // Render the transformed canvas quad to the active OpenGL framebuffer
  void render(int viewport_w, int viewport_h,
              double pivot_x, double pivot_y,
              double view_scale, double view_rotation,
              bool mirror_h, bool mirror_v,
              int doc_w, int doc_h,
              bool pixelated = false,
              bool show_grid = false,
              int grid_size = 32,
              core::Rgba8 grid_color = {128, 128, 140, 90},
              const FloatingSelectionRenderInfo &fs_info = {},
              bool is_dark_theme = true,
              int transparency_mode = 1,
              core::Rgba8 transparency_solid_color = {32, 33, 36, 255});

  // Stroke FBO & brush dab rendering
  void ensure_stroke_fbo(int width, int height);
  void clear_stroke();
  void render_brush_dabs(const std::vector<brush::BrushDab> &dabs, int doc_w, int doc_h,
                         const brush::Settings &settings, const core::SelectionMask *selection);

  // Layer texture cache & hardware compositing
  void sync_layer_tiles(std::size_t layer_id, core::TiledImage &image);
  void composite_document(const core::Document &doc, bool stroke_active,
                          std::size_t active_layer_id, brush::Mode stroke_mode,
                          double stroke_opacity = 1.0, int stroke_blend_mode = 0);
  void bake_and_readback_stroke(std::size_t layer_id, brush::Mode mode,
                                int bx, int by, int bw, int bh,
                                std::vector<core::Rgba8> &out_pixels,
                                double stroke_opacity = 1.0, int stroke_blend_mode = 0);
  void capture_downscaled_composite(int max_dimension,
                                    std::vector<core::Rgba8> &out_pixels,
                                    int &out_w, int &out_h);
  static int blend_mode_to_int(const std::string &op);
  void clear_layer_textures();
  void remove_layer_texture(std::size_t layer_id);
  // Releases the GPU texture of every layer that is no longer part of the
  // document. Without this, deleted layers leak textures for the session.
  void retain_layer_textures(const std::unordered_set<std::size_t> &live_ids);
  bool has_layer_texture(std::size_t layer_id) const {
    return layer_textures_.find(layer_id) != layer_textures_.end();
  }

private:
  bool compile_shaders();
  void init_quad();
  void init_stroke_resources();
  void init_composite_resources();
  void ensure_comp_fbos(int width, int height);

  // Stroke rendering resources
  unsigned int stroke_fbo_ = 0;
  unsigned int stroke_texture_ = 0;
  unsigned int stroke_vao_ = 0;
  unsigned int stroke_vbo_ = 0;
  unsigned int stroke_instance_vbo_ = 0;
  int stroke_width_ = 0;
  int stroke_height_ = 0;
  unsigned int dab_program_ = 0;

  int dab_u_doc_size_loc_ = -1;
  int dab_u_has_tip_loc_ = -1;
  int dab_u_tip_texture_loc_ = -1;
  int dab_u_has_texture_loc_ = -1;
  int dab_u_paper_texture_loc_ = -1;
  int dab_u_texture_scale_loc_ = -1;
  int dab_u_texture_strength_loc_ = -1;
  int dab_u_texture_contrast_loc_ = -1;
  int dab_u_texture_brightness_loc_ = -1;
  int dab_u_texture_invert_loc_ = -1;
  int dab_u_texture_offset_loc_ = -1;
  int dab_u_has_selection_loc_ = -1;
  int dab_u_selection_texture_loc_ = -1;

  unsigned int tip_texture_ = 0;
  int tip_width_ = 0;
  int tip_height_ = 0;
  unsigned int paper_texture_ = 0;
  int paper_width_ = 0;
  int paper_height_ = 0;
  unsigned int selection_texture_ = 0;
  int selection_width_ = 0;
  int selection_height_ = 0;

  // Layer textures
  struct GpuLayer {
    unsigned int texture = 0;
    int width = 0;
    int height = 0;
  };
  std::unordered_map<std::size_t, GpuLayer> layer_textures_;

  // Compositing resources
  unsigned int composite_program_ = 0;
  unsigned int bake_program_ = 0;
  unsigned int fullscreen_vao_ = 0;
  unsigned int fullscreen_vbo_ = 0;
  unsigned int comp_fbo_[2] = {0, 0};
  unsigned int comp_tex_[2] = {0, 0};
  int comp_width_ = 0;
  int comp_height_ = 0;
  unsigned int blit_fbo_ = 0;
  unsigned int bake_fbo_ = 0;
  unsigned int capture_fbo_ = 0;
  unsigned int capture_tex_ = 0;
  int capture_tex_w_ = 0;
  int capture_tex_h_ = 0;

  int comp_u_dst_loc_ = -1;
  int comp_u_layer_loc_ = -1;
  int comp_u_stroke_loc_ = -1;
  int comp_u_is_first_layer_loc_ = -1;
  int comp_u_has_stroke_loc_ = -1;
  int comp_u_stroke_mode_loc_ = -1;
  int comp_u_layer_opacity_loc_ = -1;
  int comp_u_blend_mode_loc_ = -1;
  int comp_u_clipping_mask_loc_ = -1;
  int comp_u_screentone_loc_ = -1;
  int comp_u_screentone_freq_loc_ = -1;
  int comp_u_screentone_angle_loc_ = -1;
  int comp_u_screentone_pat_loc_ = -1;
  int comp_u_doc_size_loc_ = -1;
  int comp_u_stroke_opacity_loc_ = -1;
  int comp_u_stroke_blend_mode_loc_ = -1;

  int bake_u_layer_loc_ = -1;
  int bake_u_stroke_loc_ = -1;
  int bake_u_stroke_mode_loc_ = -1;
  int bake_u_stroke_opacity_loc_ = -1;
  int bake_u_stroke_blend_mode_loc_ = -1;

  bool initialized_ = false;
  bool full_reload_ = true;
  unsigned int program_ = 0;
  unsigned int vao_ = 0;
  unsigned int vbo_ = 0;
  unsigned int texture_id_ = 0;
  int texture_width_ = 0;
  int texture_height_ = 0;
  bool last_pixelated_ = false;

  int u_transform_loc_ = -1;
  int u_doc_size_loc_ = -1;
  int u_show_grid_loc_ = -1;
  int u_grid_size_loc_ = -1;
  int u_grid_color_loc_ = -1;
  int u_texture_loc_ = -1;
  int u_is_floating_loc_ = -1;
  int u_is_dark_theme_loc_ = -1;
  int u_transparency_mode_loc_ = -1;
  int u_transparency_solid_color_loc_ = -1;
  unsigned int floating_texture_id_ = 0;
  unsigned int floating_vbo_ = 0;
  int floating_width_ = 0;
  int floating_height_ = 0;
  bool floating_uploaded_ = false;
};

} // namespace kreart::render
