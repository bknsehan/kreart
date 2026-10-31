#pragma once

#include <filesystem>
#include <string>

#include "kreart/core/tiled_image.h"

namespace kreart::io {

bool write_png(const std::filesystem::path &path, const core::TiledImage &image,
               std::string &error);

} // namespace kreart::io
