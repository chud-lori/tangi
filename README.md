# tangi ☕

A tiny, fast, cross-platform tool that keeps your computer awake for exactly as
long as you want — with a live countdown you can check, extend, and stop from
any terminal. Works on **macOS** and **Linux**.

- **Small:** ~36 KB stripped binary, zero runtime dependencies on macOS.
- **Efficient:** a single tiny daemon that sleeps in `select()` — no polling,
  near-zero CPU and memory.
- **Real OS integration:** tangi asks the operating system directly to stay
  awake — macOS IOKit power assertions and Linux systemd-logind inhibitor
  locks. Reliable, with no input-faking tricks.

## Build

```sh
make            # builds ./tangi
sudo make install   # installs to /usr/local/bin (override with PREFIX=)
```

Requirements:
- **macOS:** clang + the system SDK (IOKit / CoreFoundation). Nothing else.
- **Linux:** a C compiler. For the clean fd-based inhibitor, install
  `libsystemd` dev headers (`pkg-config --exists libsystemd`). Without them,
  tangi falls back to spawning `systemd-inhibit` at runtime.

## Usage

```sh
tangi <duration>      # start (or reset) — stay awake for the given time
tangi on              # stay awake indefinitely, until stopped
tangi add <duration>  # add more time to the current session
tangi status          # show remaining time (also the default with no args)
tangi stop            # release and allow sleep again
```

Options:
- `-d`, `--display` — also keep the display awake (works on start *and* reset).
- `-h`, `--help` — help.
- `-v`, `--version` — version + active backend.

**Durations** combine `d`/`h`/`m`/`s`: `45s`, `30m`, `2h`, `1h30m`, `1d12h`.
A bare number means seconds (`90` == `90s`).

### Examples

```sh
tangi 1h30m       # stay awake for 90 minutes
tangi             # ☕ tangi awake — 1h28m12s remaining
tangi add 10m     # extend by 10 minutes
tangi -d 25m      # 25 minutes, screen stays on too
tangi stop        # done
```

## How it works

`tangi <duration>` forks a small detached daemon that acquires an OS-level
keep-awake lock and listens on a per-user Unix socket
(`$XDG_RUNTIME_DIR/tangi-<uid>.sock`, or `/tmp`). The `status`, `add`, and
`stop` subcommands are thin clients that talk to it over that socket. When the
timer expires (or you run `tangi stop`), the daemon releases the lock and exits,
so the machine can sleep normally again. Nothing is left running.

| Platform | Backend | What it prevents |
|----------|---------|------------------|
| macOS    | IOKit `IOPMAssertion` | system idle sleep (+ display sleep with `-d`) |
| Linux    | logind `Inhibit` lock | idle + sleep (+ lid switch with `-d`) |
