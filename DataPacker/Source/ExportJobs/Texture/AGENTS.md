# Texture Export Helpers

Texture transforms, intermediate-file I/O, RDO sweeps, and legacy-intermediate migration. The parent ExportJobs hub owns asset matching and final chunk routing.

## Intermediate Contract

`Texture::Save` writes a magic marker, dimensions, mip count, then the encoded payload. BCn and R16 intermediates are zlib-compressed on disk; half-float cubemap pre-pass output remains raw. Readers must use the shared parser and its returned payload offset rather than duplicating header math.

Final texture chunks use LZ4 and store compressed and uncompressed sizes for `FileManager`. Raw intermediate input is decoded as needed and re-encoded for the chunk; changing the intermediate or chunk contract requires coordinated producer, exporter-version, shared-header, and runtime-reader updates.

## Encoding and RDO

Mip generation, format conversion, underwater masking, and block compression are offline quality-sensitive work. Preserve deterministic inputs and the configured encoder path; the optional bc7e.ispc route remains disabled. `BT_DATAPACKER_FORBID_EXPENSIVE_EXPORT=1` fails before encoding in both the job entry point and shared RDO path.

RDO sweeps decode an existing intermediate through the shared parser and report every candidate encode through `LOG` only: they write no files and change nothing on disk. Keep them that way. `RunRdoSweepValidate` is the mode that measures the current production encoder settings against alternatives.

## Migration

`MigrateLegacyIntermediates()` runs before readers consume cached intermediates. It walks the tracked asset input directories (`gpFileManager->mpInputDirectories`), not the `%LOCALAPPDATA%` cache, and rewrites qualifying files in place. It is idempotent: files already carrying the magic marker are skipped. Migration validates the legacy shape and payload before that rewrite; preserve the ordering, because the write is not transactional and it is changing tracked files. Dimensions, mip count, texture format, and payload meaning must survive migration.

Eligibility is deliberately narrow: the inverse format map recognizes only the BC4, BC5, BC7, and R16 intermediate extensions, and everything else is skipped. The IBL (image-based lighting) `.R16G16B16A16_SFLOAT` cubemap intermediates are excluded on purpose — they are legacy-shaped by design and carry no magic marker, so they look exactly like unmarked files awaiting migration. Widening the format map would zlib-wrap and re-header raw half-float cube faces and silently break IBL.

## See Also

- `../Island/AGENTS.md` - island texture producers and masking order
- `../../../../Engine/Source/File/AGENTS.md` - runtime chunk decompression
