#!/usr/bin/env python3
"""
MyGit Object Graph Visualizer
==============================
Reads your actual .git/objects directory and draws the full
commit → tree → blob DAG showing exactly what Git stores.

Usage:
    python3 visualize.py                  # reads .git in current dir
    python3 visualize.py --repo /path     # specify repo path
    python3 visualize.py --output graph   # output filename (no extension)
    python3 visualize.py --format svg     # svg | png | pdf

The key insight it reveals:
    Identical file content across commits = ONE blob (content-addressable dedup)
"""

import os
import sys
import zlib
import struct
import argparse
import hashlib
from pathlib import Path
from graphviz import Digraph

# ── Colour palette ────────────────────────────────────────────────────────────
COLOURS = {
    "commit":  {"fill": "#2D3748", "font": "#F6E05E", "border": "#F6E05E"},
    "tree":    {"fill": "#1A365D", "font": "#90CDF4", "border": "#4299E1"},
    "blob":    {"fill": "#1C4532", "font": "#9AE6B4", "border": "#38A169"},
    "ref":     {"fill": "#44337A", "font": "#D6BCFA", "border": "#9F7AEA"},
    "head":    {"fill": "#742A2A", "font": "#FEB2B2", "border": "#FC8181"},
}

# ── Object parsing ────────────────────────────────────────────────────────────

def read_object(repo: Path, sha: str) -> tuple[str, bytes]:
    """Decompress and parse a .git/objects file. Returns (type, raw_content)."""
    obj_path = repo / "objects" / sha[:2] / sha[2:]
    if not obj_path.exists():
        return "unknown", b""
    
    raw = zlib.decompress(obj_path.read_bytes())
    # header is "<type> <size>\0"
    null = raw.index(b"\x00")
    header = raw[:null].decode()
    obj_type = header.split()[0]
    content = raw[null + 1:]
    return obj_type, content


def parse_commit(content: bytes) -> dict:
    """Parse a commit object into a dict."""
    text = content.decode(errors="replace")
    fields = {"tree": "", "parents": [], "author": "", "message": ""}
    lines = text.split("\n")
    msg_start = False
    msg_lines = []
    for line in lines:
        if msg_start:
            msg_lines.append(line)
        elif line == "":
            msg_start = True
        elif line.startswith("tree "):
            fields["tree"] = line[5:].strip()
        elif line.startswith("parent "):
            fields["parents"].append(line[7:].strip())
        elif line.startswith("author "):
            # "author Name <email> timestamp tz"
            parts = line[7:].split(">")[0]
            fields["author"] = parts.split("<")[0].strip()
    fields["message"] = "\n".join(msg_lines).strip()
    return fields


def parse_tree(content: bytes) -> list[dict]:
    """Parse a tree object. Returns list of {mode, name, sha}."""
    entries = []
    i = 0
    while i < len(content):
        # mode ends at space
        space = content.index(b" ", i)
        mode = content[i:space].decode()
        i = space + 1
        # name ends at null
        null = content.index(b"\x00", i)
        name = content[i:null].decode(errors="replace")
        i = null + 1
        # 20-byte binary sha
        sha_bytes = content[i:i + 20]
        sha = sha_bytes.hex()
        i += 20
        entries.append({"mode": mode, "name": name, "sha": sha})
    return entries


def collect_objects(repo: Path) -> dict:
    """
    Walk only REACHABLE objects (from refs + HEAD).
    Orphan objects created by write-tree, stale checkouts etc. are excluded
    so the diagram only shows what actually matters.
    """
    objects = {}

    def load(sha: str):
        if sha in objects or not sha:
            return
        obj_type, content = read_object(repo, sha)
        if obj_type == "commit":
            parsed = {"type": "commit", **parse_commit(content)}
            objects[sha] = parsed
            # recurse into tree + parents
            load(parsed.get("tree", ""))
            for p in parsed.get("parents", []):
                load(p)
        elif obj_type == "tree":
            entries = parse_tree(content)
            objects[sha] = {"type": "tree", "entries": entries}
            for e in entries:
                load(e["sha"])
        elif obj_type == "blob":
            preview = content[:60].decode(errors="replace").replace("\n", "↵")
            objects[sha] = {"type": "blob", "preview": preview,
                            "size": len(content)}

    # seed from all refs
    refs = read_refs(repo)
    for name, sha in refs.items():
        if name not in ("HEAD→",) and len(sha) == 40:
            load(sha)

    return objects


def read_refs(repo: Path) -> dict:
    """Read all refs (branches, HEAD). Returns {name: sha}."""
    refs = {}
    
    # HEAD
    head_file = repo / "HEAD"
    if head_file.exists():
        head = head_file.read_text().strip()
        if head.startswith("ref: "):
            ref_path = repo / head[5:]
            if ref_path.exists():
                refs["HEAD"] = ref_path.read_text().strip()
                refs["HEAD→"] = head[5:].replace("refs/heads/", "")
        else:
            refs["HEAD"] = head

    # branches
    heads_dir = repo / "refs" / "heads"
    if heads_dir.exists():
        for branch_file in heads_dir.iterdir():
            sha = branch_file.read_text().strip()
            if sha:
                refs[branch_file.name] = sha

    return refs


# ── Graph building ─────────────────────────────────────────────────────────────

def short(sha: str, n: int = 7) -> str:
    return sha[:n]


def make_label(sha: str, obj: dict, refs: dict) -> str:
    """Build the HTML-like label shown inside each node."""
    s = short(sha)
    t = obj["type"]

    # Which refs point here?
    pointing = [name for name, target in refs.items()
                if target == sha and not name.endswith("→")]
    ref_line = ""
    if pointing:
        badges = " ".join(f"[{r}]" for r in pointing)
        ref_line = f'\n<FONT POINT-SIZE="9" COLOR="{COLOURS["ref"]["font"]}">{badges}</FONT>'

    if t == "commit":
        msg = obj.get("message", "")[:40]
        author = obj.get("author", "")[:20]
        return (f'<<TABLE BORDER="0" CELLBORDER="0" CELLSPACING="2">'
                f'<TR><TD><FONT POINT-SIZE="9" COLOR="#A0AEC0">commit</FONT></TD></TR>'
                f'<TR><TD><FONT POINT-SIZE="11" COLOR="{COLOURS["commit"]["font"]}">'
                f'<B>{s}</B></FONT></TD></TR>'
                f'<TR><TD><FONT POINT-SIZE="9" COLOR="#E2E8F0">{msg}</FONT></TD></TR>'
                f'<TR><TD><FONT POINT-SIZE="8" COLOR="#718096">{author}</FONT></TD></TR>'
                f'<TR><TD>{ref_line}</TD></TR>'
                f'</TABLE>>')

    elif t == "tree":
        n_entries = len(obj.get("entries", []))
        return (f'<<TABLE BORDER="0" CELLBORDER="0" CELLSPACING="2">'
                f'<TR><TD><FONT POINT-SIZE="9" COLOR="#90CDF4">tree</FONT></TD></TR>'
                f'<TR><TD><FONT POINT-SIZE="11" COLOR="{COLOURS["tree"]["font"]}">'
                f'<B>{s}</B></FONT></TD></TR>'
                f'<TR><TD><FONT POINT-SIZE="8" COLOR="#718096">'
                f'{n_entries} entries</FONT></TD></TR>'
                f'</TABLE>>')

    elif t == "blob":
        preview = obj.get("preview", "")[:35].replace("&", "&amp;").replace("<", "&lt;")
        size = obj.get("size", 0)
        return (f'<<TABLE BORDER="0" CELLBORDER="0" CELLSPACING="2">'
                f'<TR><TD><FONT POINT-SIZE="9" COLOR="#9AE6B4">blob</FONT></TD></TR>'
                f'<TR><TD><FONT POINT-SIZE="11" COLOR="{COLOURS["blob"]["font"]}">'
                f'<B>{s}</B></FONT></TD></TR>'
                f'<TR><TD><FONT POINT-SIZE="8" COLOR="#718096">'
                f'"{preview}…"</FONT></TD></TR>'
                f'<TR><TD><FONT POINT-SIZE="8" COLOR="#718096">'
                f'{size} bytes</FONT></TD></TR>'
                f'</TABLE>>')

    return s


def build_graph(objects: dict, refs: dict,
                output: str = "git_objects",
                fmt: str = "png") -> str:
    """Build and render the Graphviz diagram."""

    dot = Digraph(
        name="MyGit Object Graph",
        format=fmt,
        graph_attr={
            "bgcolor":    "#1A202C",
            "rankdir":    "LR",
            "splines":    "ortho",
            "nodesep":    "0.6",
            "ranksep":    "1.2",
            "fontname":   "Courier New",
            "label":      "MyGit — Object Store Visualisation",
            "labelloc":   "t",
            "fontsize":   "14",
            "fontcolor":  "#E2E8F0",
            "pad":        "0.5",
        },
        node_attr={
            "shape":     "plaintext",
            "fontname":  "Courier New",
        },
        edge_attr={
            "fontname":  "Courier New",
            "fontsize":  "9",
            "fontcolor": "#718096",
        },
    )

    # ── Add nodes ──────────────────────────────────────────────────────────────
    for sha, obj in objects.items():
        t = obj["type"]
        if t not in COLOURS:
            continue
        c = COLOURS[t]
        label = make_label(sha, obj, refs)
        dot.node(
            sha,
            label=label,
            _attributes={
                "style":     "filled,rounded",
                "fillcolor": c["fill"],
                "color":     c["border"],
                "penwidth":  "2",
            },
        )

    # ── Add edges ──────────────────────────────────────────────────────────────
    
    # Track blobs referenced by multiple trees (dedup demonstration)
    blob_ref_count: dict[str, int] = {}

    for sha, obj in objects.items():
        t = obj["type"]

        if t == "commit":
            tree_sha = obj.get("tree", "")
            if tree_sha and tree_sha in objects:
                dot.edge(sha, tree_sha,
                         label="tree",
                         _attributes={"color": "#4299E1", "penwidth": "2"})
            for parent in obj.get("parents", []):
                if parent in objects:
                    dot.edge(sha, parent,
                             label="parent",
                             _attributes={"color": "#F6E05E",
                                          "style": "dashed",
                                          "penwidth": "1.5"})

        elif t == "tree":
            for entry in obj.get("entries", []):
                child_sha = entry["sha"]
                if child_sha not in objects:
                    continue
                child_type = objects[child_sha]["type"]
                name = entry["name"]
                colour = "#38A169" if child_type == "blob" else "#4299E1"
                dot.edge(sha, child_sha,
                         label=name,
                         _attributes={"color": colour, "penwidth": "1.5"})
                if child_type == "blob":
                    blob_ref_count[child_sha] = blob_ref_count.get(child_sha, 0) + 1

    # ── Highlight shared blobs ─────────────────────────────────────────────────
    for blob_sha, count in blob_ref_count.items():
        if count > 1 and blob_sha in objects:
            # Re-render with a special border to show dedup
            c = COLOURS["blob"]
            label = make_label(blob_sha, objects[blob_sha], refs)
            dot.node(
                blob_sha,
                label=label,
                _attributes={
                    "style":     "filled,rounded",
                    "fillcolor": c["fill"],
                    "color":     "#F6AD55",   # orange = shared
                    "penwidth":  "3",
                },
            )

    # ── Ref nodes (branches) ───────────────────────────────────────────────────
    current_branch = refs.get("HEAD→", "")
    for ref_name, sha in refs.items():
        if ref_name in ("HEAD", "HEAD→") or sha not in objects:
            continue
        is_head = (ref_name == current_branch)
        c = COLOURS["head"] if is_head else COLOURS["ref"]
        prefix = "HEAD → " if is_head else ""
        node_id = f"ref_{ref_name}"
        dot.node(
            node_id,
            label=f'<<B><FONT COLOR="{c["font"]}">{prefix}{ref_name}</FONT></B>>',
            _attributes={
                "shape":     "box",
                "style":     "filled,rounded",
                "fillcolor": c["fill"],
                "color":     c["border"],
                "penwidth":  "2",
                "fontname":  "Courier New",
            },
        )
        dot.edge(node_id, sha,
                 _attributes={"color": c["border"],
                              "penwidth": "2",
                              "style": "dashed"})

    # ── Legend ─────────────────────────────────────────────────────────────────
    with dot.subgraph(name="cluster_legend") as leg:
        leg.attr(
            label="Legend",
            style="filled",
            fillcolor="#2D3748",
            color="#4A5568",
            fontcolor="#E2E8F0",
            fontname="Courier New",
            fontsize="11",
        )
        leg.node("leg_commit", "commit",
                 _attributes={"style": "filled,rounded",
                               "fillcolor": COLOURS["commit"]["fill"],
                               "color": COLOURS["commit"]["border"],
                               "fontcolor": COLOURS["commit"]["font"],
                               "fontname": "Courier New", "fontsize": "10"})
        leg.node("leg_tree", "tree",
                 _attributes={"style": "filled,rounded",
                               "fillcolor": COLOURS["tree"]["fill"],
                               "color": COLOURS["tree"]["border"],
                               "fontcolor": COLOURS["tree"]["font"],
                               "fontname": "Courier New", "fontsize": "10"})
        leg.node("leg_blob", "blob",
                 _attributes={"style": "filled,rounded",
                               "fillcolor": COLOURS["blob"]["fill"],
                               "color": COLOURS["blob"]["border"],
                               "fontcolor": COLOURS["blob"]["font"],
                               "fontname": "Courier New", "fontsize": "10"})
        leg.node("leg_shared", "shared blob\n(dedup)",
                 _attributes={"style": "filled,rounded",
                               "fillcolor": COLOURS["blob"]["fill"],
                               "color": "#F6AD55",
                               "fontcolor": COLOURS["blob"]["font"],
                               "fontname": "Courier New", "fontsize": "10",
                               "penwidth": "3"})
        leg.node("leg_ref", "branch ref",
                 _attributes={"shape": "box", "style": "filled,rounded",
                               "fillcolor": COLOURS["ref"]["fill"],
                               "color": COLOURS["ref"]["border"],
                               "fontcolor": COLOURS["ref"]["font"],
                               "fontname": "Courier New", "fontsize": "10"})
        leg.node("leg_head", "HEAD branch",
                 _attributes={"shape": "box", "style": "filled,rounded",
                               "fillcolor": COLOURS["head"]["fill"],
                               "color": COLOURS["head"]["border"],
                               "fontcolor": COLOURS["head"]["font"],
                               "fontname": "Courier New", "fontsize": "10"})
        # invisible edges to keep legend compact
        for a, b in [("leg_commit", "leg_tree"), ("leg_tree", "leg_blob"),
                     ("leg_blob", "leg_shared"), ("leg_shared", "leg_ref"),
                     ("leg_ref", "leg_head")]:
            leg.edge(a, b, _attributes={"style": "invis"})

    rendered = dot.render(output, cleanup=True)
    return rendered


# ── Stats summary ─────────────────────────────────────────────────────────────

def print_summary(objects: dict, refs: dict):
    commits = [s for s, o in objects.items() if o["type"] == "commit"]
    trees   = [s for s, o in objects.items() if o["type"] == "tree"]
    blobs   = [s for s, o in objects.items() if o["type"] == "blob"]

    # find shared blobs (referenced by > 1 tree)
    blob_refs: dict[str, int] = {}
    for obj in objects.values():
        if obj["type"] == "tree":
            for e in obj.get("entries", []):
                if e["sha"] in objects and objects[e["sha"]]["type"] == "blob":
                    blob_refs[e["sha"]] = blob_refs.get(e["sha"], 0) + 1
    shared = sum(1 for c in blob_refs.values() if c > 1)

    print("\n── MyGit Object Store Summary ──────────────────────────────")
    print(f"  Commits : {len(commits)}")
    print(f"  Trees   : {len(trees)}")
    print(f"  Blobs   : {len(blobs)}")
    if shared:
        print(f"  Shared blobs (dedup): {shared}  ← same content, one object")
    print(f"\n  Branches: {[n for n in refs if n not in ('HEAD', 'HEAD→')]}")
    current = refs.get('HEAD→', '')
    if current:
        print(f"  Current : {current}")
    print("────────────────────────────────────────────────────────────\n")


# ── CLI ───────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description="Visualize a MyGit (or real Git) object graph",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument("--repo",   default=".",
                        help="Path to repo root (default: current directory)")
    parser.add_argument("--output", default="git_objects",
                        help="Output filename without extension (default: git_objects)")
    parser.add_argument("--format", default="png",
                        choices=["png", "svg", "pdf"],
                        help="Output format (default: png)")
    args = parser.parse_args()

    repo = Path(args.repo) / ".git"
    if not repo.exists():
        print(f"Error: no .git directory found at {args.repo}")
        sys.exit(1)

    print(f"Reading objects from {repo} …")
    objects = collect_objects(repo)
    refs    = read_refs(repo)

    if not objects:
        print("No objects found. Run some commits first.")
        sys.exit(1)

    print_summary(objects, refs)

    print(f"Rendering graph …")
    output_path = build_graph(objects, refs,
                               output=args.output,
                               fmt=args.format)
    print(f"✓ Saved: {output_path}")
    print(f"\nTip: shared blobs (orange border) = identical file content")
    print(f"     stored once across all commits — content-addressable dedup\n")


if __name__ == "__main__":
    main()
