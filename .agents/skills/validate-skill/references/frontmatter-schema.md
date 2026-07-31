# Repository Skill Package v2

This is the authoritative schema for Claude-facing `SKILL.md` frontmatter and optional Codex `agents/openai.yaml`. Their invocation controls are independent. Both intentionally use constrained YAML subsets.

## Document shape

- Encode the file as UTF-8. Use LF or CRLF line endings; do not use bare CR.
- Put an exact `---` opening delimiter on line 1 and an exact `---` closing delimiter after the frontmatter.
- Put at least one non-whitespace character in the Markdown body after the closing delimiter.
- Write each key once at top level as `key: value`.
- Use a plain scalar, a single- or double-quoted scalar, an exact lowercase boolean, a flow list, or the folded-text marker `>-` according to the field table.
- For `>-`, indent every nonblank content line and stop at the next top-level key or closing delimiter.
- Do not use comments, block lists, mappings, anchors, aliases, tags, arbitrary nesting, or other YAML block styles.

## Fields

| Key | Required | Accepted form | Constraint |
|---|---:|---|---|
| `name` | yes | text scalar | Equals parent directory; lowercase letters, digits, and single internal hyphens; at most 64 characters |
| `description` | yes | text scalar or `>-` | Nonempty; at most 1,024 characters |
| `when_to_use` | no | text scalar or `>-` | Nonempty; combined with `description` at most 1,536 characters |
| `allowed-tools` | no | flow list | String items |
| `paths` | no | flow list | Nonempty string items |
| `argument-hint` | no | text scalar | Nonempty |
| `disable-model-invocation` | no | boolean | Exact `true` or `false` |
| `user-invocable` | no | boolean | Exact `true` or `false` |
| `context` | no | enum | `fork` |
| `agent` | no | text scalar | Nonempty; requires `context: fork`, and `context: fork` requires `agent` |
| `model` | no | text scalar | Nonempty |
| `effort` | no | enum | `low`, `medium`, `high`, `xhigh`, or `max` |
| `shell` | no | enum | `bash` or `powershell` |

`arguments` and `hooks` are deliberately unsupported. Extend this schema and validator together when a repository skill first needs either field.

`disallowed-tools` is banned outright, not merely unsupported. It removes tools from the *invoking* context for the remainder of the turn, so a skill written for a delegated reviewer strips the caller's own delegation, editing, and user-interview ability when invoked inline — silently, and without leaving the caller a way to ask about it. State a skill's tool bounds in its body instead. Note that omitting a tool from `allowed-tools` restricts nothing; that field only pre-approves.

## Scalar and list forms

Plain scalars occupy the remainder of one line after `:`. Quoted scalars may use YAML-style doubled single quotes or the double-quoted escapes `\"`, `\\`, `\n`, `\r`, `\t`, and `\uFFFF`. A quoted scalar must close on the same line.

Flow lists use brackets and comma-separated scalar items, for example:

```yaml
allowed-tools: [Read, Edit, "Bash(git diff *)"]
paths: ["**/*.vert", "**/*.frag"]
```

Nested lists or mappings are invalid. `argument-hint` remains a text scalar even when its text contains brackets, such as `[filename]`.

Folded text uses `>-`; consecutive nonblank lines fold with spaces and blank lines preserve paragraph breaks. Length checks apply to the parsed text, not indentation.

## Bundled links

Markdown links whose relative destination begins with `references/`, `scripts/`, or `assets/` must resolve beneath the skill directory. URL fragments and query strings do not participate in the filesystem check. Absolute paths, URI destinations, and links outside those bundled directories are not bundled links under this rule.

## Codex companion file

`interface`, `policy`, and `dependencies` are independently optional top-level objects in `agents/openai.yaml`; at least one must exist. Quote every string, indent with spaces, and omit comments. A present object requires:

- `interface`: nonempty `display_name` and 25–64-character `short_description`; optional nonempty `icon_small`, `icon_large`, `brand_color`, and `default_prompt`. Icons resolve inside the skill, color is `#RRGGBB`, and a default prompt names `$skill-name`.
- `policy`: exact boolean `allow_implicit_invocation`. `false` disables Codex implicit discovery but preserves explicit invocation.
- `dependencies`: nonempty `tools`; each item has quoted `type: "mcp"`, `value`, and `description`, with optional quoted `transport` and HTTPS `url`.

This companion file supplies Codex UI, discovery policy, and dependency wiring. Body tool and delegation bounds remain authoritative. Never infer that Claude `disable-model-invocation: true` requires a Codex companion file.

## Result contract

The mechanical command accepts exactly one target:

```powershell
pwsh -NoProfile -File .agents/skills/validate-skill/scripts/Validate-Skill.ps1 -Path <skill-directory-or-SKILL.md>
```

Repository targets must be below `.agents/skills/`. Pass `-Fixture` only for a disposable external fixture.

- `VALID <path>` with exit `0`: every mechanical check passed.
- One or more line-ordered `INVALID <path>:<line> <code>: <message>` diagnostics with exit `1`: target content is invalid.
- `SETUP_ERROR <code>: <message>` with exit `2`: invocation, path resolution, file read, or internal validation failed.

For validator changes, create disposable packages under a temporary directory; never track fixtures. Require `VALID`/0 from a valid `-Fixture` package, `INVALID`/1 after corrupting a known field, `SETUP_ERROR`/2 for a nonexistent target, and `VALID`/0 from the repository self-check.
