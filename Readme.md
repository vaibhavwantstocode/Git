# MyGit

A lightweight version control system implemented in C++17, following Git's actual
internal object model. Not a simulation — objects are stored as SHA-1-addressed,
zlib-compressed binary files in `.git/objects/`, exactly as real Git does it.

---

## Features

| Command | Description |
|---------|-------------|
| `init` | Initialise a `.git` directory |
| `add` | Stage files or directories |
| `commit` | Save staged snapshot with a message |
| `log` | Traverse commit history via parent pointer chain |
| `status` | Show staged files and unstaged modifications |
| `branch` | List, create, or delete branches |
| `switch` | Switch to a branch |
| `checkout` | Restore working directory from any branch or commit hash |
| `write-tree` | Write current directory as a tree object |
| `ls-tree` | Inspect a tree object |
| `cat-file` | Print type, size, or content of any stored object |
| `hash-object` | Compute (and optionally store) the SHA-1 of a file |
|[MyGit Demo](assets/demo.gif)
|[Object Graph](assets/git_objects.png)

---

## How It Works — The Object Model

Every piece of data in MyGit (files, directories, commits) is stored as an immutable
**object** in `.git/objects/`. An object's name IS its content hash (SHA-1), so
identical content is stored only once automatically.

### Three object types

```
blob   → compressed file content
         "blob <size>\0<content>"

tree   → sorted list of entries:
         "<mode> <name>\0<20-byte-binary-hash>" per entry
         mode 100644 = file, 40000 = directory

commit → text metadata pointing to a tree + optional parent:
         "commit <size>\0tree <hash>\nparent <hash>\nauthor ...\n\n<message>"
```

### Storage layout

```
.git/
├── HEAD                    → "ref: refs/heads/main"
├── index                   → staged files: "<hash> <path>" per line
├── refs/
│   └── heads/
│       ├── main            → SHA-1 of latest commit on main
│       └── feature-x       → SHA-1 of latest commit on feature-x
└── objects/
    ├── e2/
    │   └── 68b40...        → compressed commit object
    ├── a1/
    │   └── d41ee...        → compressed tree object
    └── 3b/
        └── 18e51...        → compressed blob object
```

The **index** (staging area) is read by `commit` to build the tree — only staged
files appear in the commit, matching real Git's behaviour.

---

## Requirements

- C++17 or higher
- OpenSSL (SHA-1 hashing)
- zlib (object compression)

```bash
# Ubuntu / Debian
sudo apt install libssl-dev zlib1g-dev

# macOS
brew install openssl
```

---

## Build

```bash
make
```

---

## Demo

```bash
$ ./mygit init
Initialized empty MyGit repository in .git/

$ mkdir src
$ echo '#include <iostream>' > src/main.cpp
$ echo '# My Project' > README.md

$ ./mygit add .
Changes staged.

$ ./mygit commit -m "initial commit"
[e268b40] initial commit

$ echo 'int main() {}' >> src/main.cpp
$ ./mygit add src/main.cpp
$ ./mygit commit -m "add main function"
[6e0c307] add main function

$ ./mygit log
commit 6e0c307265852eab8c90391d63a5fc2f643648f1
Parent: e268b4019a7dfda7362b1b771054ab0a442f91dd
Author: Vaibhav Gupta <vaibhav@example.com> 2026-03-22 15:18:47 +0000
Date:   Vaibhav Gupta <vaibhav@example.com> 2026-03-22 15:18:47 +0000

    add main function

commit e268b4019a7dfda7362b1b771054ab0a442f91dd
Author: Vaibhav Gupta <vaibhav@example.com> 2026-03-22 15:18:47 +0000

    initial commit

# Branching
$ ./mygit branch feature-x
Created branch 'feature-x' at 6e0c307

$ ./mygit branch
  feature-x
* main

$ ./mygit switch feature-x
Switched to branch 'feature-x'

$ echo 'feature work' > feature.txt
$ ./mygit add feature.txt
$ ./mygit commit -m "add feature on feature-x"
[f08b327] add feature on feature-x

$ ./mygit switch main
Switched to branch 'main'

# Checkout historical commit — restores working directory
$ ./mygit checkout e268b40...
HEAD is now at e268b40

# Inspect objects
$ ./mygit write-tree
a1d41ee59efd7b2bce3e84e8288cbc17f5b46ba2

$ ./mygit ls-tree --name-only a1d41ee59efd7b2bce3e84e8288cbc17f5b46ba2
README.md
src

$ ./mygit hash-object -w README.md
d1690510776ebc84930fae4f4e48a2d6037fefb8

$ ./mygit cat-file -t d1690510776ebc84930fae4f4e48a2d6037fefb8
blob

$ ./mygit cat-file -p d1690510776ebc84930fae4f4e48a2d6037fefb8
# My Project

# Status
$ echo 'new content' >> README.md
$ ./mygit status
Nothing staged for commit.

Unstaged changes:
  modified: README.md
  untracked: feature.txt
```

---

## Author Configuration

`commit` reads author name and email from:
1. `GIT_AUTHOR_NAME` / `GIT_AUTHOR_EMAIL` environment variables
2. `~/.gitconfig` (same format as real Git)
3. Falls back to `"Unknown <unknown@example.com>"`

---

## Known Limitations

These are deliberate scope decisions for this implementation, not bugs:

| Feature | Status | Notes |
|---------|--------|-------|
| `merge` | Not implemented | Would require 3-way diff |
| `diff` | Not implemented | Line-level diffing is a separate library problem |
| `push` / `pull` | Not implemented | Requires network protocol (packfiles, smart HTTP) |
| `rebase` | Not implemented | Depends on merge |
| `stash` | Not implemented | Future work |
| Checkout deletes untracked files | No | Real Git also leaves untracked files alone |
| Index format | Text (hash + path per line) | Real Git uses a binary format with stat data |
| SHA-1 collision resistance | Relies on OpenSSL SHA-1 | Real Git is migrating to SHA-256 |

---

## Project Structure

```
MyGit/
├── mygit.cpp     — all implementation (single translation unit)
├── Makefile      — build configuration
└── README.md     — this file
```

---

## What This Taught Me

Building a working VCS exposed exactly why Git makes the design decisions it does:

- **Content-addressable storage** means identical files across commits share one
  object on disk — deduplication for free
- **The index as a staging layer** is what separates "snapshot of what I staged"
  from "snapshot of my entire working directory" — a critical distinction when you
  have half-finished changes you're not ready to commit
- **Parent pointer chains** make `log` trivial and `branch` almost free —
  a branch is literally just a file containing one hash
- **zlib compression** on already-structured text gives 3-5x size reduction
  with almost no implementation cost

---

## License

Open source — feel free to use, modify, and distribute.
