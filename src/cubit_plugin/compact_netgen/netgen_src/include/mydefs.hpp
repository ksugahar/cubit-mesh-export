// Modified from Netgen libsrc/include/mydefs.hpp at v6.2.2608 (96e5682f6ea43ba77ba3bb2ae4bb4bd1791f506e) plus upstream 13b47c3a9fb2e7d53ca4be7227bdd4acbd6c6e73 (pyramid apex clamp).
// Local modifications present in the source history on 2026-10-15.
// Netgen licensing and attribution are provided with the distribution.

#ifndef FILE_MYDEFS
#define FILE_MYDEFS

#include <core/ngcore.hpp>
#define PACKAGE_VERSION "6.2-dev"

namespace netgen
{
  // bit-reproducible math functions, hide the libm versions
  using ngcore::math::sin;
  using ngcore::math::cos;
  using ngcore::math::atan2;
  using ngcore::math::acos;
  using ngcore::math::cbrt;
}

// #define DEBUG

// Compact Netgen: static library, no dllexport/dllimport
#ifdef COMPACT_NETGEN_STATIC
  #define DLL_HEADER
#elif defined(nglib_EXPORTS)
  #define DLL_HEADER   NGCORE_API_EXPORT
#else
  #define DLL_HEADER   NGCORE_API_IMPORT
#endif

#ifndef __assume
#ifdef __GNUC__
#define __assume(cond) if (!(cond)) __builtin_unreachable(); else;
#else
#define __assume(cond)
#endif
#endif

#ifndef NG_INLINE
#ifdef __INTEL_COMPILER
#ifdef WIN32
#define NG_INLINE __forceinline inline
#else
#define NG_INLINE __forceinline inline
#endif
#else
#ifdef __GNUC__
#define NG_INLINE __attribute__ ((__always_inline__)) inline
#define VLA
#else
#define NG_INLINE inline
#endif
#endif
#endif

#define noDEMOVERSION
#define noDEVELOP
#define noSTEP
#define noSOLIDGEOM
#define noDEMOAPP
#define noMODELLER
#define noSTAT_STREAM
#define noLOG_STREAM

#endif
