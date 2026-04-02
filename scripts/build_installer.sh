#!/usr/bin/env bash
# Wrapper — builds the plugin then creates the .pkg installer
set -euo pipefail
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
ARTEFACTS="$PROJECT_DIR/Build/DadBass_artefacts/Release"

# Build first
bash "$SCRIPT_DIR/build.sh"

# Then package
bash "$PROJECT_DIR/Installer/macOS/build_macos_pkg.sh" "$ARTEFACTS"
