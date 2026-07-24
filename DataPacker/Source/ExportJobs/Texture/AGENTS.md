# Texture Export Helpers

Texture transforms, intermediate-file I/O, RDO sweeps, and legacy-intermediate migration. The parent ExportJobs hub (`../AGENTS.md`) owns asset matching and final chunk routing.

## Intermediate Contract

`Texture::Save` writes a magic marker, dimensions, mip count, then the encoded payload. BCn and R16 intermediates are zlib-compressed on disk; half-float cubemap pre-pass output remains raw. Readers must use the shared parser and its returned payload offset rather than duplicating header math.

Final texture chunks use LZ4 and store compressed and uncompressed sizes for `FileManager`. Raw intermediate input is decoded as needed and re-encoded for the chunk; changing the intermediate or chunk contract requires coordinated producer, exporter-version, shared-header, and runtime-reader updates.

## Encoding and RDO

Mip generation, format conversion, underwater masking, and block compression are offline quality-sensitive work. Preserve deterministic inputs and the configured encoder path; the optional bc7e.ispc route remains disabled. `BT_DATAPACKER_FORBID_EXPENSIVE_EXPORT=1` fails before encoding in both the job entry point and shared RDO path.

RDO sweeps compare candidate encodes using the same intermediate parser and current encoder settings. Keep sweep diagnostics outside tracked asset directories.

## Migration

`MigrateLegacyIntermediates()` runs before readers consume cached intermediates. It is idempotent: marked files are skipped, while valid unmarked files are rewritten to the current header/encoding contract. Migration validates the legacy shape and payload before its in-place rewrite; preserve that ordering because the write is not transactional. Dimensions, mip count, texture format, and payload meaning must survive migration.

## See Also

- `../Island/AGENTS.md` - island texture producers and masking order
- `../../../../Engine/Source/File/AGENTS.md` - runtime chunk decompression
