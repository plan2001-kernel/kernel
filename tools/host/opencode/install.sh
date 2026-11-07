#!/usr/bin/env bash
# Install the automatic Plan2001-O source publication hook for OpenCode.
#
# The project already carries `.opencode/plugins/plan2001-worker-publish.js`,
# so an OpenCode process started in this canonical worktree loads the hook
# without any manual step. This script additionally links the same plugin into
# the global OpenCode plugin directory for workers started elsewhere.
#
# The plugin is keyed by project directory and registers its hooks only once,
# so loading both the project copy and the global copy is safe.
set -eu

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
PLUGIN="$ROOT/tools/host/opencode/plan2001-worker-publish.js"
CONFIG_DIR="${OPENCODE_CONFIG_DIR:-$HOME/.config/opencode}"
TARGET="$CONFIG_DIR/plugins/plan2001-worker-publish.js"

[ -f "$PLUGIN" ] || { echo "missing plugin source: $PLUGIN" >&2; exit 1; }
mkdir -p "$CONFIG_DIR/plugins"

if [ -L "$TARGET" ] && [ "$(readlink -- "$TARGET")" = "$PLUGIN" ]; then
    echo "already linked: $TARGET"
    exit 0
fi
if [ -e "$TARGET" ] || [ -L "$TARGET" ]; then
    backup="$TARGET.bak.$(date -u +%Y%m%dT%H%M%SZ)"
    mv -- "$TARGET" "$backup"
    echo "backed up: $backup"
fi
ln -s -- "$PLUGIN" "$TARGET"
echo "linked: $TARGET -> $PLUGIN"
echo "Restart OpenCode to load the publication hook."
