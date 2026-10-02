# Branching strategy

Trunk-based, short-lived branches.

| Branch | Purpose |
|---|---|
| `main` | Always builds, always passes CI. Protected; changes only via PR. |
| `feat/P<nn>.<nn>-<short-name>` | One build-plan task, e.g. `feat/P10.20-cmake-superbuild` |
| `fix/<short-name>` | Bug fix |
| `docs/<short-name>` | Documentation only |

## Rules
- Branch from `main`; keep branches < 1 week.
- Merge via PR with **squash merge**; PR title = commit message.
- Commit/PR title format: `P10.20: CMake superbuild with presets` (task ID first; `fix:` / `docs:` otherwise).
- Run `clang-format` before committing.
- Releases: annotated tags `vMAJOR.MINOR.PATCH` on `main` (V1 release = `v1.0.0`).
- Hotfix on a release: branch `release/v1.0.x` from the tag, fix, tag `v1.0.1`, cherry-pick to `main`.
