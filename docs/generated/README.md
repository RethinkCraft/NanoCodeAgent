# Generated Documentation

This directory holds machine-generated documentation artifacts. **Do not hand-edit files here** — they are produced by the docgen workflow and may be overwritten.

## Expected Outputs

| File | Produced By | Description |
|------|-------------|-------------|
| docs/generated/repo_map_output.md | `scripts/docgen/repo_map.py` / `scripts/docgen/run_repo_understanding.sh` | Repository structure snapshot used by repo-understanding |
| docs/generated/doc_inventory_output.md | `scripts/docgen/doc_inventory.py` / `scripts/docgen/run_repo_understanding.sh` | Documentation inventory and staleness snapshot |
| docs/generated/example_inventory_output.md | `scripts/docgen/example_inventory.py` / `scripts/docgen/run_repo_understanding.sh` | Example and code-block inventory for tutorial planning |
| docs/generated/repo_understanding_summary.md | `docgen-repo-understanding` | Global project semantic model |
| docs/generated/change_impact_summary.md | `docgen-change-impact` | Change-to-doc impact mapping |
| docs/generated/fact_check_report.md | `docgen-fact-check` | Factual verification report |
| docs/generated/reviewer_report.md | `docgen-reviewer` | Teaching quality review |

## Workflow

1. Run `scripts/docgen/run_repo_understanding.sh` or the individual inventory scripts to produce docs/generated/repo_map_output.md, docs/generated/doc_inventory_output.md, and docs/generated/example_inventory_output.md.
2. Feed those prep artifacts into `docgen-repo-understanding` to create docs/generated/repo_understanding_summary.md.
3. Run `scripts/docgen/changed_context.py` → feed to `docgen-change-impact` → docs/generated/change_impact_summary.md.
4. Run `docgen-tutorial-update` → produce or update the target doc.
5. Run verification scripts → feed to `docgen-fact-check` → docs/generated/fact_check_report.md.
6. Run `docgen-reviewer` → docs/generated/reviewer_report.md.

## Version Control

- These files may be committed for reference or excluded via `.gitignore` depending on team preference.
- When committed, they serve as a snapshot of the last documentation analysis.
