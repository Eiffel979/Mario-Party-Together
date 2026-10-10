# Host tools for the GX layer's dependencies (developer recipe, not part of any game build).
# Built by the root project, where asset-import's verified fetcher is available.
add_executable(mpt_fetch_sources fetch_sources.cpp)
target_link_libraries(mpt_fetch_sources PRIVATE mpt::asset_import)
mpt_set_warnings(mpt_fetch_sources)
