#!/usr/bin/env python3
"""verify_links.py — Check internal Markdown links and anchors.

Purpose:
    Scan a Markdown document for internal links (relative paths and anchors)
    and verify they resolve to valid targets.

Usage:
    python3 scripts/docgen/verify_links.py <doc-file> [--root <repo-root>]

Arguments:
    doc-file  Path to the Markdown file to check.
    --root    Repository root directory (default: cwd).

Exit codes:
    0 — all links valid
    1 — one or more broken links
"""

import argparse
import os
import re
import sys

try:
    from . import path_utils
except ImportError:
    import path_utils


def extract_links(content: str) -> list[dict]:
    """Extract Markdown links with their targets."""
    links: list[dict] = []
    for match in re.finditer(r"\[([^\]]*)\]\(([^)]+)\)", content):
        text = match.group(1)
        target = match.group(2).strip()
        if target.startswith("#") or not path_utils.is_external_uri(target):
            links.append({"text": text, "target": target, "pos": match.start()})
    return links


def slugify_heading(heading: str) -> str:
    """Convert a Markdown heading to a simplified GitHub-style anchor slug."""
    slug = re.sub(r"[^\w\s-]", "", heading.lower())
    return re.sub(r"\s+", "-", slug).strip("-")


def collect_anchors(content: str) -> set[str]:
    """Collect unique heading anchors, accounting for duplicate headings."""
    anchors: set[str] = set()
    seen: dict[str, int] = {}

    for line in content.splitlines():
        if not line.startswith("#"):
            continue

        heading = line.lstrip("#").strip()
        base_slug = slugify_heading(heading)
        if not base_slug:
            continue

        count = seen.get(base_slug, 0)
        slug = base_slug if count == 0 else f"{base_slug}-{count}"
        anchors.add(slug)
        seen[base_slug] = count + 1

    return anchors


def check_anchor(filepath: str, anchor: str) -> bool:
    """Check if a heading anchor exists in a Markdown file."""
    if not os.path.isfile(filepath):
        return False
    try:
        with open(filepath, "r", encoding="utf-8") as f:
            content = f.read()
    except OSError:
        return False

    return anchor in collect_anchors(content)


def main() -> None:
    parser = argparse.ArgumentParser(description="Verify internal Markdown links.")
    parser.add_argument("doc_file", help="Markdown file to check.")
    parser.add_argument("--root", default=".", help="Repository root directory.")
    args = parser.parse_args()

    root = os.path.abspath(args.root)

    if not os.path.isfile(args.doc_file):
        print(f"ERROR: {args.doc_file} not found.", file=sys.stderr)
        sys.exit(1)

    with open(args.doc_file, "r", encoding="utf-8") as f:
        content = f.read()

    links = extract_links(content)
    errors: list[str] = []

    for link in links:
        target = link["target"]
        path_part, _, anchor = target.partition("#")

        if path_part:
            if path_utils.is_absolute_reference(path_part):
                errors.append(f"absolute path forbidden: {target}")
                continue

            _, full_path, in_repo = path_utils.resolve_doc_relative_target(
                root, args.doc_file, path_part
            )
            if not in_repo:
                errors.append(f"broken path: {target} (escapes repo root)")
                continue
            if not os.path.exists(full_path):
                errors.append(f"broken path: {target}")
                continue
            if anchor and not check_anchor(full_path, anchor):
                errors.append(f"broken anchor: {target}")
        elif anchor:
            # Same-file anchor
            if not check_anchor(os.path.abspath(args.doc_file), anchor):
                errors.append(f"broken anchor: #{anchor}")

    if errors:
        print(f"FAIL: {len(errors)} broken link(s) in {args.doc_file}:")
        for e in errors:
            print(f"  - {e}")
        sys.exit(1)
    else:
        print(f"OK: all {len(links)} link(s) in {args.doc_file} are valid.")


if __name__ == "__main__":
    main()
