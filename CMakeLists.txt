# uZX build of zxtune's AY tracker players, used through uzx/ay_module.h only.
# The sources are the objects a static link of uzx/ay_module.cpp pulls in; when an upstream merge
# brings a link error, add the source that defines the missing symbol.

add_library(zxtune_aym STATIC
    uzx/ay_module.cpp

    3rdparty/fmt/src/format.cc
    src/binary/format/expression.cpp
    src/binary/format/full/factory.cpp
    src/binary/format/grammar.cpp
    src/binary/format/lexic_analysis.cpp
    src/binary/format/scanning.cpp
    src/binary/format/static_expression.cpp
    src/binary/format/syntax.cpp
    src/binary/src/container.cpp
    src/binary/src/crc.cpp
    src/binary/src/view.cpp
    src/debug/src/log.cpp
    src/devices/aym/src/aym.cpp
    src/devices/aym/src/turbosound.cpp
    src/formats/chiptune/aym/ascsoundmaster.cpp
    src/formats/chiptune/aym/fasttracker.cpp
    src/formats/chiptune/aym/globaltracker.cpp
    src/formats/chiptune/aym/prosoundcreator.cpp
    src/formats/chiptune/aym/prosoundmaker_compiled.cpp
    src/formats/chiptune/aym/protracker1.cpp
    src/formats/chiptune/aym/protracker2.cpp
    src/formats/chiptune/aym/protracker3_compiled.cpp
    src/formats/chiptune/aym/protracker3_vortex.cpp
    src/formats/chiptune/aym/soundtracker.cpp
    src/formats/chiptune/aym/soundtracker3.cpp
    src/formats/chiptune/aym/soundtracker_compiled.cpp
    src/formats/chiptune/aym/soundtrackerpro_compiled.cpp
    src/formats/chiptune/aym/sqtracker_compiled.cpp
    src/formats/chiptune/aym/turbosound.cpp
    src/formats/chiptune/common/builder_meta_stub.cpp
    src/formats/chiptune/common/builder_pattern_stub.cpp
    src/formats/chiptune/common/container.cpp
    src/formats/chiptune/common/metainfo.cpp
    src/l10n/stub/stub.cpp
    src/module/players/aym/ascsoundmaster.cpp
    src/module/players/aym/aym_base.cpp
    src/module/players/aym/aym_base_track.cpp
    src/module/players/aym/aym_factory.cpp
    src/module/players/aym/aym_parameters.cpp
    src/module/players/aym/aym_properties_helper.cpp
    src/module/players/aym/fasttracker.cpp
    src/module/players/aym/freq_tables.cpp
    src/module/players/aym/globaltracker.cpp
    src/module/players/aym/prosoundcreator.cpp
    src/module/players/aym/prosoundmaker.cpp
    src/module/players/aym/protracker1.cpp
    src/module/players/aym/protracker2.cpp
    src/module/players/aym/protracker3.cpp
    src/module/players/aym/soundtracker.cpp
    src/module/players/aym/soundtrackerpro.cpp
    src/module/players/aym/sqtracker.cpp
    src/module/players/aym/ts.cpp
    src/module/players/aym/turbosound.cpp
    src/module/players/aym/vortex.cpp
    src/module/players/properties_helper.cpp
    src/module/players/properties_meta.cpp
    src/module/players/streaming.cpp
    src/module/players/tracking.cpp
    src/parameters/src/container.cpp
    src/parameters/src/merged.cpp
    src/sound/impl/mixer.cpp
    src/sound/impl/mixer_params.cpp
    src/sound/impl/parameters.cpp
    src/strings/src/casing.cpp
    src/strings/src/encoding.cpp
    src/strings/src/optimize.cpp
    src/strings/src/sanitize.cpp
    src/strings/src/trim.cpp
    src/tools/src/range_checker.cpp
)

target_compile_features(zxtune_aym PRIVATE cxx_std_20)

# Only uzx/ay_module.h is public: zxtune's headers (types.h, error.h, ...) stay out of the consumer.
target_include_directories(zxtune_aym
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/fmt/include
)

# ThisFile() makes __FILE__ relative to this root at compile time.
target_compile_definitions(zxtune_aym PRIVATE SOURCES_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")

# zxtune is written for unsigned char; third-party warnings are not ours to fix.
if(MSVC)
    target_compile_options(zxtune_aym PRIVATE /J /w /bigobj /utf-8 /permissive- /Zc:__cplusplus)
else()
    target_compile_options(zxtune_aym PRIVATE -funsigned-char -fno-strict-aliasing -w)
endif()
