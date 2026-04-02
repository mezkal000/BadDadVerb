#!/usr/bin/env bash
# =============================================================================
# DADBADBASS — Build + install script
# Usage: bash scripts/build.sh
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/Build"

echo "==> DADBADBASS Build Script"
echo "    Project : $PROJECT_DIR"
echo "    Build   : $BUILD_DIR"
echo ""

if [ ! -f "$PROJECT_DIR/JUCE/CMakeLists.txt" ]; then
    echo "[ERROR] JUCE not found."
    echo "        Run: git clone https://github.com/juce-framework/JUCE.git \"$PROJECT_DIR/JUCE\""
    exit 1
fi

echo "==> Configuring..."
cmake -S "$PROJECT_DIR" \
      -B "$BUILD_DIR" \
      -DCMAKE_BUILD_TYPE=Release \
      -DJUCE_BUILD_EXAMPLES=OFF \
      -DJUCE_BUILD_EXTRAS=OFF

echo ""
echo "==> Building..."
cmake --build "$BUILD_DIR" \
      --config Release \
      --parallel "$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"

ARTEFACTS="$BUILD_DIR/DadBass_artefacts/Release"

echo ""
echo "==> Installing plugins locally..."

AU_SRC="$ARTEFACTS/AU/DADBADBASS.component"
AU_DEST="$HOME/Library/Audio/Plug-Ins/Components/DADBADBASS.component"
if [ -d "$AU_SRC" ]; then
    rm -rf "$AU_DEST"
    cp -R "$AU_SRC" "$AU_DEST"
    killall -9 AudioComponentRegistrar 2>/dev/null || true
    echo "    AU installed: $AU_DEST"
else
    echo "    [WARNING] AU not found at $AU_SRC"
fi

VST3_SRC="$ARTEFACTS/VST3/DADBADBASS.vst3"
VST3_DEST="$HOME/Library/Audio/Plug-Ins/VST3/DADBADBASS.vst3"
if [ -d "$VST3_SRC" ]; then
    rm -rf "$VST3_DEST"
    cp -R "$VST3_SRC" "$VST3_DEST"
    echo "    VST3 installed: $VST3_DEST"
else
    echo "    [WARNING] VST3 not found at $VST3_SRC"
fi

echo ""
echo "==> Done. Restart your DAW and rescan plugins."
