# fmanager

Single-panel TUI file manager in C11 with ncurses. Walks directories, copies,
moves, deletes, searches recursively, and scans `/` once so it can later
show folder sizes from a cache.

## What it looks like

```

┌─ /home/user ────────────────────────────────────────┐
│ + Documents/                              4.2 MB / │
│   Downloads/                            128.0 MB / │
│   notes.txt                                24 KB   │
│   report.pdf                              1.2 MB   │
└─────────────────────────────────────────────────────┘
Files: 14203 | Cache: 3421 | Errors: 12 | No access: 5 | Sort: name | -rw-r--r-- notes.txt
h Help | / Search | r Rename | v View | e Edit | c Copy | m Move | n Newdir | f File | d Delete | s Scan | q Quit

```

## Dependencies

- Linux (or another POSIX-compatible OS), a C11 compiler, ncurses headers
  with wide-character support.
- Debian/Ubuntu: `sudo apt install build-essential libncurses-dev`
- Fedora: `sudo dnf install gcc make ncurses-devel`
- Arch: `sudo pacman -S base-devel ncurses`

## Building

```sh
gcc -Wall -Wextra -O2 -std=c11 src/*.c -o fmanager -lncurses
./fmanager
```

Or:

```
make clean && make
```

AppImage is built with `build-appimage.sh`. It needs `nix` (pulls
`libncursesw.so.6` from there), `patchelf`, `wget`, and `mksquashfs`:

```
./build-appimage.sh
# produces fmanager-x86_64.AppImage
```

## Keys

| Key ↕▾ | Action ↕▾ |
|---|---|
| −`↑` / `↓` | Move through the list |
| −`Home` / `End` | Jump to the top / bottom of the list |
| −`Enter` | Enter a directory or open a file for viewing |
| `→` | Enter a directory |
| `←`, `Backspace` | Go one level up |
| `Space` | Mark/unmark an entry, cursor moves down |
| `u` | Clear all marks |
| `h` | Help |
| `/` | Recursive search (substring or mask `*.c`, `?`) |
| `r` | Rename |
| `v` | View file (built-in viewer) |
| `e` | Edit file (built-in editor) |
| `c` | Copy marked (or current) into a given directory |
| `m` | Move marked (or current) into a given directory |
| `n` | Create a directory |
| `f` | Create an empty file |
| `d` | Delete marked (or current), with confirmation |
| `p` | chmod (octal input, e.g. `755`) |
| `s` | Start a background scan of `/` and fill the cache |
| `S` | Cycle sort mode: name → size → time → ext |
| `:` | Run a shell command in the panel's current directory |
| `q` | Quit |
⚙

In dialogs: `Tab` — path autocomplete, `Enter` — confirm, `Esc` — cancel.
On overwrite during copy/move: `y` — yes, `n` — skip, `a` — all, `c` — cancel.

## Viewer and editor

Hand-rolled, roughly in the spirit of nano or Notepad. Syntax highlighting for
C/C++/Rust/Go/JS/TS/Java/C# (`.c .h .cpp .cc .hpp .rs .go .js .ts .java .cs`),
Python (`.py`), and shell (`.sh .bash`).

- View: arrows, `PgUp`/`PgDn`, `Home`/`End` to navigate, `q` or `Esc` to exit.
- Edit: normal text input, `Enter` — new line, `Backspace` — delete a character,
`Esc` — save and exit. The cursor always sits at the end of the buffer.

## Configuration

The file `~/.config/fmanager/fmanager.conf` is created on first run. It
controls colors of folders, symlinks, executables, the status bar, the panel
header, key hints, and syntax highlighting. Available colors:
`black red green yellow blue magenta cyan white default`.

State (last directory and cursor position) and the size cache live in
`~/.cache/fmanager.state` and `~/.cache/fmanager.cache`.

## How it works

- Directory traversal uses `lstat`, not `stat` — symlinks aren't followed, so
no loops.
- File size comes from `st_blocks * 512` (actual disk usage), with a fallback
to `st_size` when blocks are zero.
- The background `/` scan runs in a separate thread (`pthread`), fills the
cache via `cache_put`, and lookups go through `bsearch` over the sorted array.
- `/proc`, `/sys`, `/dev`, `/run` are skipped during scanning and searching.
- Copying uses a 64 KB buffer with `read`/`write`. Moving first tries
`rename`; if the kernel returns `EXDEV`, it copies and deletes the source.
- Deleting the `/` root is blocked inside `fs_delete`.

## Limitations

- POSIX/Linux only.
- No archive support and no network support, no dual-panel switching
(there's a single panel).
- The `s` key always scans `/`, regardless of the current directory.
- The editor accepts ASCII keyboard input only: non-ASCII input isn't
supported, though UTF-8 rendering and deleting a whole codepoint do work.
- The overwrite dialog during copy/move does appear, but the destination check
uses `stat` (follows symlinks).

Installation:
tar -xjf fmanager-0.1.0-linux-x86_64.tbz
cd fmanager-0.1.0-linux-x86_64
./install.sh

*If the fmanager command is not found after installation, add the following to ~/.bashrc:*
export PATH="$HOME/.local/bin:$PATH"

or run script: 
```bash
echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.bashrc
source ~/.bashrc
```

Learning project, do whatever you want.
