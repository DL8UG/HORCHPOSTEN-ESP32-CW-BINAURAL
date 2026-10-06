#!/usr/bin/env python3
"""Check the Markdown documentation of the repository.

- every local link points to an existing file in the repository, and its
  #anchor to an existing heading (anchors as GitHub makes them); inline
  links and images (also with a title or a <target>), reference
  definitions and HTML <a href> / <img src>; a leading / is the repository
  root, as on GitHub
- a "## Contents" list names every level 2 and 3 heading after it, in order

Run from anywhere: python3 tools/check_doc_links.py
Exit status 1 and one line per problem if something is wrong.
"""
import re
import sys
from pathlib import Path
from urllib.parse import unquote

ROOT = Path(__file__).resolve().parent.parent
# folders not checked (generated or third-party files)
SKIP = [ROOT / "firmware" / "build", ROOT / "firmware" / "managed_components"]

HEADING = re.compile(r"^(#{1,6})\s+(.*?)\s*#*\s*$")
# [text](target "title") and ![alt](<tar get>): the target is group 1 or 2
LINK = re.compile(r"!?\[[^\]]*\]\(\s*(?:<([^>\n]*)>|([^)\s]+))"
                  r"(?:\s+(?:\"[^\"]*\"|'[^']*'|\([^)]*\)))?\s*\)")
REF_DEF = re.compile(r"^ {0,3}\[[^\]]+\]:\s*(?:<([^>]*)>|(\S+))", re.M)
HTML = re.compile(r"<(?:a|img)\b[^>]*?\s(?:href|src)\s*=\s*[\"']([^\"']+)[\"']", re.I)
FENCE = re.compile(r"^ {0,3}(`{3,}|~{3,})(.*)$")


def slug(text):
    """Anchor of a heading as GitHub makes it: links reduced to their text,
    lower case, punctuation (also ` and *) removed, spaces to hyphens."""
    text = re.sub(r"!?\[([^\]]*)\]\([^)]*\)", r"\1", text).strip().lower()
    text = re.sub(r"[^\w\- ]", "", text)
    return text.replace(" ", "-")


def parse(path):
    """Headings (level, text, anchor, line) and links (target, line)."""
    headings, text, seen = [], [], {}
    fence = None    # the opening fence while inside a code block
    for no, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        m = FENCE.match(line)
        if fence is None and m:
            fence = m.group(1)
            line = ""
        elif fence is not None:
            # closed by the same character, at least as long, nothing after
            if (m and m.group(1)[0] == fence[0] and len(m.group(1)) >= len(fence)
                    and not m.group(2).strip()):
                fence = None
            line = ""
        m = HEADING.match(line)
        if m:
            base = slug(m.group(2))
            n = seen.get(base, 0)
            seen[base] = n + 1
            headings.append((len(m.group(1)), m.group(2), base if n == 0 else f"{base}-{n}", no))
        text.append(re.sub(r"`[^`]*`", "", line))
    # links over the whole text: a link text may run over two lines
    text = "\n".join(text)
    links = []
    for pattern in (LINK, REF_DEF, HTML):
        for m in pattern.finditer(text):
            target = next(g for g in m.groups() if g is not None)
            links.append((target, text.count("\n", 0, m.start()) + 1))
    links.sort(key=lambda link: link[1])
    return headings, links


def check_links(path, links, anchors_of, problems):
    for target, no in links:
        if re.match(r"[a-z]+:", target):
            continue  # http:, https:, mailto:
        file_part, _, anchor = target.partition("#")
        file_part = unquote(file_part)
        if not file_part:
            dest = path
        elif file_part.startswith("/"):
            dest = (ROOT / file_part.lstrip("/")).resolve()
        else:
            dest = (path.parent / file_part).resolve()
        where = f"{path.relative_to(ROOT)}:{no}"
        if dest != ROOT and ROOT not in dest.parents:
            problems.append(f"{where}: link leaves the repository: {target}")
        elif not dest.exists():
            problems.append(f"{where}: missing file {target}")
        elif anchor:
            if dest.suffix != ".md":
                problems.append(f"{where}: anchor on a file that is not Markdown: {target}")
            elif anchor not in anchors_of(dest):
                problems.append(f"{where}: no heading for #{anchor} in {dest.relative_to(ROOT)}")


def check_contents(path, headings, problems):
    lines = path.read_text(encoding="utf-8").splitlines()
    idx = next((i for i, h in enumerate(headings) if h[0] == 2 and h[1] == "Contents"), None)
    if idx is None:
        return
    start = headings[idx][3]
    end = headings[idx + 1][3] - 1 if idx + 1 < len(headings) else len(lines)
    listed = re.findall(r"\]\(#([^)]+)\)", "\n".join(lines[start:end]))
    wanted = [h[2] for h in headings[idx + 1:] if h[0] in (2, 3)]
    if listed != wanted:
        missing = [a for a in wanted if a not in listed]
        extra = [a for a in listed if a not in wanted]
        detail = []
        if missing:
            detail.append("not listed: " + ", ".join(missing))
        if extra:
            detail.append("not a level 2/3 heading after it: " + ", ".join(extra))
        if not detail:
            detail.append("order differs from the headings")
        problems.append(f"{path.relative_to(ROOT)}:{start}: Contents " + "; ".join(detail))


def main():
    files = sorted(p for p in ROOT.rglob("*.md")
                   if not any(s in p.parents for s in SKIP) and ".git" not in p.parts)
    cache = {}

    def parsed(p):
        if p not in cache:
            cache[p] = parse(p)
        return cache[p]

    def anchors_of(p):
        return {h[2] for h in parsed(p)[0]}

    problems = []
    for path in files:
        headings, links = parsed(path)
        check_links(path, links, anchors_of, problems)
        check_contents(path, headings, problems)
    for p in problems:
        print(p)
    print(f"{len(files)} Markdown files checked, {len(problems)} problems")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
