#!/usr/bin/env python3
"""
generate_demo_gif.py
====================
Generates an animated GIF showing a MyGit terminal session.
No browser, no asciinema needed — pure Python with Pillow.

Usage:
    python3 generate_demo_gif.py                 # outputs demo.gif
    python3 generate_demo_gif.py --output demo   # same
    python3 generate_demo_gif.py --fast          # shorter delays

Requirements:
    pip install pillow
"""

import argparse
from PIL import Image, ImageDraw, ImageFont
import os, sys

# ── Config ────────────────────────────────────────────────────────────────────
W, H        = 860, 520        # frame size (px)
PAD_X       = 18
PAD_Y       = 54              # top padding (below titlebar)
LINE_H      = 18
FONT_SIZE   = 13
TITLEBAR_H  = 32

# Colours (dark GitHub theme)
BG          = (22,  27,  34)
TITLEBAR_BG = (33,  38,  45)
BORDER      = (48,  54,  61)

C = {
    "prompt":   (63,  185, 80),
    "cmd":      (230, 237, 243),
    "flag":     (121, 192, 255),
    "output":   (139, 148, 158),
    "hash":     (246, 224, 94),
    "branch":   (210, 168, 255),
    "success":  (63,  185, 80),
    "error":    (248, 81,  73),
    "info":     (88,  166, 255),
    "modified": (240, 136, 62),
    "comment":  (99,  110, 123),
}

# ── Terminal script ───────────────────────────────────────────────────────────
# Each entry: ("prompt", text, colour_key) | ("out", text, colour_key) | ("blank",)

SCRIPT = [
    ("comment", "# Initialise a new repository"),
    ("cmd",  "./mygit init"),
    ("out",  "Initialized empty MyGit repository in .git/", "success"),
    ("blank",),

    ("comment", "# Stage files and commit"),
    ("cmd",  "./mygit add ."),
    ("out",  "Changes staged.", "success"),
    ("cmd",  './mygit commit -m "initial commit"'),
    ("out",  "[e268b40] initial commit", "hash"),
    ("blank",),

    ("comment", "# Second commit after modifying a file"),
    ("cmd",  'echo "int main() {}" >> src/main.cpp'),
    ("cmd",  "./mygit add src/main.cpp"),
    ("out",  "Changes staged.", "success"),
    ("cmd",  './mygit commit -m "add main function"'),
    ("out",  "[6e0c307] add main function", "hash"),
    ("blank",),

    ("comment", "# View commit history"),
    ("cmd",  "./mygit log"),
    ("out",  "commit 6e0c307265852eab8c90391d63a5fc2f643648f1", "hash"),
    ("out",  "Parent: e268b4019a7dfda7362b1b771054ab0a442f91dd", "output"),
    ("out",  "Author: Vaibhav Gupta <vaibhav@example.com>",     "output"),
    ("out",  "    add main function",                            "output"),
    ("out",  "",                                                 "output"),
    ("out",  "commit e268b4019a7dfda7362b1b771054ab0a442f91dd", "hash"),
    ("out",  "    initial commit",                              "output"),
    ("blank",),

    ("comment", "# Create a branch and switch to it"),
    ("cmd",  "./mygit branch feature-x"),
    ("out",  "Created branch 'feature-x' at 6e0c307", "branch"),
    ("cmd",  "./mygit switch feature-x"),
    ("out",  "Switched to branch 'feature-x'", "success"),
    ("blank",),

    ("comment", "# Commit on the branch"),
    ("cmd",  "./mygit add feature.txt"),
    ("out",  "Changes staged.", "success"),
    ("cmd",  './mygit commit -m "add feature on feature-x"'),
    ("out",  "[f08b327] add feature on feature-x", "hash"),
    ("blank",),

    ("comment", "# Switch back to main — working dir restores"),
    ("cmd",  "./mygit switch main"),
    ("out",  "Switched to branch 'main'", "success"),
    ("blank",),

    ("comment", "# Restore any historical commit"),
    ("cmd",  "./mygit checkout e268b40"),
    ("out",  "HEAD is now at e268b40", "info"),
    ("blank",),

    ("comment", "# Inspect the object store"),
    ("cmd",  "./mygit cat-file -p d1690510"),
    ("out",  "# My Project", "output"),
    ("blank",),
]

# ── Font loading ─────────────────────────────────────────────────────────────

def load_font(size: int):
    """Try common monospace fonts, fall back to default."""
    candidates = [
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
        "/usr/share/fonts/truetype/noto/NotoMono-Regular.ttf",
        "/System/Library/Fonts/Supplemental/Courier New.ttf",   # macOS
        "C:/Windows/Fonts/cour.ttf",                            # Windows
    ]
    for path in candidates:
        if os.path.exists(path):
            return ImageFont.truetype(path, size)
    return ImageFont.load_default()

# ── Frame rendering ───────────────────────────────────────────────────────────

def render_frame(lines: list, cursor_visible: bool, font) -> Image.Image:
    img  = Image.new("RGB", (W, H), BG)
    draw = ImageDraw.Draw(img)

    # titlebar
    draw.rectangle([0, 0, W, TITLEBAR_H], fill=TITLEBAR_BG)
    draw.rectangle([0, TITLEBAR_H, W, TITLEBAR_H + 1], fill=BORDER)

    # traffic lights
    for x, col in [(14, (255, 95, 87)), (30, (254, 188, 46)), (46, (40, 200, 64))]:
        draw.ellipse([x - 5, TITLEBAR_H // 2 - 5, x + 5, TITLEBAR_H // 2 + 5], fill=col)

    # title
    title = "vaibhav@dev: ~/projects/myproject"
    tw = draw.textlength(title, font=font)
    draw.text(((W - tw) / 2, 8), title, fill=(139, 148, 158), font=font)

    # lines
    y = PAD_Y
    for i, (tokens, is_prompt_line, show_cursor) in enumerate(lines):
        x = PAD_X
        for text, colour in tokens:
            if not text:
                continue
            draw.text((x, y), text, fill=colour, font=font)
            x += int(draw.textlength(text, font=font))
        # cursor on last prompt line
        if show_cursor and cursor_visible:
            draw.rectangle([x, y + 1, x + 7, y + LINE_H - 2],
                           fill=C["prompt"])
        y += LINE_H
        if y > H - LINE_H:
            break

    return img


# ── Script player ─────────────────────────────────────────────────────────────

def colour_cmd(text: str):
    """Very simple tokeniser: colour -flag and "quoted" parts."""
    tokens = []
    i = 0
    while i < len(text):
        if text[i] == '-':
            # flag — read until space
            j = i
            while j < len(text) and text[j] != ' ':
                j += 1
            tokens.append((text[i:j], C["flag"]))
            i = j
        elif text[i] == '"':
            j = text.find('"', i + 1)
            if j == -1: j = len(text) - 1
            tokens.append((text[i:j+1], C["flag"]))
            i = j + 1
        else:
            # plain cmd text
            j = i
            while j < len(text) and text[j] not in ('-', '"'):
                j += 1
            tokens.append((text[i:j], C["cmd"]))
            i = j
    return tokens


def generate_frames(fast: bool) -> list[tuple[Image.Image, int]]:
    """
    Returns list of (PIL.Image, duration_ms).
    """
    font   = load_font(FONT_SIZE)
    frames = []

    # Each "line" stored as: (tokens, is_prompt, show_cursor)
    # tokens = list of (text, colour)
    rendered_lines: list = []

    # How many lines fit on screen
    visible = (H - PAD_Y) // LINE_H - 1

    CHAR_DELAY = 30 if fast else 60
    OUT_DELAY  = 40 if fast else 80
    PAUSE      = 350 if fast else 700

    def snap(dur, cursor=True):
        view = rendered_lines[-visible:]
        # set show_cursor only on last line
        view_tagged = []
        for j, entry in enumerate(view):
            tokens, is_p, _ = entry
            show = (j == len(view) - 1) and cursor
            view_tagged.append((tokens, is_p, show))
        frames.append((render_frame(view_tagged, True, font), dur))

    def blink_pause(ms):
        # render two frames (cursor on / off) to simulate blink
        steps = max(1, ms // 200)
        for s in range(steps):
            view = rendered_lines[-visible:]
            view_tagged = []
            for j, entry in enumerate(view):
                tokens, is_p, _ = entry
                show = (j == len(view) - 1)
                view_tagged.append((tokens, is_p, show))
            cursor_on = (s % 2 == 0)
            frames.append((render_frame(view_tagged, cursor_on, font), 200))

    for step in SCRIPT:
        if step[0] == "blank":
            rendered_lines.append(([("", C["output"])], False, False))
            snap(OUT_DELAY)
            continue

        if step[0] == "comment":
            text = step[1]
            rendered_lines.append(([(text, C["comment"])], False, False))
            snap(PAUSE // 2)
            continue

        if step[0] == "cmd":
            text = step[1]
            # start prompt line
            rendered_lines.append(([("$ ", C["prompt"])], True, True))
            snap(CHAR_DELAY)

            # type characters one by one
            for i in range(1, len(text) + 1):
                partial = text[:i]
                tokens  = [("$ ", C["prompt"])] + colour_cmd(partial)
                rendered_lines[-1] = (tokens, True, True)
                snap(CHAR_DELAY)

            # pause at end of command
            blink_pause(PAUSE)
            # remove cursor
            tokens = [("$ ", C["prompt"])] + colour_cmd(text)
            rendered_lines[-1] = (tokens, True, False)
            snap(OUT_DELAY)
            continue

        if step[0] == "out":
            _, text, cls = step
            rendered_lines.append(([(text, C[cls])], False, False))
            snap(OUT_DELAY)
            continue

    # Final blinking cursor
    rendered_lines.append(([("$ ", C["prompt"])], True, True))
    for _ in range(6):
        blink_pause(300)

    return frames


# ── Main ─────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="Generate MyGit demo GIF")
    parser.add_argument("--output", default="demo", help="Output filename (no .gif)")
    parser.add_argument("--fast",   action="store_true", help="Faster animation")
    args = parser.parse_args()

    print("Generating frames…")
    frames = generate_frames(fast=args.fast)
    print(f"  {len(frames)} frames generated")

    images   = [f[0] for f in frames]
    durations = [f[1] for f in frames]

    out_path = args.output + ".gif"
    print(f"Saving {out_path}…")
    images[0].save(
        out_path,
        save_all=True,
        append_images=images[1:],
        duration=durations,
        loop=0,
        optimize=True,
    )

    size_kb = os.path.getsize(out_path) / 1024
    print(f"✓ Saved: {out_path}  ({size_kb:.0f} KB,  {len(frames)} frames)")
    print(f"\nEmbed in README.md:")
    print(f'  ![MyGit Demo]({out_path})')


if __name__ == "__main__":
    main()
