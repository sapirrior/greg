# ⚡ greg — Search Like Flash

> A modern, high-performance text search utility written in C.  
> Parallel walk-and-search architecture. PCRE2 JIT. Faster than ripgrep on warm cache.

---

## ⚡ Benchmark — greg vs ripgrep

Tested on warm disk cache, search pattern `ls` over ~1,141 files (home directory, ARM64 / Android/Termux).  
Both tools run **back-to-back** on identical data — no unfair warmup advantage.

```
Run 01 → rg=51ms   greg=52ms   🏆 rg  (+1ms)
Run 02 → rg=47ms   greg=38ms   ⚡ greg (+9ms faster)
Run 03 → rg=71ms   greg=50ms   ⚡ greg (+21ms faster)
Run 04 → rg=78ms   greg=49ms   ⚡ greg (+29ms faster)
Run 05 → rg=48ms   greg=50ms   🏆 rg  (+2ms)
Run 06 → rg=59ms   greg=46ms   ⚡ greg (+13ms faster)
Run 07 → rg=53ms   greg=65ms   🏆 rg  (+12ms)
Run 08 → rg=56ms   greg=52ms   ⚡ greg (+4ms faster)
Run 09 → rg=83ms   greg=43ms   ⚡ greg (+40ms faster)
Run 10 → rg=56ms   greg=56ms   🤝 tie
```

**Result: greg wins 6/10, ties 1/10, loses 3/10.**

> **Note:** Benchmarks that run one tool's full warmup before the other (like `benchmark.js`) will
> favour whichever runs first due to OS disk cache and CPU scheduler state. The results above run
> both tools interleaved — the fairest possible comparison.

---

## 🚀 Features

- **Parallel walk-and-search** — worker threads handle both directory traversal and file searching simultaneously (work-stealing design)
- **PCRE2 JIT** — full regex with JIT compilation, same engine as ripgrep
- **Smart-case** — case-insensitive when pattern is all lowercase (like ripgrep)
- **Slab-allocated queue** — zero per-file `malloc`/`free` overhead in the hot path
- **gitignore support** — respects `.gitignore` and `.ignore` files with a lock-free, reference-counted ignore tree
- **mmap file reading** — memory-mapped I/O with `MADV_SEQUENTIAL` for large files
- **Buffered output** — 1 MiB stdout buffer, single `fwrite` per file result under mutex

---

## 📦 Installation

```bash
git clone <repo>
cd greg
make
# Binary is ./greg
```

**Dependencies:** `libpcre2-8` (dev package), a C99 compiler, pthreads.

```bash
# Debian/Ubuntu/Termux
apt install libpcre2-dev

# macOS
brew install pcre2
```

---

## 🔧 Usage

```
greg [options] <pattern> [path]
```

### Options

| Flag | Long form | Description |
|------|-----------|-------------|
| `-i` | `--ignore-case` | Search case-insensitively |
| `-S` | `--smart-case` | Case-insensitive if pattern is all lowercase *(default)* |
| `-v` | `--invert-match` | Select non-matching lines |
| `-F` | `--fixed-strings` | Treat pattern as a literal string |
| `-n` | `--line-number` | Always show line numbers |
| `-N` | `--no-line-number` | Never show line numbers |
| `-l` | `--files-with-matches` | Print only filenames containing matches |
| `-c` | `--count` | Print match count per file |
| `-m NUM` | `--max-count NUM` | Stop after NUM matches per file |
| `-j NUM` | `--threads NUM` | Number of worker threads *(default: CPU cores)* |
| `-a` | `--text` | Search binary files |
| | `--raw` | Machine-readable output format |
| | `--no-ignore` | Ignore `.gitignore` / `.ignore` rules |
| | `--follow` | Follow symbolic links |
| | `--hidden` | Search hidden files and directories |
| | `--heading` | Group results by filename *(default on TTY)* |
| | `--no-heading` | Disable grouped output |
| | `--color always\|never` | Control ANSI color output |
| `-h` | `--help` | Print help |
| | `--version` | Print version |

### Examples

```bash
# Search for "TODO" in current directory
greg TODO

# Case-insensitive search
greg -i "error" /var/log

# Only filenames
greg -l "main" src/

# Fixed string (no regex)
greg -F "fn main()" .

# Limit to 5 matches per file
greg -m 5 "panic" .

# Search hidden files too
greg --hidden "secret" ~
```

---

## ⚙️ Architecture

```
main thread
  └─ pushes root path as WORK_DIR onto shared queue

worker threads (N = CPU cores)
  ├─ pop WORK_DIR  → opendir/readdir → push child files (WORK_FILE) + subdirs (WORK_DIR)
  └─ pop WORK_FILE → mmap → PCRE2 JIT match → buffered output
```

Key design decisions:
- **Slab allocator** for queue nodes — 256 nodes per slab, recycled via free-list; no per-item `malloc`
- **Reference-counted ignore tree** — parent ignore contexts shared across threads with atomic refcount, no copies
- **Task tracking** in queue — automatic termination when `active_tasks` reaches zero; no explicit "deactivate" signal needed for the normal path

---

## 📄 License

MIT
