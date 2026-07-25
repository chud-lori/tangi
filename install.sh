#!/bin/sh
# tangi installer — puts `tangi` on your PATH.
#
# Two ways to run it:
#
#   Remote (no clone) — downloads a prebuilt binary from the latest release:
#     curl -fsSL https://raw.githubusercontent.com/chud-lori/tangi/main/install.sh | sh
#
#   From a clone — builds from source:
#     ./install.sh
#
# Options:
#   --user             install to ~/.local/bin (no sudo)
#   --prefix DIR       install to DIR/bin
#   --download         force download of a prebuilt binary (skip building)
#   --from-source      force a build from source (requires a clone + compiler)
#   --version VER      download this release tag instead of latest (e.g. v0.1.0)
#   --uninstall        remove tangi (and stop any running session)
#   --help
#
# Honors $CC for the compiler when building from source.

set -eu

PROG=tangi
REPO=chud-lori/tangi
SELF_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" 2>/dev/null && pwd || echo "")

PREFIX=""
USER_INSTALL=0
UNINSTALL=0
MODE=""          # "", "download", or "source"
REL_VERSION=""   # empty = latest

# ---- parse args ----
while [ $# -gt 0 ]; do
	case "$1" in
		--user)        USER_INSTALL=1 ;;
		--uninstall)   UNINSTALL=1 ;;
		--download)    MODE=download ;;
		--from-source) MODE=source ;;
		--version)     shift; REL_VERSION="${1:-}"; [ -n "$REL_VERSION" ] || { echo "tangi: --version needs a tag" >&2; exit 2; } ;;
		--version=*)   REL_VERSION="${1#--version=}" ;;
		--prefix)      shift; PREFIX="${1:-}";  [ -n "$PREFIX" ] || { echo "tangi: --prefix needs a directory" >&2; exit 2; } ;;
		--prefix=*)    PREFIX="${1#--prefix=}" ;;
		-h|--help)
			sed -n '2,21p' "$0" 2>/dev/null | sed 's/^# \{0,1\}//'
			exit 0 ;;
		*) echo "tangi: unknown option '$1' (try --help)" >&2; exit 2 ;;
	esac
	shift
done

# ---- choose install directory ----
if [ -n "$PREFIX" ]; then
	BINDIR="$PREFIX/bin"
elif [ "$USER_INSTALL" -eq 1 ]; then
	BINDIR="$HOME/.local/bin"
else
	BINDIR="/usr/local/bin"
fi

# ---- decide whether we need sudo for this BINDIR ----
SUDO=""
need_sudo() {
	if [ -d "$BINDIR" ] && [ -w "$BINDIR" ]; then return 1; fi
	parent=$(dirname -- "$BINDIR")
	if [ -w "$parent" ]; then return 1; fi
	return 0
}
if need_sudo; then
	if command -v sudo >/dev/null 2>&1; then
		SUDO="sudo"
	else
		echo "tangi: $BINDIR is not writable and sudo is unavailable." >&2
		echo "       Try: --user" >&2
		exit 1
	fi
fi

# ---- uninstall ----
if [ "$UNINSTALL" -eq 1 ]; then
	if command -v "$PROG" >/dev/null 2>&1; then
		"$PROG" stop >/dev/null 2>&1 || true
	elif [ -x "$BINDIR/$PROG" ]; then
		"$BINDIR/$PROG" stop >/dev/null 2>&1 || true
	fi
	if [ -e "$BINDIR/$PROG" ]; then
		echo "Removing $BINDIR/$PROG"
		$SUDO rm -f "$BINDIR/$PROG"
		echo "Done."
	else
		echo "tangi: not found at $BINDIR/$PROG (nothing to remove)"
	fi
	exit 0
fi

# ---- pick a mode: build from a clone, otherwise download ----
if [ -z "$MODE" ]; then
	if [ -n "$SELF_DIR" ] && [ -f "$SELF_DIR/Makefile" ] && [ -f "$SELF_DIR/src/main.c" ]; then
		MODE=source
	else
		MODE=download
	fi
fi

# ---- fetch a URL to a file (curl or wget) ----
fetch() {
	# fetch <url> <dest>
	if command -v curl >/dev/null 2>&1; then
		curl -fsSL "$1" -o "$2"
	elif command -v wget >/dev/null 2>&1; then
		wget -qO "$2" "$1"
	else
		echo "tangi: need curl or wget to download." >&2
		exit 1
	fi
}

if [ "$MODE" = download ]; then
	# ---- detect platform ----
	os=$(uname -s)
	arch=$(uname -m)
	case "$os" in
		Darwin) os=macos ;;
		Linux)  os=linux ;;
		*) echo "tangi: no prebuilt binary for '$os' — build from source (clone + ./install.sh)." >&2; exit 1 ;;
	esac
	case "$arch" in
		arm64|aarch64)  arch=arm64 ;;
		x86_64|amd64)   arch=x86_64 ;;
		*) echo "tangi: no prebuilt binary for arch '$arch' — build from source." >&2; exit 1 ;;
	esac

	asset="$PROG-$os-$arch"
	if [ -n "$REL_VERSION" ]; then
		url="https://github.com/$REPO/releases/download/$REL_VERSION/$asset"
	else
		url="https://github.com/$REPO/releases/latest/download/$asset"
	fi

	tmp=$(mktemp 2>/dev/null || echo "/tmp/$asset.$$")
	trap 'rm -f "$tmp"' EXIT INT TERM
	echo "Downloading $asset..."
	if ! fetch "$url" "$tmp"; then
		echo "tangi: download failed ($url)." >&2
		echo "       No matching release asset? Build from source instead." >&2
		exit 1
	fi
	chmod +x "$tmp"
	echo "Installing to $BINDIR ${SUDO:+(with sudo)}..."
	$SUDO mkdir -p "$BINDIR"
	$SUDO install -m 0755 "$tmp" "$BINDIR/$PROG"
else
	# ---- build from source ----
	[ -n "$SELF_DIR" ] && [ -f "$SELF_DIR/Makefile" ] || {
		echo "tangi: --from-source needs a clone (no Makefile next to this script)." >&2
		exit 1
	}
	echo "Building $PROG..."
	make -C "$SELF_DIR" >/dev/null
	[ -x "$SELF_DIR/$PROG" ] || { echo "tangi: build did not produce ./$PROG" >&2; exit 1; }
	echo "Installing to $BINDIR ${SUDO:+(with sudo)}..."
	$SUDO mkdir -p "$BINDIR"
	$SUDO install -m 0755 "$SELF_DIR/$PROG" "$BINDIR/$PROG"
fi

echo "Installed $BINDIR/$PROG"

# ---- PATH check ----
case ":$PATH:" in
	*":$BINDIR:"*)
		echo
		echo "✅ Done. Run it from anywhere:"
		echo "     $PROG 1h"
		;;
	*)
		shell_name=$(basename -- "${SHELL:-sh}")
		case "$shell_name" in
			zsh)  rc="$HOME/.zshrc" ;;
			bash) rc="$HOME/.bashrc" ;;
			*)    rc="your shell profile" ;;
		esac
		echo
		echo "⚠  $BINDIR is not on your PATH yet."
		echo "   Add it by appending this line to $rc, then restart your shell:"
		echo
		echo "     export PATH=\"$BINDIR:\$PATH\""
		echo
		echo "   (or run with the full path for now: $BINDIR/$PROG 1h)"
		;;
esac
