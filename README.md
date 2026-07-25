# tangi ☕

A tiny, fast, cross-platform tool that keeps your computer awake and your screen
on for exactly as long as you want — with a live countdown you can check,
extend, and stop from any terminal. Works on **macOS** and **Linux**.

By default tangi behaves like a CLI Amphetamine: it keeps the **system awake**,
keeps the **display on** (no screensaver, no lock), and keeps you showing as
**active in chat apps** like Slack — no flags required. Add `-s` for a quiet
"just don't sleep in the background" mode.

- **Small:** ~40 KB stripped binary, zero runtime dependencies on macOS.
- **Efficient:** a single tiny daemon that sleeps in `select()` — no busy
  polling, near-zero CPU and memory.
- **Real OS integration:** tangi asks the operating system directly to stay
  awake — macOS IOKit power assertions and Linux systemd-logind inhibitor locks.

## Install

One line — downloads a prebuilt binary for your platform, no clone or compiler
needed:

```sh
curl -fsSL https://raw.githubusercontent.com/chud-lori/tangi/main/install.sh | sh
```

Prebuilt binaries are published for macOS (arm64 / x86_64) and Linux (x86_64 /
arm64) on every [release](https://github.com/chud-lori/tangi/releases). The same
script handles the rest:

```sh
# no-sudo install into ~/.local/bin:
curl -fsSL https://raw.githubusercontent.com/chud-lori/tangi/main/install.sh | sh -s -- --user

# pin a specific version, or uninstall:
curl -fsSL https://raw.githubusercontent.com/chud-lori/tangi/main/install.sh | sh -s -- --version v0.1.0
curl -fsSL https://raw.githubusercontent.com/chud-lori/tangi/main/install.sh | sh -s -- --uninstall
```

Or clone and run the installer, which **builds from source** when a checkout is
present and puts `tangi` on your `PATH`:

```sh
./install.sh            # build + install to /usr/local/bin (asks for sudo if needed)
./install.sh --user     # install to ~/.local/bin (no sudo)
./install.sh --prefix ~/opt   # install to ~/opt/bin
./install.sh --uninstall      # stop any session and remove tangi
```

If the chosen directory isn't on your `PATH` yet, the installer tells you the
exact line to add to your shell profile.

Or build/install manually with make:

```sh
make                 # builds ./tangi
sudo make install    # installs to /usr/local/bin (override with PREFIX=)
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
tangi lid <duration>  # stay awake even with the lid closed
tangi status          # show remaining time + end clock time (default, no args)
tangi stop            # release and allow sleep again
```

Options:
- `-l`, `--lid` — also stay awake when the laptop lid is closed.
- `-s`, `--system-only` — quiet mode: only stop the machine sleeping. The
  display may sleep and chat-app presence is left alone (good for background
  jobs).
- `-h`, `--help` — help.
- `-v`, `--version` — version + active backend.

**Durations** combine `d`/`h`/`m`/`s`: `45s`, `30m`, `2h`, `1h30m`, `1d12h`.
A bare number means seconds (`90` == `90s`).

### Examples

```sh
tangi 1h30m       # awake + screen on + online for 90 minutes
tangi on          # ...indefinitely, until you stop it
tangi             # ● tangi awake · 1h 28m remaining · ends 16:12 (in color)
tangi add 10m     # extend by 10 minutes
tangi lid 1h      # keep working for an hour with the lid closed
tangi -s 2h       # background: just don't sleep for 2 hours
tangi stop        # done
```

## Staying "online" (default)

In its default mode tangi keeps the display on and, every ~50 seconds, tells the
OS the user is active — so chat apps that track idle time (Slack, Teams) keep
showing you as active. This does **not** move the cursor or type anything: on
macOS it reuses Apple's `IOPMAssertionDeclareUserActivity` and posts a
zero-distance mouse event at the current pointer position to reset the system
idle timer. Use `-s` if you'd rather tangi leave presence and the display alone.

## Lid mode

By default a closed lid will still put the machine to sleep — the normal
keep-awake lock only blocks *idle* sleep, not the explicit "lid closed" signal.
`tangi lid <duration>` (or `-l`) keeps the session running with the lid shut.

- **macOS:** there is no power-assertion for this, so tangi globally disables
  lid-close sleep via `pmset disablesleep`. That needs admin — you'll be asked
  for your password once, when lid mode starts. A small root "lid guard" then
  watches the session and **re-enables sleep automatically** as soon as the
  timer ends, you run `tangi stop`, or you reset without `-l`. Note: running
  with the lid closed and no external airflow can let the machine run warm.
- **Linux:** no admin needed — tangi simply asks logind for a
  `handle-lid-switch` inhibitor alongside the idle/sleep one.

## How it works

`tangi <duration>` forks a small detached daemon that acquires an OS-level
keep-awake lock and listens on a per-user Unix socket
(`$XDG_RUNTIME_DIR/tangi-<uid>.sock`, or `/tmp`). The `status`, `add`, and
`stop` subcommands are thin clients that talk to it over that socket. When the
timer expires (or you run `tangi stop`), the daemon releases the lock and exits,
so the machine can sleep normally again. Nothing is left running.

| Platform | Backend | Default keeps awake | Extras |
|----------|---------|---------------------|--------|
| macOS    | IOKit `IOPMAssertion` + `DeclareUserActivity` | system, display, presence | `pmset disablesleep` for lid mode (`-l`) |
| Linux    | logind `Inhibit` lock | system, idle/presence | `handle-lid-switch` for lid mode (`-l`) |

(`-s` drops back to system-sleep only.)

## License

MIT — see [LICENSE](LICENSE).
