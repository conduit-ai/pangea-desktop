# Pangea native shell. build.sh configures this once per project with the PANGEA_* variables,
# after generating FaustDsp.h (and copying logic.cpp and ui.json) into PANGEA_GENERATED_DIR.
#
# Four plugin targets share one build directory: PangeaEffect (aufx, no MIDI), PangeaInstrument
# (aumu, MIDI in), and for a MIDI tool (docs/midi-tools.md) the pair Pattern Player ships as:
# PangeaMidi (an instrument with MIDI out, AU aumu and VST3, silent audio, for Ableton Live and
# Bitwig) and PangeaMidiFx (an AU MIDI effect, aumi, for Logic's MIDI FX slot, named "<name> FX"
# with its own code and bundle id). build.sh builds the ones the project's kind asks for. The
# plugin's name, codes and version are kept out of the JUCE module objects (see "identity"
# below), so a new project or a new version only recompiles the shell and the format wrappers.
cmake_minimum_required(VERSION 3.22)

set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Oldest macOS the plugin runs on")

set(PANGEA_NAME "Pangea Plugin" CACHE STRING "Plugin name shown in the host")
set(PANGEA_SLUG "pangea-plugin" CACHE STRING "Project slug")
set(PANGEA_BUNDLE_ID "com.pangea.pangea-plugin" CACHE STRING "Bundle identifier")
set(PANGEA_MANUFACTURER_CODE "Pnga" CACHE STRING "Four character manufacturer code")
set(PANGEA_PLUGIN_CODE "Pgpl" CACHE STRING "Four character plugin code, first one upper case")
set(PANGEA_VERSION "0.1.0" CACHE STRING "Plugin version, three numbers")
set(PANGEA_DESCRIPTION "" CACHE STRING "One line description")
option(PANGEA_IS_SYNTH "Instrument (aumu, MIDI in) rather than effect (aufx)" OFF)
option(PANGEA_IS_MIDI_TOOL "A MIDI tool: the logic module's notes go out as MIDI, no sound" OFF)
set(PANGEA_FX_PLUGIN_CODE "Pgpx" CACHE STRING "A MIDI tool's AU MIDI effect: four characters, first one upper case")
set(PANGEA_FX_BUNDLE_ID "com.pangea.pangea-plugin.fx" CACHE STRING "A MIDI tool's AU MIDI effect: bundle identifier")
set(PANGEA_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated" CACHE PATH "Holds FaustDsp.h, ui.json, credit.json and, optionally, FaustEffect.h and LogicModule.cpp")
option(PANGEA_HAS_LOGIC "The project has a logic module at PANGEA_GENERATED_DIR/LogicModule.cpp" OFF)
option(PANGEA_HAS_EFFECT "The instrument has an effect section, class mydsp_effect in PANGEA_GENERATED_DIR/FaustEffect.h" OFF)
set(PANGEA_FAUST_INCLUDE "" CACHE PATH "Directory holding faust/dsp/dsp.h and the other Faust architecture headers")
option(PANGEA_BUILD_RENDER "Also build pangea-render, the offline render and check tool" OFF)
option(PANGEA_WEB_EDITOR "Test: draw the interface with the component library in a web view (PangeaWebEditor.h)" OFF)
option(PANGEA_STANDALONE "Test: also build a standalone app of each plugin, for measuring" OFF)

project(PangeaShell VERSION ${PANGEA_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION OFF)   # link-time optimisation took the bench from 7 s to over 2 min
set(JUCE_ENABLE_MODULE_SOURCE_GROUPS OFF)
set(JUCE_COPY_PLUGIN_AFTER_BUILD OFF)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    message(FATAL_ERROR "JUCE is missing. Clone JUCE 8.0.10 into ${CMAKE_CURRENT_SOURCE_DIR}/JUCE")
endif()
if(NOT EXISTS "${PANGEA_FAUST_INCLUDE}/faust/dsp/poly-dsp.h")
    message(FATAL_ERROR "Faust headers not found under '${PANGEA_FAUST_INCLUDE}'. Pass -DPANGEA_FAUST_INCLUDE=<prefix>/include")
endif()

add_subdirectory(JUCE)

# The Pangea plugin (docs/pangea-plugin.md): three plugins, written once, that open any version as
# a module at run time. build-host.sh configures its own build directory with PANGEA_HOST_ONLY, so
# nothing below (a project's sources) is needed or touched.
option(PANGEA_HOST_ONLY "Build the Pangea plugin (PangeaHost*) and nothing project specific" OFF)
option(PANGEA_BUILD_HOST_CHECK "Also build pangea-host-check, the Pangea plugin's offline checks" OFF)
if(PANGEA_HOST_ONLY)
    # The web panel (PangeaWebEditor.h): the page, the component library, its fonts and JUCE's own
    # bridge script, compiled into the plugin and served to the web view from memory.
    file(GLOB pangea_web_files CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/web/*.html"
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/web/*.js"
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/web/*.css"
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/web/fonts/*.woff2"
        # Each font family's licence (SIL OFL 1.1), carried by every copy of the fonts.
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/web/fonts/licences/*.txt")
    juce_add_binary_data(PangeaWebData
        NAMESPACE PangeaWebData
        HEADER_NAME PangeaWebData.h
        SOURCES ${pangea_web_files}
            "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/modules/juce_gui_extra/native/javascript/index.js"
            "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/modules/juce_gui_extra/native/javascript/check_native_interop.js")

    # pangea_add_host(<target> <kind> NAME <n> CODE <c> BUNDLE <b> [IS_SYNTH] [MIDI_OUT] [MIDI_EFFECT] [FORMATS <f>...])
    function(pangea_add_host target kind)
        cmake_parse_arguments(PARSE_ARGV 2 arg "IS_SYNTH;MIDI_OUT;MIDI_EFFECT" "NAME;CODE;BUNDLE" "FORMATS")
        if(NOT arg_FORMATS)
            set(arg_FORMATS AU VST3)
        endif()
        set(midi_in FALSE)
        if(arg_IS_SYNTH OR arg_MIDI_OUT OR arg_MIDI_EFFECT)
            set(midi_in TRUE)
        endif()
        juce_add_plugin(${target}
            PRODUCT_NAME "${arg_NAME}"
            COMPANY_NAME "Pangea"
            BUNDLE_ID "${arg_BUNDLE}"
            PLUGIN_MANUFACTURER_CODE "${PANGEA_MANUFACTURER_CODE}"
            PLUGIN_CODE "${arg_CODE}"
            VERSION "${PANGEA_VERSION}"
            DESCRIPTION "Opens your Pangea plugins and follows their new versions"
            FORMATS ${arg_FORMATS}
            IS_SYNTH ${arg_IS_SYNTH}
            NEEDS_MIDI_INPUT ${midi_in}
            NEEDS_MIDI_OUTPUT ${arg_MIDI_OUT}
            IS_MIDI_EFFECT ${arg_MIDI_EFFECT}
            EDITOR_WANTS_KEYBOARD_FOCUS FALSE
            COPY_PLUGIN_AFTER_BUILD FALSE)
        target_sources(${target} PRIVATE platform/PangeaHost.cpp)
        target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/platform" "${PANGEA_FAUST_INCLUDE}")
        target_compile_definitions(${target} PUBLIC
            PANGEA_HOST_KIND=${kind}
            JUCE_WEB_BROWSER=1
            JUCE_USE_CURL=0
            JUCE_VST3_CAN_REPLACE_VST2=0)
        target_link_libraries(${target}
            PRIVATE juce::juce_audio_processors juce::juce_cryptography juce::juce_gui_extra PangeaWebData
            PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    endfunction()

    pangea_add_host(PangeaHostEffect 0 NAME "Pangea" CODE "Phfx" BUNDLE "com.pangea.host.effect")
    pangea_add_host(PangeaHostInstrument 1 NAME "Pangea Instrument" CODE "Phin" BUNDLE "com.pangea.host.instrument" IS_SYNTH MIDI_OUT)
    pangea_add_host(PangeaHostMidiFx 2 NAME "Pangea MIDI FX" CODE "Phmi" BUNDLE "com.pangea.host.midifx" MIDI_OUT MIDI_EFFECT FORMATS AU)

    if(PANGEA_BUILD_HOST_CHECK)
        juce_add_console_app(pangea-host-check PRODUCT_NAME "pangea-host-check")
        target_sources(pangea-host-check PRIVATE tools/host-check/Main.cpp)
        target_include_directories(pangea-host-check PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/platform" "${PANGEA_FAUST_INCLUDE}")
        target_compile_definitions(pangea-host-check PRIVATE
            JUCE_WEB_BROWSER=1
            JUCE_USE_CURL=0
            JUCE_PLUGINHOST_VST3=1
            JUCE_MODAL_LOOPS_PERMITTED=1)   # it pumps the message loop while a loaded plugin settles
        target_link_libraries(pangea-host-check PRIVATE
            juce::juce_audio_processors
            juce::juce_cryptography
            juce::juce_gui_extra
            PangeaWebData
            juce::juce_recommended_config_flags)
    endif()
    return()
endif()

# The interface: the project's ui.json, compiled into the plugin as a byte array and nothing else.
# build.sh copies it into the generated directory (an empty object when the project has none);
# the editor reads it at run time with juce::JSON and checks every field (platform/PangeaUi.h).
if(NOT EXISTS "${PANGEA_GENERATED_DIR}/ui.json")
    file(WRITE "${PANGEA_GENERATED_DIR}/ui.json" "{}\n")
endif()
# With it, credit.json: the owner's name and the site's address from plugin.json, which the web
# panel's footer shows and links to (platform/PangeaWebEditor.h, Credit). build.sh writes it with
# tools/write-credit.py; an empty object names nobody and links to Pangea's own name.
if(NOT EXISTS "${PANGEA_GENERATED_DIR}/credit.json")
    file(WRITE "${PANGEA_GENERATED_DIR}/credit.json" "{}\n")
endif()
juce_add_binary_data(PangeaUiData
    NAMESPACE PangeaUiData
    HEADER_NAME PangeaUiData.h
    SOURCES "${PANGEA_GENERATED_DIR}/ui.json" "${PANGEA_GENERATED_DIR}/credit.json")

# The web view test: the page, the component library, its fonts and JUCE's own bridge script, all
# compiled into the plugin and served to the web view from memory (PangeaWebEditor.h).
if(PANGEA_WEB_EDITOR)
    file(GLOB pangea_web_files CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/web/*.html"
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/web/*.js"
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/web/*.css"
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/web/fonts/*.woff2"
        # Each font family's licence (SIL OFL 1.1), carried by every copy of the fonts.
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/web/fonts/licences/*.txt")
    juce_add_binary_data(PangeaWebData
        NAMESPACE PangeaWebData
        HEADER_NAME PangeaWebData.h
        SOURCES ${pangea_web_files}
            "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/modules/juce_gui_extra/native/javascript/index.js"
            "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/modules/juce_gui_extra/native/javascript/check_native_interop.js")
endif()

# The shell source carries everything project specific as definitions on this one file,
# so changing a project does not touch the JUCE module objects.
set(shell_definitions
    "PANGEA_NAME=\"${PANGEA_NAME}\""
    "PANGEA_IS_SYNTH=$<IF:$<BOOL:${PANGEA_IS_SYNTH}>,1,0>"
    "PANGEA_IS_MIDI_TOOL=$<IF:$<BOOL:${PANGEA_IS_MIDI_TOOL}>,1,0>"
    "PANGEA_WEB_EDITOR=$<IF:$<BOOL:${PANGEA_WEB_EDITOR}>,1,0>")
if(PANGEA_HAS_LOGIC)
    list(APPEND shell_definitions "HAS_LOGIC=1")
else()
    list(APPEND shell_definitions "HAS_LOGIC=0")
endif()
# An instrument's effect section, the stage its voices share: tools/build-effect.sh wrote its class
# beside FaustDsp.h, and the shell runs it on the mixed voices (decision 73).
if(PANGEA_HAS_EFFECT)
    list(APPEND shell_definitions "HAS_EFFECT=1")
else()
    list(APPEND shell_definitions "HAS_EFFECT=0")
endif()
set_source_files_properties(platform/HybridShell.cpp PROPERTIES COMPILE_DEFINITIONS "${shell_definitions}")

# The logic module is its own translation unit, compiled with the step budget's flags and nothing of
# the shell, so only the module's code counts steps (platform/logic_native.flags, PangeaLogicGuard.h;
# docs/midi-tools.md, "The step budget"; decision 46).
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/platform/logic_native.flags" logic_native_flags REGEX "^-")
set_source_files_properties(platform/logic_native.cpp PROPERTIES
    COMPILE_OPTIONS "${logic_native_flags}"
    COMPILE_DEFINITIONS "LOGIC_FILE=\"LogicModule.cpp\"")

# pangea_add_plugin(<target> <is synth> [MIDI_OUT] [MIDI_EFFECT] [NAME <n>] [CODE <c>] [BUNDLE <b>] [FORMATS <f>...])
function(pangea_add_plugin target is_synth)
    cmake_parse_arguments(PARSE_ARGV 2 arg "MIDI_OUT;MIDI_EFFECT" "NAME;CODE;BUNDLE" "FORMATS")
    if(NOT arg_NAME)
        set(arg_NAME "${PANGEA_NAME}")
    endif()
    if(NOT arg_CODE)
        set(arg_CODE "${PANGEA_PLUGIN_CODE}")
    endif()
    if(NOT arg_BUNDLE)
        set(arg_BUNDLE "${PANGEA_BUNDLE_ID}")
    endif()
    if(NOT arg_FORMATS)
        set(arg_FORMATS AU VST3)
        if(PANGEA_STANDALONE)
            list(APPEND arg_FORMATS Standalone)
        endif()
    endif()
    set(needs_midi_in ${is_synth})
    set(needs_midi_out FALSE)
    set(is_midi_effect FALSE)
    if(arg_MIDI_OUT)
        set(needs_midi_in TRUE)
        set(needs_midi_out TRUE)
    endif()
    if(arg_MIDI_EFFECT)
        set(needs_midi_in TRUE)
        set(is_midi_effect TRUE)
    endif()
    juce_add_plugin(${target}
        PRODUCT_NAME "${arg_NAME}"
        COMPANY_NAME "Pangea"
        BUNDLE_ID "${arg_BUNDLE}"
        PLUGIN_MANUFACTURER_CODE "${PANGEA_MANUFACTURER_CODE}"
        PLUGIN_CODE "${arg_CODE}"
        VERSION "${PANGEA_VERSION}"
        DESCRIPTION "${PANGEA_DESCRIPTION}"
        FORMATS ${arg_FORMATS}
        IS_SYNTH ${is_synth}
        NEEDS_MIDI_INPUT ${needs_midi_in}
        NEEDS_MIDI_OUTPUT ${needs_midi_out}
        IS_MIDI_EFFECT ${is_midi_effect}
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE)

    target_sources(${target} PRIVATE platform/HybridShell.cpp)
    if(PANGEA_HAS_LOGIC)
        target_sources(${target} PRIVATE platform/logic_native.cpp)
    endif()
    target_include_directories(${target} PRIVATE
        "${PANGEA_GENERATED_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/platform"
        "${PANGEA_FAUST_INCLUDE}")
    target_compile_definitions(${target} PUBLIC
        JUCE_WEB_BROWSER=$<IF:$<BOOL:${PANGEA_WEB_EDITOR}>,1,0>
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)
    target_link_libraries(${target}
        PRIVATE juce::juce_audio_processors PangeaUiData
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    if(PANGEA_WEB_EDITOR)
        target_link_libraries(${target} PRIVATE juce::juce_gui_extra PangeaWebData)
    endif()
    if(PANGEA_STANDALONE)
        target_link_libraries(${target} PRIVATE juce::juce_audio_utils juce::juce_audio_devices)
    endif()

    # Identity. juce_add_plugin puts the name, codes and version into PUBLIC definitions of the
    # shared code target, which would recompile every JUCE module for every new project or version.
    # The AU and VST3 wrappers (separate targets) still receive them through the INTERFACE side;
    # the shared code gets its name from PANGEA_NAME on HybridShell.cpp instead.
    set(identity_pattern "^JucePlugin_(Name|Desc|Version|VersionString|VersionCode|PluginCode|CFBundleIdentifier|AAXIdentifier|ARAFactoryID|ARADocumentArchiveID|ARACompatibleArchiveIDs|AUExportPrefix|AUExportPrefixQuoted)=")
    get_target_property(all_definitions ${target} COMPILE_DEFINITIONS)
    set(private_definitions "${all_definitions}")
    list(FILTER private_definitions EXCLUDE REGEX "${identity_pattern}")
    set_target_properties(${target} PROPERTIES COMPILE_DEFINITIONS "${private_definitions}")

    # The VST3 manifest helper (a small tool JUCE builds and runs to write moduleinfo.json) copies
    # the shared code's private definitions, so it gets the identity back directly.
    set(identity_definitions "${all_definitions}")
    list(FILTER identity_definitions INCLUDE REGEX "${identity_pattern}")
    if(TARGET ${target}_vst3_helper)
        target_compile_definitions(${target}_vst3_helper PRIVATE ${identity_definitions})
    endif()
endfunction()

pangea_add_plugin(PangeaEffect FALSE)
pangea_add_plugin(PangeaInstrument TRUE)
# A MIDI tool, built from the same sources twice: for Ableton Live and Bitwig, and for Logic.
pangea_add_plugin(PangeaMidi TRUE MIDI_OUT)
pangea_add_plugin(PangeaMidiFx FALSE MIDI_OUT MIDI_EFFECT
    NAME "${PANGEA_NAME} FX" CODE "${PANGEA_FX_PLUGIN_CODE}" BUNDLE "${PANGEA_FX_BUNDLE_ID}" FORMATS AU)

if(PANGEA_BUILD_RENDER)
    juce_add_console_app(pangea-render PRODUCT_NAME "pangea-render")
    target_sources(pangea-render PRIVATE tools/render/Main.cpp tools/render/MidiChecks.cpp tools/render/BypassChecks.cpp tools/render/SidechainChecks.cpp platform/HybridShell.cpp)
    if(PANGEA_HAS_LOGIC)
        target_sources(pangea-render PRIVATE platform/logic_native.cpp)
    endif()
    target_include_directories(pangea-render PRIVATE
        "${PANGEA_GENERATED_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/platform"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/render"
        "${PANGEA_FAUST_INCLUDE}")
    target_compile_definitions(pangea-render PRIVATE
        PANGEA_RENDER_TOOL=1
        JUCE_WEB_BROWSER=$<IF:$<BOOL:${PANGEA_WEB_EDITOR}>,1,0>
        JUCE_USE_CURL=0
        JUCE_PLUGINHOST_VST3=1
        JUCE_MODAL_LOOPS_PERMITTED=0)
    target_link_libraries(pangea-render PRIVATE
        juce::juce_audio_processors
        juce::juce_audio_formats
        PangeaUiData
        juce::juce_recommended_config_flags)
    if(PANGEA_WEB_EDITOR)
        target_link_libraries(pangea-render PRIVATE juce::juce_gui_extra PangeaWebData)
    endif()
endif()
