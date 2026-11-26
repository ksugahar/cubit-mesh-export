// Modified from Netgen libsrc/core/statushandler.cpp at v6.2.2608 (96e5682f6ea43ba77ba3bb2ae4bb4bd1791f506e) plus upstream 13b47c3a9fb2e7d53ca4be7227bdd4acbd6c6e73 (pyramid apex clamp).
// Local modifications present in the source history on 2026-10-15.
// Netgen licensing and attribution are provided with the distribution.

// statushandler_patched.cpp — Compact Netgen
// All file-scope statics converted to function-local statics (lazy init)
// to avoid DLL DllMain static initialization issues.

#include "array.hpp"
#include "statushandler.hpp"

namespace ngcore
{
  volatile multithreadt multithread;

  multithreadt :: multithreadt()
  {
    pause = 0;
    testmode = 0;
    redraw = 0;
    drawing = 0;
    terminate = 0;
    running = 0;
    percent = 0;
    task = "";
  }

  static Array<double>& get_threadpercent_stack() {
    static Array<double> s(0);
    return s;
  }
  void SetThreadPercent(double percent)
  {
    multithread.percent = percent;
    if (get_threadpercent_stack().Size() > 0)
      get_threadpercent_stack().Last() = percent;
  }

}
