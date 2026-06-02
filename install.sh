#!/bin/sh
# tangi installer — builds tangi and installs it onto your PATH so you can run
# `tangi` from anywhere.
#
#   ./install.sh                 build + install to /usr/local/bin (uses sudo if needed)
#   ./install.sh --user          install to ~/.local/bin (no sudo)
#   ./install.sh --prefix DIR    install to DIR/bin
#   ./install.sh --uninstall     remove tangi (and stop any running session)
#   ./install.sh --help
#
# Honors $CC for the compiler.

set -eu

PROG=tangi
SELF_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

PREFIX=""
USER_INSTALL=0
UNINSTALL=0

# ---- parse args ----
while [ $# -gt 0 ]; do
	case "$1" in
		--user)       USER_INSTALL=1 ;;
		--uninstall)  UNINSTALL=1 ;;
		--prefix)     shift; PREFIX="${1:-}";  [ -n "$PREFIX" ] || { echo "tangi: --prefix needs a directory" >&2; exit 2; } ;;
		--prefix=*)   PREFIX="${1#--prefix=}" ;;
		-h|--help)
			sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'
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
	# parent must exist & be writable, or BINDIR itself writable
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
		echo "       Try: ./install.sh --user" >&2
		exit 1
	fi
fi

# ---- uninstall ----
if [ "$UNINSTALL" -eq 1 ]; then
	# Stop a running session first so nothing is left holding a lock.
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

# ---- build ----
echo "Building $PROG..."
make -C "$SELF_DIR" >/dev/null
[ -x "$SELF_DIR/$PROG" ] || { echo "tangi: build did not produce ./$PROG" >&2; exit 1; }

# ---- install ----
echo "Installing to $BINDIR ${SUDO:+(with sudo)}..."
$SUDO mkdir -p "$BINDIR"
$SUDO install -m 0755 "$SELF_DIR/$PROG" "$BINDIR/$PROG"
echo "Installed $BINDIR/$PROG"

# ---- PATH check ----
case ":$PATH:" in
	*":$BINDIR:"*)
		echo
		echo "✅ Done. Run it from anywhere:"
		echo "     $PROG 1h"
		;;
	*)
		# Guess the right rc file for a helpful hint.
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
