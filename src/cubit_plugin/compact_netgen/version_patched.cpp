// Modified from Netgen libsrc/core/version.cpp at v6.2.2608 (96e5682f6ea43ba77ba3bb2ae4bb4bd1791f506e) plus upstream 13b47c3a9fb2e7d53ca4be7227bdd4acbd6c6e73 (pyramid apex clamp).
// Local modifications present in the source history on 2026-10-15.
// Netgen licensing and attribution are provided with the distribution.

// version_patched.cpp — Compact Netgen version
// Removes file-scope std::map and lambda static init that crash in DLL DllMain.

#include <map>
#include <netgen_version.hpp>
#include "exception.hpp"
#include "version.hpp"

namespace ngcore
{
  // Lazy init: function-local static avoids file-scope static init order issues
  static std::map<std::string, VersionInfo>& get_library_versions()
  {
    static std::map<std::string, VersionInfo> library_versions;
    return library_versions;
  }

  const VersionInfo& GetLibraryVersion(const std::string& library)
  { return get_library_versions()[library]; }

  const std::map<std::string, VersionInfo>& GetLibraryVersions()
  { return get_library_versions(); }

  void SetLibraryVersion(const std::string& library, const VersionInfo& version)
  {
    auto& lib_versions = get_library_versions();
    if(lib_versions.count(library) && (lib_versions[library] != version))
      throw Exception("Failed to set library version for " + library + " to " + version.to_string() + ": version already set to " + lib_versions[library].to_string());
    lib_versions[library] = version;
  }

  // NOTE: No lambda static init here. Version registration happens on first use.
} // namespace ngcore
