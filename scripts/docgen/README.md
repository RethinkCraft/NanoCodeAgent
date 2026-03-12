# Docgen Scripts

Deterministic scripts that support the documentation automation workflow. These scripts handle scanning, extraction, and verification — tasks that should NOT be left to the model to guess.

## Script Inventory

| Script | Purpose | Status | Blocking? |
|--------|---------|--------|-----------|
| `scripts/docgen/setup.sh` | Verify environment prerequisites (Python 3, git, optional pytest notice) | Functional | Yes |
| `scripts/docgen/run_repo_understanding.sh` | Unified entry point for repo-understanding pipeline | Functional | Yes |
| `scripts/docgen/repo_map.py` | Generate repository structure map | Functional | Yes |
| `scripts/docgen/doc_inventory.py` | Index documentation files and assess staleness | Functional | Yes |
| `scripts/docgen/example_inventory.py` | Map examples to tutorial sections | Functional | Yes |
| `scripts/docgen/changed_context.py` | Collect context around changed files | Functional | Yes |
| `scripts/docgen/verify_paths.py` | Check that documented paths exist | Functional | Yes |
| `scripts/docgen/verify_links.py` | Validate internal Markdown links and anchors | Functional | Yes |
| `scripts/docgen/verify_doc_consistency.py` | Check path/script references against repo | Minimal | No |
| `scripts/docgen/verify_commands.py` | Validate shell command syntax via `bash -n` | Minimal | No |

### Status Legend

- **Functional** — complete implementation, exit codes reliable.
- **Minimal** — core checks implemented, some verification categories NOT_IMPLEMENTED.

### Blocking vs Non-Blocking

**Blocking** scripts must pass (exit 0) for the pipeline to continue. If a blocking script fails, the pipeline halts and the error must be fixed.

**Non-blocking** scripts report findings but do not gate the pipeline. Their output is still printed, but `scripts/docgen/run_repo_understanding.sh` does not fail the workflow on their exit codes.

## Unified Entry Point

```bash
bash scripts/docgen/run_repo_understanding.sh
```

This runs the deterministic repo-understanding prep pipeline in order:
1. Read AGENTS.md documentation automation rules (targeted `sed` extract).
2. Verify skill file exists.
3. Run `scripts/docgen/repo_map.py`, `scripts/docgen/doc_inventory.py`, and `scripts/docgen/example_inventory.py`.
4. Check whether `docs/generated/repo_understanding_summary.md` already exists.
5. Run blocking verifiers (`verify_paths.py`, `verify_links.py`) if the summary exists.
6. Run non-blocking verifiers (`verify_doc_consistency.py`, `verify_commands.py`) if the summary exists.

The script does not synthesize `repo_understanding_summary.md` itself; it prepares the deterministic inputs and verifies an existing summary.

## Common Interface

All Python scripts share a small set of conventions:

- `--root`: repository root directory (default: current working directory).
- Exit code `0`: success. Exit code `1`: errors found or invalid input.

The script families then add their own arguments:

```bash
python3 scripts/docgen/<inventory>.py [--root <repo-root>] [--output <path>]
python3 scripts/docgen/changed_context.py [--root <repo-root>] [--ref <git-ref>] [--output <path>]
python3 scripts/docgen/verify_<name>.py <doc-file> [--root <repo-root>]
```

## Local Reproduction

```bash
# Prerequisites
bash scripts/docgen/setup.sh

# Full pipeline
bash scripts/docgen/run_repo_understanding.sh

# Individual scripts
python3 scripts/docgen/repo_map.py --root .
python3 scripts/docgen/doc_inventory.py --root .
python3 scripts/docgen/example_inventory.py --root .
python3 scripts/docgen/repo_map.py --root . --output docs/generated/repo_map_output.md
python3 scripts/docgen/doc_inventory.py --root . --output docs/generated/doc_inventory_output.md
python3 scripts/docgen/example_inventory.py --root . --output docs/generated/example_inventory_output.md
python3 scripts/docgen/verify_paths.py README.md --root .
python3 scripts/docgen/verify_links.py README.md --root .
python3 scripts/docgen/verify_commands.py README.md --root .
python3 scripts/docgen/verify_doc_consistency.py README.md --root .

# Regression tests
python3 -m unittest tests.test_docgen_verify tests.test_docgen_verify_consistency_links
```

`setup.sh` verifies tool availability only. It reports whether `pytest` is present,
but the bundled regression tests run with `unittest` and do not require extra packages.

## Relationship to Skills

Scripts provide **data**; skills provide **judgment**:

1. Scripts scan, extract, and verify deterministically.
2. Skills consume script output and make decisions about what to write or update.
3. The model does the actual understanding, synthesis, and writing.

See `AGENTS.md` → "Responsibility Split" for the full breakdown.
