# ─────────────────────────────────────────────────────────
# MyGit — Makefile
# ─────────────────────────────────────────────────────────

CXX      = g++
CXXFLAGS = -Wall -std=c++23

# OpenSSL — adjust paths for your OS:
#   macOS (Homebrew):  /opt/homebrew/opt/openssl@3
#   Ubuntu (apt):      /usr  (after: sudo apt install libssl-dev)
#   Ubuntu (node):     /usr/include/node  (no libssl-dev needed)
OPENSSL_INCLUDE = /usr/include/node

# Linker — use whichever is present on your system
#   Ubuntu apt:   -lssl -lcrypto
#   Ubuntu node:  explicit .so paths (see LIBS_DIRECT below)
#   macOS brew:   -L$(OPENSSL_ROOT)/lib -lssl -lcrypto

LIBS_APT    = -lssl -lcrypto -lz
LIBS_DIRECT = /usr/lib/x86_64-linux-gnu/libssl.so.3 \
              /usr/lib/x86_64-linux-gnu/libcrypto.so.3 \
              -lz

TARGET  = mygit
SOURCES = mygit.cpp

# ── default build (tries apt-style first, falls back to direct) ──
all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CXX) $(CXXFLAGS) -I$(OPENSSL_INCLUDE) $^ -o $@ $(LIBS_APT) || \
	$(CXX) $(CXXFLAGS) -I$(OPENSSL_INCLUDE) $^ -o $@ $(LIBS_DIRECT)

# ── convenience: run the full demo ───────────────────────────────
demo: $(TARGET)
	@bash demo.sh

# ── clean ────────────────────────────────────────────────────────
clean:
	rm -f $(TARGET)

# ── help ─────────────────────────────────────────────────────────
help:
	@echo "Commands:"
	@echo "  make          Build mygit"
	@echo "  make demo     Run demo workflow"
	@echo "  make clean    Remove binary"
	@echo ""
	@echo "Usage:"
	@echo "  ./mygit init"
	@echo "  ./mygit add <file|dir|.>"
	@echo "  ./mygit commit -m \"message\""
	@echo "  ./mygit log"
	@echo "  ./mygit status"
	@echo "  ./mygit branch [name] [-d name]"
	@echo "  ./mygit switch <branch>"
	@echo "  ./mygit checkout <branch|commit-hash>"
	@echo "  ./mygit write-tree"
	@echo "  ./mygit ls-tree [--name-only] <hash>"
	@echo "  ./mygit cat-file [-p|-t|-s] <hash>"
	@echo "  ./mygit hash-object [-w] <file>"
