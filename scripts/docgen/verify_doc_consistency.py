#!/usr/bin/env python3
"""verify_doc_consistency.py — Check doc claims against repository artifacts.

Purpose:
    Verify that file paths, script references, and local Markdown link targets
    mentioned in documentation exist in the repository. Absolute paths and path
    references under ``scripts/`` and ``.agents/`` are treated as errors
    (exit 1); other missing paths are warnings only.

Usage:
    python3 scripts/docgen/verify_doc_consistency.py <doc-file> [--root <repo-root>]

Arguments:
    doc-file  Path to the Markdown file to check.
    --root    Repository root directory (default: cwd).

Exit codes:
    0 — no errors (warnings are OK)
    1 — one or more errors detected

NOT_IMPLEMENTED:
    - CLI argument verification (--help parsing)
    - Config key / default-value verification
    - Environment variable verification

Status: NON-BLOCKING — path / script existence checks only.
"""

import argparse
import os
import sys

try:
    from . import path_utils
except ImportError:
    import path_utils


# Prefixes whose absence is an error, not just a warning.
ERROR_PREFIXES = ("scripts/", ".agents/")


def extract_filenames(content: str) -> list[str]:
    """Extract filenames mentioned in backticks."""
    return path_utils.extract_backtick_repo_references(content)


def extract_link_references(
    content: str, doc_file: str, root: str
) -> tuple[list[tuple[str, str]], list[str], list[str]]:
    """Extract local Markdown link targets and resolve them for existence checks."""
    references: dict[str, str] = {}
    escaped: set[str] = set()
    absolute: set[str] = set()

    for target in path_utils.extract_markdown_link_targets(content):
        if path_utils.is_absolute_reference(target):
            absolute.add(target)
            continue
        reference, full_path, in_repo = path_utils.resolve_doc_relative_target(
            root, doc_file, target
        )
        if not in_repo:
            escaped.add(target)
            continue
        references.setdefault(reference, full_path)

    return sorted(references.items()), sorted(escaped), sorted(absolute)


def main() -> None:
    parser = argparse.ArgumentParser(description="Check doc consistency with repo.")
    parser.add_argument("doc_file", help="Markdown file to check.")
    parser.add_argument("--root", default=".", help="Repository root directory.")
    args = parser.parse_args()

    root = os.path.abspath(args.root)

    if not os.path.isfile(args.doc_file):
        print(f"ERROR: {args.doc_file} not found.", file=sys.stderr)
        sys.exit(1)

    with open(args.doc_file, "r", encoding="utf-8") as f:
        content = f.read()

    errors: list[str] = []
    warnings: list[str] = []
    references: dict[str, str] = {}

    for filename in extract_filenames(content):
        if path_utils.is_absolute_reference(filename):
            errors.append(filename)
            print(f"  ERROR  {filename} — absolute path forbidden")
            continue
        references[filename] = os.path.join(root, filename)

    link_references, escaped_links, absolute_links = extract_link_references(
        content, args.doc_file, root
    )
    references.update(link_references)

    for target in absolute_links:
        errors.append(target)
        print(f"  ERROR  {target} — absolute path forbidden")

    for target in escaped_links:
        errors.append(target)
        print(f"  ERROR  {target} — escapes repo root")

    for fname, full in sorted(references.items()):
        if os.path.exists(full):
            continue
        is_error = any(fname.startswith(p) for p in ERROR_PREFIXES)
        if is_error:
            errors.append(fname)
            print(f"  ERROR  {fname} — not found in repo")
        else:
            warnings.append(fname)
            print(f"  WARN   {fname} — not found (non-critical)")

    # Report NOT_IMPLEMENTED checks
    print(
        "\nNOT_IMPLEMENTED: CLI argument, config key, and environment variable checks."
    )

    if errors:
        print(
            f"\nFAILED: {len(errors)} error(s), {len(warnings)} warning(s) in {args.doc_file}."
        )
        sys.exit(1)
    elif warnings:
        print(f"\nPASSED with {len(warnings)} warning(s) in {args.doc_file}.")
    else:
        print(f"\nOK: all references in {args.doc_file} appear consistent.")


if __name__ == "__main__":
    main()
