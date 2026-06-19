# greg

> Parallel walk-and-search architecture. PCRE2 JIT. High-performance design.

---

## Performance Comparison

Below is a benchmark comparison demonstrating the execution time of `greg` alongside other popular code search tools. The benchmark measures search execution time for the pattern `"ls"` across a home directory structure (~1,141 files) under Linux / ARM64.

All stdout output was redirected to `/dev/null` to measure engine processing latency rather than terminal rendering speeds.

| Tool | Average Execution Time (ms) | Relative Performance |
| :--- | :---: | :--- |
| **ugrep** | 53 ms | 1.00x (Baseline) |
| **greg** | 54 ms | 1.02x |
| **ripgrep (`rg`)** | 63 ms | 1.19x |
| **The Silver Searcher (`ag`)** | 662 ms | 12.49x |

Average execution times show that `greg` achieves processing speeds comparable to standard high-performance search utilities.

---

## Features

- **Parallel walk-and-search** — worker threads handle both directory traversal and file searching simultaneously (work-stealing design)
- **PCRE2 JIT** — full regex with JIT compilation, same engine as ripgrep
- **Smart-case** — case-insensitive when pattern is all lowercase (like ripgrep)
- **Slab-allocated queue** — zero per-file `malloc`/`free` overhead in the hot path
- **gitignore support** — respects `.gitignore` and `.ignore` files with a lock-free, reference-counted ignore tree
- **mmap file reading** — memory-mapped I/O with `MADV_SEQUENTIAL` for large files
- **Buffered output** — 1 MiB stdout buffer, single `fwrite` per file result under mutex

---

## Installation

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

## Usage

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

## Architecture

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

## License

MIT
