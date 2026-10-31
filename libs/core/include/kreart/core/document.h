#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include "kreart/core/selection_mask.h"

#include "kreart/core/layer.h"
#include "kreart/core/tiled_image.h"

namespace kreart::core {
struct FloatingSelection {
  bool active = false;
  int origin_x = 0;
  int origin_y = 0;
  int offset_x = 0;
  int offset_y = 0;
  int width = 0;
  int height = 0;
  double rotation = 0.0;
  double scale_x = 1.0;
  double scale_y = 1.0;
  double pivot_x = 0.0;
  double pivot_y = 0.0;
  std::vector<Rgba8> pixels;
  std::vector<std::uint8_t> mask;

  bool operator==(const FloatingSelection &) const = default;
};

struct SelectionClipboard {
  int width = 0;
  int height = 0;
  std::vector<Rgba8> pixels;
  std::vector<std::uint8_t> mask;
  bool valid = false;
};


struct DocumentSnapshot {
  int width = 0;
  int height = 0;
  std::size_t active_layer_index = 0;
  std::vector<LayerSnapshot> layers;
  SelectionMask selection;
  FloatingSelection floating_selection;
};

class Document {
public:
  Document(int width, int height);

  void reset(int width, int height);
  void resize_canvas(int new_width, int new_height, int offset_x = 0, int offset_y = 0);
  void scale_image(int new_width, int new_height, ScaleFilter filter);


  int width() const {
    return width_;
  }
  int height() const {
    return height_;
  }

  // Layer stack: index 0 is bottom-most, size()-1 is top-most
  std::size_t layer_count() const {
    return layers_.size();
  }
  std::size_t active_layer_index() const {
    return active_layer_index_;
  }
  void set_active_layer_index(std::size_t index);
  bool has_active_layer() const { return has_active_layer_; }
  void clear_active_layer() { has_active_layer_ = false; }
  std::size_t auto_select_top_layer();
  std::optional<std::size_t> find_layer_at(int x, int y) const;

  Layer &layer(std::size_t index) {
    return layers_.at(index);
  }
  const Layer &layer(std::size_t index) const {
    return layers_.at(index);
  }

  Layer &active_layer();
  const Layer &active_layer() const;

  // Active layer image (backwards-compatible with single-layer API)
  TiledImage &image() {
    return active_layer().image();
  }
  const TiledImage &image() const {
    return active_layer().image();
  }
  SelectionMask &selection() {
    return selection_;
  }
  const SelectionMask &selection() const {
    return selection_;
  }
  void clear_selection_content();
  void move_selection(int delta_x, int delta_y);
  bool has_floating_selection() const {
    return floating_selection_.active;
  }
  const FloatingSelection &floating_selection() const {
    return floating_selection_;
  }
  void start_move_selection();
  void update_move_selection(int delta_x, int delta_y);
  void update_transform_selection(int delta_x, int delta_y, double rotation,
                                 double scale_x = 1.0, double scale_y = 1.0);
  void rotate_selection(double delta_radians);
  void scale_selection(double factor_x, double factor_y);
  void commit_move_selection();
  void cancel_move_selection();
  void flip_floating_horizontal();
  void flip_floating_vertical();
  void nudge_floating(int dx, int dy);

  const SelectionClipboard &clipboard() const { return clipboard_; }
  void set_clipboard(int w, int h, std::vector<Rgba8> pixels, std::vector<std::uint8_t> mask);
  void copy_selection();
  void cut_selection();
  bool paste_selection();
  bool paste_as_new_layer();
  void flood_fill(int start_x, int start_y, Rgba8 fill_color, int tolerance = 15);
  void flood_select(int start_x, int start_y, int tolerance = 32, bool contiguous = true,
                    SelectionCombineMode mode = SelectionCombineMode::REPLACE);
  void feather_selection(double radius);
  void grow_selection(int pixels);
  void shrink_selection(int pixels);
  void border_selection(int pixels);




  std::size_t add_layer(std::string name = "", bool fill_white = false);
  std::size_t add_group(std::string name = "");
  void rename_layer(std::size_t index, std::string new_name);
  void insert_layer(std::size_t index, Layer layer);
  bool remove_layer(std::size_t index);
  bool move_layer(std::size_t from_index, std::size_t to_index);
  // Contiguous [bottom, top] index range of a layer including its descendants (block below it).
  std::pair<std::size_t, std::size_t> layer_block_range(std::size_t index) const;
  // Moves the block of `from_index` so it lands directly above/below the block of `target_index`.
  bool move_layer_relative(std::size_t from_index, std::size_t target_index, bool below);
  bool move_into_group(std::size_t layer_idx, std::size_t group_idx);
  bool duplicate_layer(std::size_t index);
  bool merge_layer_down(std::size_t index);
  bool merge_group(std::size_t folder_index);
  void get_group_descendants_count(std::size_t folder_index,
                                   std::size_t &out_layer_count,
                                   std::size_t &out_folder_count) const;
  bool remove_group_recursive(std::size_t folder_index);
  bool merge_selected_layers(const std::vector<std::size_t> &indices);
  std::size_t group_selected_layers(const std::vector<std::size_t> &indices, std::string group_name = "");
  void set_layer_visible(std::size_t index, bool visible);
  void set_layer_opacity(std::size_t index, double opacity);
  void set_layer_alpha_locked(std::size_t index, bool locked);
  void set_layer_clipping_mask(std::size_t index, bool clipping);
  void set_layer_composite_op(std::size_t index, std::string op);
  void set_layer_screentone(std::size_t index, bool screentone, double freq = 60.0, double angle = 45.0);
  void set_layer_screentone_pattern(std::size_t index, ScreentonePattern pat);
  const Layer *find_layer_by_id(std::size_t id) const;
  Layer *find_layer_by_id(std::size_t id);
  int get_layer_depth(std::size_t index) const;
  bool is_ancestor_collapsed(std::size_t index) const;
  bool is_descendant_of(std::size_t layer_id, std::size_t group_id) const;
  void clean_hierarchy_invariants();



  // Compositing
  TiledImage &composite_image();
  const TiledImage &composite_image() const;
  // Composite with the active floating selection baked in (for export, save, picking).
  TiledImage composite_with_floating() const;
  void mark_composite_dirty() {
    touch_composite();
  }

  // Monotonic counter bumped whenever the composite content may have changed.
  // Consumers (the GL canvas) compare snapshots to skip redundant recomposites.
  std::uint64_t composite_revision() const {
    return composite_revision_;
  }

  // History / Undo
  void begin_change();
  bool commit_change();
  void cancel_change();

  bool can_undo() const {
    return !undo_.empty();
  }
  bool can_redo() const {
    return !redo_.empty();
  }
  bool undo();
  bool redo();

  bool modified() const {
    return modified_;
  }
  void mark_saved() {
    modified_ = false;
  }

  DocumentSnapshot snapshot() const;
  void restore(const DocumentSnapshot &snapshot);

private:
  struct HistoryEntry {
    DocumentSnapshot before;
    DocumentSnapshot after;
  };

  void update_composite() const;
  void touch_composite() const {
    composite_dirty_ = true;
    ++composite_revision_;
  }

  static constexpr std::size_t kMaxHistory = 64;

  int width_ = 0;
  int height_ = 0;
  std::vector<Layer> layers_;
  std::size_t active_layer_index_ = 0;
  bool has_active_layer_ = true;

  mutable TiledImage composite_;
  mutable bool composite_dirty_ = true;
  mutable std::uint64_t composite_revision_ = 0;

  std::optional<DocumentSnapshot> pending_;
  std::vector<HistoryEntry> undo_;
  std::vector<HistoryEntry> redo_;
  bool modified_ = false;
  SelectionMask selection_;
  FloatingSelection floating_selection_;
  SelectionClipboard clipboard_;
  std::size_t next_layer_id_ = 0;
};

} // namespace kreart::core
