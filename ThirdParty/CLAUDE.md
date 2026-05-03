# ThirdParty

External libraries. **DO NOT modify** the source code of existing libraries — keep upstream pristine so updates can be re-imported cleanly.

Adding a *new* library is allowed, but only with **explicit user approval** and only if it satisfies the License Policy below.

## License Policy

Only **permissive open-source licenses** that allow closed-source commercial redistribution. Acceptable:

- MIT / X11
- BSD-2-Clause, BSD-3-Clause
- Apache-2.0
- zlib / libpng
- ISC
- Unlicense / public domain / CC0

**NO viral (copyleft) licenses.** Reject any library distributed under:

- GPL (any version) — strong copyleft
- AGPL — strong copyleft, extends to network use
- LGPL — weak copyleft (still impractical for static-linked engine code)
- MPL, EPL, CDDL — file-level copyleft
- SSPL, "source-available", or non-commercial-only licenses

When adding a new library:

1. Read its `LICENSE` / `COPYING` *before* importing.
2. If multi-licensed (see lz4 caveat below), import only the permissive parts. **Never add copyleft-licensed files to a `.vcxproj`.**

## lz4 caveat

`lz4` ships under two licenses:

- `lz4/lib/**` — **BSD-2-Clause** (the only portion linked into the engine)
- `lz4/programs/**`, `lz4/tests/**`, `lz4/examples/**` — **GPL-2.0-or-later** (the standalone CLI utility and benchmarks)

Never reference files outside `lz4/lib/` from any `.vcxproj`.
