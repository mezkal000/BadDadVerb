# DadBass — Build Results

## Project Status
**All source files generated. Project is ready to configure and build.**

---

## Prerequisites

| Tool | Version | Notes |
|---|---|---|
| CMake | ≥ 3.22 | [cmake.org](https://cmake.org) |
| JUCE | 7.x or 8.x | Clone into `DadBass/JUCE/` |
| macOS: Xcode | ≥ 14 | AU support requires Xcode |
| Windows: Visual Studio | 2022 | or Build Tools with MSVC |
| Windows: Inno Setup | 6.x | [jrsoftware.org](https://jrsoftware.org/isinfo.php) — for installer only |

**Get JUCE:**
```bash
git clone https://github.com/juce-framework/JUCE.git DadBass/JUCE
```

---

## Build Instructions

### macOS (VST3 + AU + Standalone + PKG installer)
```bash
bash scripts/build.sh
```

### Windows (VST3 + Standalone + EXE installer)
```bat
scripts\build.bat
```

### Manual CMake (any platform)
```bash
cmake -S . -B Build -DCMAKE_BUILD_TYPE=Release
cmake --build Build --config Release --parallel
```

---

## Expected Output Paths

### macOS
| Format | Path |
|---|---|
| VST3 | `Build/DadBass_artefacts/Release/VST3/DadBass.vst3` |
| AU | `Build/DadBass_artefacts/Release/AU/DadBass.component` |
| Standalone | `Build/DadBass_artefacts/Release/Standalone/DadBass.app` |
| PKG Installer | `Installer/macOS/Output/DadBass_1.0.0_macOS.pkg` |

**Auto-copy install locations (COPY_PLUGIN_AFTER_BUILD=TRUE):**
- VST3 → `~/Library/Audio/Plug-Ins/VST3/DadBass.vst3`
- AU → `~/Library/Audio/Plug-Ins/Components/DadBass.component`

### Windows
| Format | Path |
|---|---|
| VST3 | `Build\DadBass_artefacts\Release\VST3\DadBass.vst3` |
| Standalone | `Build\DadBass_artefacts\Release\Standalone\DadBass.exe` |
| EXE Installer | `Installer\Windows\Output\DadBass_Setup_1.0.0_Win64.exe` |

**Auto-copy install location:**
- VST3 → `C:\Program Files\Common Files\VST3\DadBass.vst3`

---

## Project File Tree

```
DadBass/
├── CMakeLists.txt                     ← Main build config
├── JUCE/                              ← Clone JUCE here
├── Resources/
│   ├── panel_rust.png                 ← Background texture
│   ├── knob_bakelite.png              ← Knob skin
│   ├── crt_bezel.png                  ← CRT scope bezel
│   ├── tube_glow.png                  ← Tube glow overlay
│   ├── logo_plate.png                 ← ДАДБАСС logo plate
│   └── switch_plate.png               ← Button plate texture
├── Source/
│   ├── PluginProcessor.h / .cpp       ← APVTS, DSP chain, oversampling
│   ├── PluginEditor.h / .cpp          ← UI, knobs, scope, buttons
│   ├── DSP/
│   │   ├── DSPHelpers.h               ← fastTanh, softClip utilities
│   │   ├── NoiseGate.h                ← Downward expander
│   │   ├── TriodeStage.h              ← Asymmetric waveshaper + sag + hum
│   │   ├── ToneStack.h                ← 4-band EQ (Bass/Mid/Treble/Presence)
│   │   ├── Compressor.h               ← Optical-style RMS compressor
│   │   ├── CabinetBlock.h             ← FIR cabinet sim via convolution
│   │   └── Limiter.h                  ← Brickwall output limiter
│   └── UI/
│       ├── Oscilloscope.h             ← CRT phosphor scope with glitch
│       └── RustLookAndFeel.h          ← Soviet rust L&F (knobs, switches)
├── Installer/
│   ├── Windows/
│   │   └── DadBass_Installer.iss      ← Inno Setup script
│   └── macOS/
│       └── build_macos_pkg.sh         ← pkgbuild + productbuild script
└── scripts/
    ├── build.sh                       ← macOS/Linux build + PKG
    └── build.bat                      ← Windows build + installer
```

---

## DSP Signal Chain

```
Input
  │
  ├─ [Clean buffer copy] ──────────────────────────────────┐
  │                                                         │
  ↓                                                         │
NoiseGate                                                   │
  ↓                                                         │
[2× Oversampling UP — polyphase IIR]                        │
  ↓                                                         │
TriodeStage 1  (drive × 1.0,  bias 0.018, sag 0.42)        │
  ↓  [+ Cold War 50 Hz hum]                                 │
TriodeStage 2  (drive × 1.18, bias 0.035, sag 0.58)        │
  ↓  [+ Real Shit random ticks]                             │
[2× Oversampling DOWN]                                      │
  ↓                                                         │
ToneStack (Bass / Mid / Treble / Presence)                  │
  ↓                                                         │
CabinetBlock (64-tap FIR, 1×15" Soviet voicing)             │
  ↓                                                         │
Blend ← ─────────────────────────────────────────── (clean)┘
  ↓
Compressor (optical, ratio 1.5–6× controlled by Comp knob)
  ↓
Limiter (brickwall −0.3 dBFS)
  ↓
Output Gain
  ↓
Output
```

---

## Parameters (APVTS)

| ID | Name | Range | Default |
|---|---|---|---|
| `input` | Input | −18 → +24 dB | 0 dB |
| `drive` | Drive | 1.0 → 8.0 | 2.6 |
| `blend` | Blend | 0.0 → 1.0 | 0.65 |
| `bass` | Bass | −12 → +12 dB | +2 dB |
| `mid` | Mid | −12 → +12 dB | +1 dB |
| `treble` | Treble | −12 → +12 dB | −1 dB |
| `presence` | Presence | −12 → +12 dB | +0.5 dB |
| `gate` | Gate | −72 → −20 dB | −48 dB |
| `comp` | Comp | 0.0 → 1.0 | 0.45 |
| `output` | Output | −24 → +12 dB | 0 dB |
| `realshit` | REAL SHIT | bool | false |
| `coldwar` | COLD WAR | bool | false |

---

## Known Issues / Notes

- **Notarization**: macOS PKG is unsigned. For distribution, add your Apple Developer certificate via `--sign` flag in `pkgbuild` and run `notarytool`.
- **Code signing (Windows)**: EXE installer is unsigned. Use `signtool.exe` with a code signing cert for distribution.
- **JUCE version**: Tested against JUCE 7.x API. If using JUCE 8, `Convolution::loadImpulseResponse` signature is identical.
- **AU validation**: Run `auval -v aufx DDBS DLAB` after install to verify AU registration.
