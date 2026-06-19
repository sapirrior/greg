# greg - Makefile
# Version is resolved dynamically (in this priority order):
#   1. `git describe` if this is a git checkout with tags
#   2. The VERSION file in the repo root
#   3. Falls back to "0.0.0-unknown" if neither is available
# This means you NEVER hand-edit a version string in source: bump the
# VERSION file (or tag a release in git) and the binary picks it up.

CC      ?= gcc
SRC_DIR := src
INC_DIR := include
OBJ_DIR := obj

VERSION := $(shell git describe --tags --always --dirty 2>/dev/null || cat VERSION 2>/dev/null || echo 0.0.0-unknown)

# --- Warnings & correctness -------------------------------------------------
WARN_FLAGS := -Wall -Wextra -Wpedantic -Wshadow -Wformat=2 -Wstrict-prototypes -Wundef

# --- Optimization ------------------------------------------------------------
# Release (default): -O3 + LTO + native tuning (override NATIVE=0 for portable builds,
# e.g. when the build machine differs from the target machine).
NATIVE ?= 1
OPT_FLAGS := -O3 -flto -fomit-frame-pointer -funroll-loops
ifeq ($(NATIVE),1)
    OPT_FLAGS += -march=native
endif

# Debug build: `make DEBUG=1` — no optimization, sanitizers, debug symbols.
DEBUG ?= 0
ifeq ($(DEBUG),1)
    OPT_FLAGS  := -O0 -g3 -fsanitize=address,undefined -fno-omit-frame-pointer
    LDFLAGS_DBG := -fsanitize=address,undefined
endif

CFLAGS  ?= $(WARN_FLAGS) $(OPT_FLAGS) -I$(INC_DIR) -D_GNU_SOURCE \
           -DGREG_VERSION=\"$(VERSION)\" -MMD -MP
LDFLAGS ?= -lpcre2-8 -lpthread $(LDFLAGS_DBG)

ifeq ($(DEBUG),1)
    LDFLAGS += $(OPT_FLAGS)
endif

# --- OS detection (native Windows / MinGW) -----------------------------------
ifeq ($(OS),Windows_NT)
    TARGET  := greg.exe
    LDFLAGS := -lpcre2-8 $(LDFLAGS_DBG)
else
    TARGET  := greg
endif

PREFIX ?= /usr/local

SOURCES := $(wildcard $(SRC_DIR)/*.c)
OBJECTS := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SOURCES))
DEPS    := $(OBJECTS:.o=.d)

.PHONY: all clean install uninstall debug release version help

all: $(TARGET)

release: ## Optimized build (default)
	@$(MAKE) all

debug: ## Debug build with ASan/UBSan, no optimization
	@$(MAKE) DEBUG=1 all

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $(TARGET) $(LDFLAGS)
	@echo "Built $(TARGET) version $(VERSION)"

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

-include $(DEPS)

install: $(TARGET) ## Install to $(PREFIX)/bin (override with PREFIX=...)
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(TARGET)

version: ## Print the version that would be embedded in the binary
	@echo $(VERSION)

clean:
	rm -rf $(OBJ_DIR) greg greg.exe

help:
	@echo "Targets: all (default), release, debug, install, uninstall, clean, version"
	@echo "Useful overrides: NATIVE=0 (disable -march=native), PREFIX=/path, DEBUG=1"
