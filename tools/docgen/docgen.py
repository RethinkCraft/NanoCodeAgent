#!/usr/bin/env python3
"""
AI documentation generator for NanoCodeAgent.

Reads CODE_DIFF from environment, calls GitHub Models API,
parses the JSON response, and writes updated book/src/** files.

Environment variables:
  GITHUB_TOKEN   - GitHub token with models:read permission (required)
  DOCGEN_MODEL   - Model to use (default: openai/gpt-4.1-mini)
  CODE_DIFF      - The git diff to analyze (required)
"""
import json
import os
import sys
import re

try:
    import requests
except ImportError:
    print("ERROR: 'requests' package not installed. Run: pip install requests", file=sys.stderr)
    sys.exit(1)

GITHUB_MODELS_ENDPOINT = "https://models.inference.ai.azure.com"
DEFAULT_MODEL = "openai/gpt-4.1-mini"
ALLOWED_PREFIX = "book/src/"

SYSTEM_PROMPT = """You are a technical documentation assistant for the NanoCodeAgent project.
NanoCodeAgent is a teaching-focused C++ AI Code Agent that implements LLM tool calling, file system operations, and shell execution.

When given a git diff of code changes, you must analyze the changes and return a JSON object describing which book chapters (in book/src/) need updating and what the updated content should be.

IMPORTANT RULES:
1. Only output files within book/src/ (e.g., "book/src/03-architecture.md")
2. Return ONLY valid JSON, no markdown fences, no extra text
3. The JSON format must be exactly:
{
  "files": [
    {"path": "book/src/XX-chapter.md", "content": "...full markdown content..."},
    ...
  ],
  "summary": "Brief description of what changed and why these chapters were updated"
}
4. Only update chapters that are genuinely affected by the code changes
5. Preserve the existing chapter structure and headings
6. Append a changelog entry to book/src/99-changelog.md describing the changes"""

def validate_path(path: str) -> bool:
    """Ensure path is within book/src/ and has no traversal."""
    # Reject null bytes, newlines, or other control characters
    if re.search(r'[\x00-\x1f]', path):
        return False
    # Reject absolute paths (Unix or Windows style)
    if path.startswith("/") or path.startswith("\\") or re.match(r'^[A-Za-z]:', path):
        return False
    # Normalize the path and verify it still starts with the allowed prefix
    normalized = os.path.normpath(path).replace("\\", "/")
    if not normalized.startswith(ALLOWED_PREFIX):
        return False
    return True

def call_github_models(token: str, model: str, diff: str) -> dict:
    """Call GitHub Models API and return the parsed JSON response."""
    url = f"{GITHUB_MODELS_ENDPOINT}/chat/completions"
    headers = {
        "Authorization": f"Bearer {token}",
        "Content-Type": "application/json",
    }
    payload = {
        "model": model,
        "messages": [
            {"role": "system", "content": SYSTEM_PROMPT},
            {"role": "user", "content": f"Here is the git diff of recent code changes:\n\n```diff\n{diff}\n```\n\nPlease analyze these changes and return the documentation updates as JSON."},
        ],
        "temperature": 0.3,
        "max_tokens": 8192,
    }

    print(f"[docgen] Calling model: {model}", file=sys.stderr)
    try:
        resp = requests.post(url, headers=headers, json=payload, timeout=120)
    except requests.RequestException as e:
        print(f"[docgen] ERROR: HTTP request failed: {e}", file=sys.stderr)
        print("[docgen] To list available models: gh api GET /catalog/models", file=sys.stderr)
        sys.exit(1)

    if resp.status_code != 200:
        print(f"[docgen] ERROR: API returned HTTP {resp.status_code}", file=sys.stderr)
        print(f"[docgen] Response: {resp.text}", file=sys.stderr)
        print("[docgen] To list available models: gh api GET /catalog/models", file=sys.stderr)
        sys.exit(1)

    raw = resp.json()
    content = raw["choices"][0]["message"]["content"]
    return content

def parse_model_output(raw_content: str) -> dict:
    """Parse model output as JSON, failing with helpful error if invalid."""
    # Strip any accidental markdown code fences
    stripped = raw_content.strip()
    if stripped.startswith("```"):
        # Remove opening fence (```json or ```)
        stripped = re.sub(r'^```[a-z]*\n?', '', stripped)
        # Remove closing fence
        stripped = re.sub(r'\n?```$', '', stripped)

    try:
        return json.loads(stripped)
    except json.JSONDecodeError as e:
        print("[docgen] ERROR: Model returned invalid JSON.", file=sys.stderr)
        print(f"[docgen] JSON parse error: {e}", file=sys.stderr)
        print("[docgen] Raw model response:", file=sys.stderr)
        print(raw_content, file=sys.stderr)
        sys.exit(1)

def write_files(files: list) -> None:
    """Write the files returned by the model, with strict path validation."""
    repo_root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    written = []
    for entry in files:
        path = entry.get("path", "")
        content = entry.get("content", "")

        if not validate_path(path):
            print(f"[docgen] SKIP (unsafe path): {path}", file=sys.stderr)
            continue

        abs_path = os.path.join(repo_root, path)
        # Final safety check: resolved path must be under book/src/
        resolved = os.path.realpath(abs_path)
        book_src = os.path.realpath(os.path.join(repo_root, ALLOWED_PREFIX))
        if not resolved.startswith(book_src):
            print(f"[docgen] SKIP (path escapes book/src/): {path}", file=sys.stderr)
            continue

        os.makedirs(os.path.dirname(abs_path), exist_ok=True)
        with open(abs_path, "w", encoding="utf-8") as f:
            f.write(content)
        print(f"[docgen] Wrote: {path}", file=sys.stderr)
        written.append(path)

    if not written:
        print("[docgen] No files written.", file=sys.stderr)
    else:
        print(f"[docgen] Updated {len(written)} file(s): {', '.join(written)}", file=sys.stderr)

def main():
    token = os.environ.get("GITHUB_TOKEN", "")
    if not token:
        print("[docgen] ERROR: GITHUB_TOKEN environment variable is not set.", file=sys.stderr)
        sys.exit(1)

    model = os.environ.get("DOCGEN_MODEL", DEFAULT_MODEL)
    diff = os.environ.get("CODE_DIFF", "")

    if not diff.strip():
        print("[docgen] No diff content found, nothing to do.", file=sys.stderr)
        sys.exit(0)

    raw_content = call_github_models(token, model, diff)
    result = parse_model_output(raw_content)

    if "files" not in result:
        print("[docgen] ERROR: Model response missing 'files' key.", file=sys.stderr)
        print(f"[docgen] Got: {result}", file=sys.stderr)
        sys.exit(1)

    summary = result.get("summary", "(no summary)")
    print(f"[docgen] Summary: {summary}", file=sys.stderr)

    write_files(result["files"])

if __name__ == "__main__":
    main()
