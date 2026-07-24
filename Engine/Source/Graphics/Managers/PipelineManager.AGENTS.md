# PipelineManager

Global: `gpPipelineManager`

Loads SPIR-V from pack chunks and owns fixed engine pipelines plus CRC-keyed dynamic collection pipelines. Validate shader metadata and byte ranges before module creation; pipelines retain pointers into the manager's stable shader map.

## Pipeline Families

Fixed pipelines implement renderer passes and utility compute work. Lighting follows deposit, spread, combine, then temporal accumulation; separable blur pipelines pre-process registered light textures.

Collections register dynamic pipelines by behavior and model role. These maps repopulate lazily after pipeline-tier recreation, so new renderable collections must use the established idempotent registration path rather than requiring command-buffer re-recording for each scene CRC.

## Descriptor Registration

Pipeline construction registers texture consumers with the texture-descriptor registry, which owns descriptor-generation verification. A full pipeline rebuild clears raw pipeline back-references, but rebuilt bindless consumers must repopulate descriptors for live island slots so a lazy channel pending across recreation reaches the rebuilt pipeline when it later adopts.
