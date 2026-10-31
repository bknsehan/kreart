#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "kreart/core/document.h"

namespace kreart::io {

// Returns the autosave path for a named document (~filename.kra in the same directory).
std::filesystem::path autosave_path_for(const std::filesystem::path &original_path);

// Returns an autosave path for an untitled document in the user's cache directory.
std::filesystem::path untitled_autosave_path();

// Checks if a newer autosave file exists for the given document path.
bool has_autosave(const std::filesystem::path &original_path, std::filesystem::path &autosave_path);

// Saves the document to its designated autosave location.
bool perform_autosave(const core::Document &document, const std::filesystem::path &original_path,
                      std::filesystem::path &saved_path, std::string &error);

// Removes the autosave file associated with the original path, if it exists.
void remove_autosave(const std::filesystem::path &original_path);

// Finds any orphaned untitled autosave files in the cache directory.
std::vector<std::filesystem::path> find_orphaned_autosaves();

} // namespace kreart::io
