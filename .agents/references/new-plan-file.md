# New Plan File

`.agents/scripts/New-PlanFile.ps1` is the repository-owned writer for one
executable Plan. It writes the immutable `broken-engine-plan/v1` metadata marker
at byte zero with a canonical `createdUtc` and a unique ordinal-sorted
`dependsOn`, copies the supplied body verbatim below it in BOM-less UTF-8,
refuses to overwrite an existing path, and folds `WorktreeCli plan validate`
into its result. Never reconstruct its marker, timestamp, encoding, dependency,
filename, overwrite, or validation operations inline.

## Invocation

```powershell
$RepositoryRoot = (git rev-parse --show-toplevel).Trim()
$Script = Join-Path $RepositoryRoot '.agents/scripts/New-PlanFile.ps1'
pwsh -NoProfile -File $Script -Area <existing area> -Name <PascalCase.md> -Body <body file path> -DependsOn <plan paths as one comma-separated token>
```

- `-Area` — an existing directory beneath `Documents/Plans`; the script creates
  none.
- `-Name` — a bare filename matching `^[A-Z][A-Za-z0-9]*\.md$`.
- `-Body` — a file holding the plan text that goes below the marker. The body
  travels as a file so no transcript text is ever executed.
- `-DependsOn` — `Documents/Plans/**/*.md` paths. `pwsh -File` hands every
  argument over as one literal string, so several dependencies can only travel
  as one comma-separated token. Omit the parameter entirely when the Plan has no
  dependencies: the script rejects a blank entry and defaults to an empty
  dependency list only when the parameter is absent.

## Result

Parse the single `broken-engine-new-plan-file/v1` JSON object on stdout. It
carries `schemaVersion`, `status`, `code`, `message`, `plan`, `createdUtc`,
`dependsOn`, `written`, `validation` (the folded `plan validate` `exitCode`,
`status`, `code`, `message`, `diagnostics`, and `notices`), and `truncated`.

Exit `0` with a passing `status` is the only outcome reportable as created. Exit
`1` (`error`) and exit `2` (`blocked`) both block reporting the Plan as created;
report the returned `code` and `message`. Whether a file exists on disk is
`written`, not the exit code: a validation failure leaves the written file in
place, so correct the body and revalidate instead of recreating the Plan or
rewriting the file by hand.

`truncated` `true` means a cap applied — 64 `dependsOn` entries, 16 diagnostics
or notices, a 256-character message, or a `validation` projection dropped from
an oversized envelope — so the reported detail is incomplete.
