#ifndef MY_PLUGIN_UTF8_PATH_HPP
#define MY_PLUGIN_UTF8_PATH_HPP

// Cubit passes file names as std::string holding UTF-8.  On Japanese
// Windows (cp932) std::filesystem::path(std::string) decodes them as cp932,
// so a path like C:\temp\日本語\a.vol fails to open.  Convert via UTF-16.

#include <filesystem>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

inline std::filesystem::path u8_string_to_path(const std::string& s)
{
#ifdef _WIN32
  if (s.empty()) return {};
  const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(),
                                    static_cast<int>(s.size()), nullptr, 0);
  if (n <= 0) return std::filesystem::path(s);
  std::wstring w(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(),
                      static_cast<int>(s.size()), w.data(), n);
  return std::filesystem::path(w);
#else
  return std::filesystem::path(s);
#endif
}

#endif
