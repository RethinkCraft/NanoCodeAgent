# Documentation Templates

This directory contains standardized templates for documentation generation. The `docgen-tutorial-update` skill selects the appropriate template based on the type of documentation being produced.

## Available Templates

| Template | Use Case |
|----------|----------|
| `docs/templates/tutorial_template.md` | Step-by-step learning guides for newcomers |
| `docs/templates/feature_update_template.md` | Documenting feature changes for existing users |
| `docs/templates/release_note_template.md` | Structured release notes |
| `docs/templates/troubleshooting_template.md` | Problem/solution pairs for common issues |

## Usage

1. The `docgen-tutorial-update` skill reads the relevant template.
2. The model fills in each section, grounding every claim in repository evidence.
3. The output passes through `docgen-fact-check` and `docgen-reviewer` before finalization.

## Guidelines

- **Do not remove template section markers** — they provide stable structure for human review and future automation.
- **Fact source priority** is embedded in each template header as a reminder.
- Templates are versioned; update the version footer when making structural changes.
- Add new templates here when a new documentation category is needed.
