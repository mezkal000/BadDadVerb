#!/usr/bin/env bash
# =============================================================================
# DADBADBASS — macOS PKG Installer Builder
# Requires: Xcode Command Line Tools (pkgbuild, productbuild)
# Usage:    bash build_macos_pkg.sh [path/to/Build/Release/artefacts]
# =============================================================================

set -euo pipefail

APP_NAME="DADBADBASS"
APP_VERSION="1.0.0"
BUNDLE_ID="com.dadlabs.dadbass"
COMPANY="DadLabs"

# ── Paths ─────────────────────────────────────────────────────────────────────
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$(dirname "$(dirname "$SCRIPT_DIR")")"

BUILD_ARTEFACTS="${1:-$PROJECT_ROOT/Build/DadBass_artefacts/Release}"

VST3_SRC="$BUILD_ARTEFACTS/VST3/DADBADBASS.vst3"
AU_SRC="$BUILD_ARTEFACTS/AU/DADBADBASS.component"
STANDALONE_SRC="$BUILD_ARTEFACTS/Standalone/DADBADBASS.app"

OUT_DIR="$SCRIPT_DIR/Output"
STAGE_DIR="$OUT_DIR/stage"
PKG_OUT="$OUT_DIR/${APP_NAME}_${APP_VERSION}_macOS.pkg"

mkdir -p "$OUT_DIR" "$STAGE_DIR"

echo "==> Building macOS PKG for $APP_NAME $APP_VERSION"

# ── Helper: build a component pkg ─────────────────────────────────────────────
make_component_pkg() {
    local src="$1"
    local install_location="$2"
    local component_id="$3"
    local out_pkg="$4"

    if [ ! -e "$src" ]; then
        echo "[WARN] Not found, skipping: $src"
        return
    fi

    echo "  Packaging: $src  →  $install_location"

    local payload_dir="$STAGE_DIR/$(basename "$src")-payload"
    mkdir -p "$payload_dir$install_location"
    cp -R "$src" "$payload_dir$install_location/"

    pkgbuild \
        --root "$payload_dir" \
        --identifier "$component_id" \
        --version "$APP_VERSION" \
        --install-location "/" \
        "$out_pkg"

    rm -rf "$payload_dir"
    echo "  Created: $out_pkg"
}

# ── Component packages ─────────────────────────────────────────────────────────
VST3_PKG="$STAGE_DIR/DADBADBASS_VST3.pkg"
AU_PKG="$STAGE_DIR/DADBADBASS_AU.pkg"
STANDALONE_PKG="$STAGE_DIR/DADBADBASS_Standalone.pkg"

make_component_pkg \
    "$VST3_SRC" \
    "/Library/Audio/Plug-Ins/VST3" \
    "com.dadlabs.dadbass.vst3" \
    "$VST3_PKG"

make_component_pkg \
    "$AU_SRC" \
    "/Library/Audio/Plug-Ins/Components" \
    "com.dadlabs.dadbass.au" \
    "$AU_PKG"

make_component_pkg \
    "$STANDALONE_SRC" \
    "/Applications" \
    "com.dadlabs.dadbass.standalone" \
    "$STANDALONE_PKG"

# ── Distribution XML ───────────────────────────────────────────────────────────
DIST_XML="$STAGE_DIR/distribution.xml"

cat > "$DIST_XML" << DISTXML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>DADBADBASS ${APP_VERSION}</title>
    <organization>com.dadlabs</organization>
    <domains enable_localSystem="true"/>
    <options customize="allow" require-scripts="false" rootVolumeOnly="true"/>

    <welcome    file="Welcome.rtf"    mime-type="text/rtf"/>
    <license    file="License.rtf"    mime-type="text/rtf"/>
    <conclusion file="Conclusion.rtf" mime-type="text/rtf"/>

    <choices-outline>
        <line choice="vst3"/>
        <line choice="au"/>
        <line choice="standalone"/>
    </choices-outline>

    <choice id="vst3"       title="VST3 Plugin"     description="Installs DADBADBASS.vst3 to /Library/Audio/Plug-Ins/VST3">
        <pkg-ref id="com.dadlabs.dadbass.vst3"/>
    </choice>
    <choice id="au"         title="Audio Unit (AU)"  description="Installs DADBADBASS.component to /Library/Audio/Plug-Ins/Components">
        <pkg-ref id="com.dadlabs.dadbass.au"/>
    </choice>
    <choice id="standalone" title="Standalone App"   description="Installs DADBADBASS.app to /Applications">
        <pkg-ref id="com.dadlabs.dadbass.standalone"/>
    </choice>

    <pkg-ref id="com.dadlabs.dadbass.vst3"       version="${APP_VERSION}" auth="root">DADBADBASS_VST3.pkg</pkg-ref>
    <pkg-ref id="com.dadlabs.dadbass.au"         version="${APP_VERSION}" auth="root">DADBADBASS_AU.pkg</pkg-ref>
    <pkg-ref id="com.dadlabs.dadbass.standalone" version="${APP_VERSION}" auth="root">DADBADBASS_Standalone.pkg</pkg-ref>
</installer-gui-script>
DISTXML

# ── RTF stubs ──────────────────────────────────────────────────────────────────
cat > "$STAGE_DIR/Welcome.rtf" << 'RTF'
{\rtf1\ansi\deff0
{\colortbl;\red200\green100\blue0;}
\cf1\b\fs36 DADBADBASS\b0\cf0\fs24\par
Vintage Soviet Bass Amplifier Plugin\par\par
This installer will install DADBADBASS VST3, AU, and Standalone on your Mac.\par
}
RTF

cp "$STAGE_DIR/Welcome.rtf" "$STAGE_DIR/License.rtf"
cp "$STAGE_DIR/Welcome.rtf" "$STAGE_DIR/Conclusion.rtf"

# ── Build distribution package ─────────────────────────────────────────────────
PKG_REFS=()
[ -f "$VST3_PKG"       ] && PKG_REFS+=("$VST3_PKG")
[ -f "$AU_PKG"         ] && PKG_REFS+=("$AU_PKG")
[ -f "$STANDALONE_PKG" ] && PKG_REFS+=("$STANDALONE_PKG")

if [ ${#PKG_REFS[@]} -eq 0 ]; then
    echo "[ERROR] No component packages were created. Check build artefacts path."
    echo "        Expected: $BUILD_ARTEFACTS"
    exit 1
fi

echo "==> Assembling final PKG: $PKG_OUT"
productbuild \
    --distribution "$DIST_XML" \
    --package-path "$STAGE_DIR" \
    --resources    "$STAGE_DIR" \
    "$PKG_OUT"

echo ""
echo "==> SUCCESS"
echo "    Output: $PKG_OUT"
SIZE=$(du -sh "$PKG_OUT" 2>/dev/null | cut -f1 || echo "?")
echo "    Size:   $SIZE"
