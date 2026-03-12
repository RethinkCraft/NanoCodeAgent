#!/usr/bin/env bash
# scripts/docgen/run_repo_understanding.sh
#
# Unified entry point for the repo-understanding docgen workflow.
# Executes the fixed pipeline: read rules → read skill → run scripts → verify.
#
# Usage:
#   ./scripts/docgen/run_repo_understanding.sh
#
# Exit codes:
#   0 — deterministic prep succeeded (and summary verification passed if run)
#   1 — a blocking step failed

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$REPO_ROOT"

GENERATED="docs/generated"
mkdir -p "$GENERATED"

log() { echo ">>> [$(date +%H:%M:%S)] $*"; }
fail() { echo "!!! FAIL: $*" >&2; exit 1; }

# ── Step 1: Read AGENTS.md Documentation Automation Rules ──────────────
log "Step 1: Reading Documentation Automation Rules from AGENTS.md"
RULES=$(sed -n '/## Documentation Automation Rules/,/## AI Customization Layout/p' AGENTS.md)
if [ -z "$RULES" ]; then
    fail "Heading '## Documentation Automation Rules' not found in AGENTS.md. Cannot proceed."
fi
echo "$RULES" | head -5
log "Step 1: OK — rules section found ($(echo "$RULES" | wc -l) lines)"

# ── Step 2: Read skill definition ─────────────────────────────────────
SKILL_FILE=".agents/skills/docgen-repo-understanding/SKILL.md"
log "Step 2: Reading skill definition from $SKILL_FILE"
if [ ! -f "$SKILL_FILE" ]; then
    fail "$SKILL_FILE not found."
fi
head -5 "$SKILL_FILE"
log "Step 2: OK — skill file exists"

# ── Step 3: Run repo_map.py ───────────────────────────────────────────
log "Step 3: Running repo_map.py"
python3 scripts/docgen/repo_map.py --root . --output "$GENERATED/repo_map_output.md"
log "Step 3: OK — wrote $GENERATED/repo_map_output.md"

# ── Step 4: Run doc_inventory.py ──────────────────────────────────────
log "Step 4: Running doc_inventory.py"
python3 scripts/docgen/doc_inventory.py --root . --output "$GENERATED/doc_inventory_output.md"
log "Step 4: OK — wrote $GENERATED/doc_inventory_output.md"

# ── Step 5: Run example_inventory.py ──────────────────────────────────
log "Step 5: Running example_inventory.py"
python3 scripts/docgen/example_inventory.py --root . --output "$GENERATED/example_inventory_output.md"
log "Step 5: OK — wrote $GENERATED/example_inventory_output.md"

# ── Step 6: Placeholder for repo_understanding_summary.md ─────────────
# This step requires model synthesis. In non-interactive mode, the script
# prepares inputs but cannot generate the summary itself.
SUMMARY="$GENERATED/repo_understanding_summary.md"
SUMMARY_PRESENT=0
log "Step 6: Summary generation"
if [ -f "$SUMMARY" ]; then
    SUMMARY_PRESENT=1
    log "Step 6: Found existing $SUMMARY — will verify it"
else
    log "Step 6: SKIP — $SUMMARY not found. Generate it via Codex IDE/App or codex exec."
    log "  Hint: ask the model to read the outputs from steps 3-5 and synthesize"
    log "  the summary following the docgen-repo-understanding skill format."
fi

# ── Step 7: Verification ──────────────────────────────────────────────
log "Step 7: Running verification scripts"

VERIFY_FAILED=0

if [ "$SUMMARY_PRESENT" -eq 1 ]; then
    log "  7a: verify_paths.py on $SUMMARY"
    if ! python3 scripts/docgen/verify_paths.py "$SUMMARY" --root .; then
        echo "  ⚠ verify_paths FAILED (blocking)"
        VERIFY_FAILED=1
    fi

    log "  7b: verify_links.py on $SUMMARY"
    if ! python3 scripts/docgen/verify_links.py "$SUMMARY" --root .; then
        echo "  ⚠ verify_links FAILED (blocking)"
        VERIFY_FAILED=1
    fi

    log "  7c: verify_doc_consistency.py on $SUMMARY (non-blocking)"
    python3 scripts/docgen/verify_doc_consistency.py "$SUMMARY" --root . || true

    log "  7d: verify_commands.py on $SUMMARY (non-blocking)"
    python3 scripts/docgen/verify_commands.py "$SUMMARY" --root . || true
else
    log "  Skipping verification — no summary file to verify"
fi

# ── Step 8: Verification note ─────────────────────────────────────────
if [ "$SUMMARY_PRESENT" -eq 1 ]; then
    log "Step 8: OK — absolute path validation is handled by verify_paths.py"
fi

# ── Result ────────────────────────────────────────────────────────────
echo ""
if [ "$VERIFY_FAILED" -ne 0 ]; then
    fail "One or more blocking checks failed. See above."
fi
if [ "$SUMMARY_PRESENT" -eq 1 ]; then
    log "Repo-understanding prep and summary verification completed successfully."
else
    log "Deterministic repo-understanding prep completed successfully."
    log "Generate $SUMMARY from the outputs above, then rerun this script to verify it."
fi
