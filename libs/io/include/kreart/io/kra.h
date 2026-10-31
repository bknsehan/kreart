#pragma once

#include <filesystem>
#include <string>

#include "kreart/core/document.h"

namespace kreart::io {

bool read_kra(const std::filesystem::path &path, core::Document &document, std::string &error,
              const std::string &session_uuid = "");

bool read_kra_thumbnail(const std::filesystem::path &path, std::vector<std::uint8_t> &out_png_bytes, std::string &error);

bool read_kra_thumbnail(const std::filesystem::path &path, std::vector<std::uint8_t> &out_png_bytes, int &out_width, int &out_height, std::string &error);
// allow_full_load = false skips the read_kra fallback (which parses the whole
// document) and fails with "No preview found" when the archive has no preview.
bool read_kra_thumbnail(const std::filesystem::path &path, std::vector<std::uint8_t> &out_png_bytes, int &out_width, int &out_height, std::string &error, bool allow_full_load);
bool write_kra(const std::filesystem::path &path, const core::Document &document, std::string &error,
               const std::string &session_uuid = "");

} // namespace kreart::io
