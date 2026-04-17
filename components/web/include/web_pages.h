#pragma once

#include <cstddef>

namespace web_pages {

bool renderRoot(char* out, size_t out_len);
bool renderDebug(char* out, size_t out_len);
bool renderAdmin(char* out, size_t out_len);

}  // namespace web_pages
