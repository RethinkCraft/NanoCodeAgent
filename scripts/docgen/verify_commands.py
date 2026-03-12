#!/usr/bin/env python3
"""verify_commands.py — Validate shell commands referenced in documentation.

Purpose:
    Extract shell commands from fenced code blocks in a Markdown document
    and validate their syntax using ``bash -n``.

Usage:
    python3 scripts/docgen/verify_commands.py <doc-file> [--root <repo-root>]

Arguments:
    doc-file   Path to the Markdown file to check.
    --root     Repository root directory (default: cwd).

Exit codes:
    0 — all commands pass syntax check
    1 — one or more syntax errors detected

Status: NON-BLOCKING — syntax-only validation via ``bash -n``.
    Deep semantic validation (e.g. verifying that referenced executables exist)
    is NOT_IMPLEMENTED and deferred to a future milestone.
"""

import argparse
import os
import re
import subprocess
import sys
import tempfile


HEREDOC_RE = re.compile(r"<<-?\s*(['\"]?)([A-Za-z_][A-Za-z0-9_]*)\1")


def extract_shell_blocks(content: str) -> list[tuple[str, str]]:
    """Extract shell-like fenced code blocks and their fence language."""
    blocks: list[tuple[str, str]] = []
    pattern = re.compile(r"```(bash|sh|shell|console)\s*\n(.*?)```", re.DOTALL)
    for match in pattern.finditer(content):
        blocks.append((match.group(1), match.group(2).strip()))
    return blocks


def extract_heredoc_delimiters(command: str) -> list[str]:
    """Extract heredoc delimiters that require following body lines."""
    return [match.group(2) for match in HEREDOC_RE.finditer(command)]


def strip_prompts(block: str, prompt_only: bool = False) -> str:
    """Normalize shell snippets by trimming prompts and optional console output."""
    lines: list[str] = []
    pending_heredocs: list[str] = []

    for line in block.splitlines():
        raw = line.rstrip()
        stripped = raw.strip()

        if prompt_only and pending_heredocs and not stripped.startswith(("$ ", "> ")):
            lines.append(raw)
            if stripped == pending_heredocs[0]:
                pending_heredocs.pop(0)
            continue

        if not stripped:
            continue

        if stripped.startswith("$ "):
            stripped = stripped[2:]
        elif stripped.startswith("> "):
            stripped = stripped[2:]
        elif prompt_only:
            continue
        elif stripped.startswith("#"):
            continue

        if stripped:
            lines.append(stripped)
            pending_heredocs.extend(extract_heredoc_delimiters(stripped))
    return "\n".join(lines)


def check_syntax(script_text: str) -> tuple[bool, str]:
    """Run ``bash -n`` on *script_text* and return (ok, stderr)."""
    with tempfile.NamedTemporaryFile(
        mode="w", suffix=".sh", delete=True, encoding="utf-8"
    ) as tmp:
        tmp.write(script_text + "\n")
        tmp.flush()
        result = subprocess.run(
            ["bash", "-n", tmp.name],
            capture_output=True,
            text=True,
        )
    return result.returncode == 0, result.stderr.strip()


def main() -> None:
    parser = argparse.ArgumentParser(description="Verify commands in documentation.")
    parser.add_argument("doc_file", help="Markdown file to check.")
    parser.add_argument("--root", default=".", help="Repository root directory.")
    args = parser.parse_args()

    if not os.path.isfile(args.doc_file):
        print(f"ERROR: {args.doc_file} not found.", file=sys.stderr)
        sys.exit(1)

    with open(args.doc_file, "r", encoding="utf-8") as f:
        content = f.read()

    blocks = extract_shell_blocks(content)

    if not blocks:
        print(f"OK: no shell code blocks found in {args.doc_file}.")
        sys.exit(0)

    errors: list[str] = []
    for i, (fence, block) in enumerate(blocks, 1):
        script = strip_prompts(block, prompt_only=fence == "console")
        if not script:
            continue
        ok, stderr = check_syntax(script)
        if ok:
            print(f"  PASS  block {i}")
        else:
            errors.append(f"block {i}: {stderr}")
            print(f"  FAIL  block {i}: {stderr}")

    if errors:
        print(
            f"\nFAILED: {len(errors)} block(s) have syntax errors in {args.doc_file}."
        )
        sys.exit(1)
    else:
        print(f"\nOK: all {len(blocks)} block(s) pass syntax check in {args.doc_file}.")


if __name__ == "__main__":
    main()
