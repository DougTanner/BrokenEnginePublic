# Reporting regression corpus

This tracked corpus is shared by `BoundAgentContextAndCapabilityReporting` and
`LinearRiskTieredCppChangeProcess`. It freezes retained session reports before
consumer-specific fixture packets are built.

Each completed set contains:

- `source-manifest.tsv`: ordinal-by-filename rows
  `<filename>\t<byte-count>\t<source-sha256>\n` for the ignored retained source
  reports. Its own SHA-256 is the approved frozen identity.
- `reports/`: strict UTF-8, LF/trailing-whitespace-normalized copies whose
  bodies replace worktree/user-home paths, GUIDs, and IPv4 addresses with stable
  placeholders. Source filenames remain manifest keys. These copies are test
  inputs, never process evidence.
- `normalized-manifest.tsv`: ordinal rows binding each normalized report's byte
  count and SHA-256.
- `decision-catalog.tsv`: report/ID occurrences, residual-section anchors, and
  bounded log-evidence blocks used to prove that packet fixtures cover every
  decision-driving item. Compact Markdown ID ranges are expanded so each ID is
  inventoried at the range's original source line. Contiguous `log-evidence`
  rows share a report-local `E###` ID, preserve one exact source line per row,
  and define the block's inclusive helper range. It is an inventory, not a
  compact-envelope locator; tests copy a report to the current worktree's
  `Temp/AgentReports/`, hash it, and construct exact line ranges there.

Frozen source identities:

| Set | Commit | Reports | Bytes | Source-manifest SHA-256 | State |
|---|---|---:|---:|---|---|
| DataPacker | `d730205ab92e91b48f46d29076f12165dde8dda5` | 21 | 112270 | `4655da5ac8ffc7db53eed3e8e8f58872f7bb9047f7677a103ecab3dde4b0fa33` | captured |
| PREfast | `98b5272c0cb836b822cc5692484107768426b103` | 51 | 328170 | `543e943a887335cafaaf75dfc573d04f544483ad49f7b254c7aa1a69a9508597` | captured |
| Save | `90f7ed3450b9318d89b3de74cb7e1271f12789ce` | 48 | 362650 | `7b462e9a720441dff0316c7c6fe1d8b6d70233df29c8d7e074b61aefc09f3120` | captured |
