add_library(mpt_asset_import
  src/byte_source.cpp
  src/dump_format.cpp
  src/gc_disc.cpp
  src/importer.cpp
  src/known_dumps.cpp
  src/sha256.cpp
)
add_library(mpt::asset_import ALIAS mpt_asset_import)
target_include_directories(mpt_asset_import PUBLIC include)
target_link_libraries(mpt_asset_import
  PUBLIC mpt::core
  PRIVATE nlohmann_json::nlohmann_json)
mpt_set_warnings(mpt_asset_import)

# known_dumps.json ships next to the executable (hashes and metadata only).
set(MPT_KNOWN_DUMPS_JSON "${CMAKE_CURRENT_SOURCE_DIR}/known_dumps.json" CACHE INTERNAL "")
