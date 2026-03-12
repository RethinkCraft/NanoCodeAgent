#!/usr/bin/env python3
"""Shared helpers for docgen path detection and resolution."""

import ntpath
import os
import re


ROOT_FILE_NAMES = {"CMakeLists.txt", "Dockerfile", "LICENSE", "Makefile"}
ROOT_FILE_SUFFIXES = {
    ".cfg",
    ".cmake",
    ".conf",
    ".ini",
    ".json",
    ".md",
    ".rst",
    ".sh",
    ".toml",
    ".txt",
    ".xml",
    ".yaml",
    ".yml",
}
ROOT_DOTFILES = {
    ".clang-format",
    ".clang-tidy",
    ".editorconfig",
    ".env",
    ".env.example",
    ".gitattributes",
    ".gitignore",
}

REPO_TOP_LEVEL_DIRS = {
    ".agents",
    ".github",
    "3rd-party",
    "book",
    "docs",
    "include",
    "prompts",
    "scripts",
    "src",
    "tests",
}

PATH_SEGMENT_RE = re.compile(r"[A-Za-z0-9_.-](?:[A-Za-z0-9_. -]*[A-Za-z0-9_.-])?")
URI_SCHEME_RE = re.compile(r"^[A-Za-z][A-Za-z0-9+.-]*:")
NO_SLASH_EXTERNAL_SCHEMES = {"data", "mailto", "news", "sms", "tel", "urn", "xmpp"}


def is_root_file_candidate(candidate: str) -> bool:
    """Return whether a bare token looks like a root-level repo file."""
    if candidate in ROOT_FILE_NAMES or candidate in ROOT_DOTFILES:
        return True

    stem, suffix = os.path.splitext(candidate)
    return bool(stem) and suffix in ROOT_FILE_SUFFIXES


def is_repo_reference(candidate: str) -> bool:
    """Return whether a backtick token should be checked against the repo."""
    if is_external_uri(candidate) or candidate.startswith("-"):
        return False

    if is_absolute_reference(candidate):
        return True

    if " " in candidate:
        return is_spaced_repo_reference(candidate)

    return "/" in candidate or is_root_file_candidate(candidate)


def is_spaced_repo_reference(candidate: str) -> bool:
    """Return whether a spaced token still looks like a repo-relative path."""
    if "/" not in candidate:
        return False

    parts = candidate.split("/")
    if any(not part for part in parts):
        return False

    if parts[0] not in REPO_TOP_LEVEL_DIRS:
        return False

    return all(PATH_SEGMENT_RE.fullmatch(part) for part in parts)


def is_absolute_reference(candidate: str) -> bool:
    """Return whether a candidate is an absolute path on POSIX or Windows."""
    return (
        candidate.startswith("file://")
        or os.path.isabs(candidate)
        or ntpath.isabs(candidate)
    )


def is_external_uri(candidate: str) -> bool:
    """Return whether a candidate is a non-file URI that should be skipped."""
    if re.match(r"^[A-Za-z]:[\\/]", candidate):
        return False

    match = URI_SCHEME_RE.match(candidate)
    if not match:
        return False

    scheme = match.group(0)[:-1].lower()
    if scheme == "file":
        return False

    return "://" in candidate or scheme in NO_SLASH_EXTERNAL_SCHEMES


def extract_backtick_repo_references(content: str) -> list[str]:
    """Extract unique repo-like references from backtick-enclosed content."""
    references: set[str] = set()

    for match in re.finditer(r"`([^`]+)`", content):
        candidate = match.group(1).strip()
        if is_repo_reference(candidate):
            references.add(candidate)

    return sorted(references)


def extract_markdown_link_targets(content: str) -> list[str]:
    """Extract local Markdown link targets without anchors."""
    targets: set[str] = set()

    for match in re.finditer(r"\]\(([^)]+)\)", content):
        target = match.group(1).strip()
        if not target.startswith("#") and not is_external_uri(target):
            target = target.split("#", 1)[0]
            if target:
                targets.add(target)

    return sorted(targets)


def resolve_doc_relative_target(
    repo_root: str, doc_file: str, target: str
) -> tuple[str, str, bool]:
    """Resolve a document-relative target and report whether it stays in-repo."""
    repo_root = os.path.abspath(repo_root)
    doc_dir = os.path.dirname(os.path.abspath(doc_file))
    full_path = os.path.abspath(os.path.join(doc_dir, target))

    try:
        in_repo = os.path.commonpath((repo_root, full_path)) == repo_root
    except ValueError:
        in_repo = False

    reference = os.path.relpath(full_path, repo_root) if in_repo else target
    return reference, full_path, in_repo
