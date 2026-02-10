#pragma once

#if defined(APPLE)

namespace ff::desktop {
class Runtime;
}

namespace ff::diagnostics {
class Reporter;
}

int runMacDesktopApp(ff::desktop::Runtime* runtime,
                     ff::diagnostics::Reporter* diagnostics);

#endif
