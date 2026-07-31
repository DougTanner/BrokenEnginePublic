# Explicit Primary Commit

Load only for an explicitly requested non-session commit on the primary
checkout. Require primary checkout identity and a clean authorized scope.
`../scripts/Invoke-FinalizeCandidateCommit.ps1 -Route primary-commit` uses a temporary
index to build the commit without moving the primary ref or real index.

After `/verify-changes` binds acceptance to that diff, present the primary
summary and the canonical confirmation from the skill's `## Landing
confirmation` section. Only its affirmative response permits resuming the
landing script with `-AdvancePrimary`. Advance the named ref by guarded CAS with
guarded rollback on postcondition failure. When the commit changes
`Documents/Plans/**`, run WorktreeCli `plan validate` afterwards.
