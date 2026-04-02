#!/usr/bin/env bash
# Build BadDadVerb on macOS
# Usage: bash scripts/build_mac.sh
set -e
cd "$(dirname "$0")/.."

echo "==> Configuring..."
cmake -B Build -DCMAKE_BUILD_TYPE=Release

echo "==> Building..."
cmake --build Build --config Release -j$(sysctl -n hw.logicalcpu)

VST3="Build/BadDadVerb_artefacts/Release/VST3/BadDadVerb.vst3"
AU="Build/BadDadVerb_artefacts/Release/AU/BadDadVerb.component"

VST3_DEST="/Library/Audio/Plug-Ins/VST3"
AU_DEST="/Library/Audio/Plug-Ins/Components"

if [ -d "$VST3" ]; then
    sudo cp -r "$VST3" "$VST3_DEST/"
    echo "==> Installed VST3 → $VST3_DEST/"
fi
if [ -d "$AU" ]; then
    sudo cp -r "$AU" "$AU_DEST/"
    echo "==> Installed AU  → $AU_DEST/"
fi
echo "==> Done. Rescan plugins in your DAW."
