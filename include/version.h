#ifndef GREG_VERSION_H
#define GREG_VERSION_H

// GREG_VERSION is normally injected at compile time by the Makefile via
// `-DGREG_VERSION=\"...\"`, derived dynamically from `git describe` or the
// VERSION file (see Makefile). This fallback only kicks in for builds that
// bypass the Makefile (e.g. an IDE's background syntax check).
#ifndef GREG_VERSION
#define GREG_VERSION "0.0.0-dev"
#endif

#endif // GREG_VERSION_H
