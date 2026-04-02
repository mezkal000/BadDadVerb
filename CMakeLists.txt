cmake_minimum_required(VERSION 3.22)
project(BadDadVerb VERSION 0.1.0)

# ── Fetch JUCE 8 automatically ────────────────────────────────────────────────
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)
FetchContent_MakeAvailable(JUCE)

# ── Embedded binary assets ────────────────────────────────────────────────────
juce_add_binary_data(DadBassAssets
    NAMESPACE DadBassAssets
    SOURCES
        Resources/panel_rust.png
        Resources/knob_bakelite.png
        Resources/crt_bezel.png
        Resources/tube_glow.png
        Resources/logo_plate.png
        Resources/switch_plate.png
        # ── Synthetic impulse responses — one per preset ──
        Resources/IR/01_MKULTRA_ROOM.wav
        Resources/IR/02_ARTICHOKE.wav
        Resources/IR/03_BLUEBIRD.wav
        Resources/IR/04_PAPERCLIP_HALL.wav
        Resources/IR/05_MOCKINGBIRD.wav
        Resources/IR/06_NORTHWOODS.wav
        Resources/IR/07_GLADIO_CHURCH.wav
        Resources/IR/08_COINTELPRO.wav
        Resources/IR/09_ECHELON.wav
        Resources/IR/10_DULCE_BASE.wav
        Resources/IR/11_PHILLY_EXP.wav
        Resources/IR/12_STARGATE.wav
        Resources/IR/13_MONARCH.wav
        Resources/IR/14_CHEMTRAIL.wav
        Resources/IR/15_AREA51.wav
        Resources/IR/16_ZOG_ROOM.wav
        Resources/IR/17_ELLIOTS_ABDUCTION.wav
        # ── Embedded fonts with Cyrillic support ──
        Resources/Fonts/DejaVuSans-Bold.ttf
        Resources/Fonts/DejaVuSans.ttf
)

# ── Plugin target ─────────────────────────────────────────────────────────────
juce_add_plugin(BadDadVerb
    COMPANY_NAME                "DadLabs"
    BUNDLE_ID                   "com.dadlabs.baddadverb"
    PLUGIN_MANUFACTURER_CODE    "DLAB"
    PLUGIN_CODE                 "BDVB"
    FORMATS                     VST3 AU Standalone
    PRODUCT_NAME                "BadDadVerb"
    DESCRIPTION                 "BadDadVerb — IR convolution reverb"
    AU_MAIN_TYPE                "kAudioUnitType_Effect"
    AU_EXPORT_PREFIX            "BadDadVerbAU"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
)

# ── Source files ──────────────────────────────────────────────────────────────
# All .cpp files must be listed here. Header-only files (.h) are optional
# but help IDEs. The PRIVATE keyword ensures they go into the shared-code
# object that all plugin format targets link against.
target_sources(BadDadVerb
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginProcessor.h
        Source/PluginEditor.cpp
        Source/PluginEditor.h
        Source/PresetManager.h
        Source/UI/Oscilloscope.h
        Source/UI/RustLookAndFeel.h
        Source/DSP/DSPHelpers.h
        Source/DSP/AudioToIR.h
)

# ── C++17 ─────────────────────────────────────────────────────────────────────
target_compile_features(BadDadVerb PRIVATE cxx_std_17)

# ── Compile definitions ───────────────────────────────────────────────────────
target_compile_definitions(BadDadVerb
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

# ── Link libraries ────────────────────────────────────────────────────────────
target_link_libraries(BadDadVerb
    PRIVATE
        DadBassAssets
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_plugin_client
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_data_structures
        juce::juce_dsp
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
)

# ── Compiler warnings ─────────────────────────────────────────────────────────
if(MSVC)
    target_compile_options(BadDadVerb PRIVATE /W3 /wd4100 /wd4189 /wd4244)
else()
    target_compile_options(BadDadVerb PRIVATE
        -Wall
        -Wno-unused-parameter
        -Wno-unused-const-variable
        -Wno-unused-variable
    )
endif()



# ── macOS: disable code signing for local dev ─────────────────────────────────
if(APPLE)
    set_target_properties(BadDadVerb PROPERTIES
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY          ""
        XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED       NO
        XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED        NO
    )
endif()
