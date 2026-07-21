# PipelineManager

**Global:** `gpPipelineManager`

Loads SPIR-V from pack chunks and owns fixed engine pipelines plus CRC-keyed dynamic collection pipelines. Validate shader metadata and byte ranges before module creation; pipelines retain pointers into the manager's stable shader map.

## Pipeline Families

Fixed pipelines implement renderer passes and utility compute work. Lighting follows deposit, spread, combine, then temporal accumulation; separable blur pipelines pre-process registered light textures.

Collections register dynamic pipelines by behavior and model role. These maps repopulate lazily after pipeline-tier recreation, so new renderable collections must use the established idempotent registration path rather than requiring command-buffer re-recording for each scene CRC.

## Descriptor Validity

Global command-buffer recording verifies cached texture descriptor snapshots. Texture creation and transferred-image adoption increment the live generation; destruction is detected through a null live image because it does not increment generation. Preserve both checks when changing texture lifetime or descriptor registration.
