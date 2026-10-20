# asset-import's MP4 build recipe (host side; built with the tree's compiler, see recipe.h).
add_library(mpt_mp4_recipe STATIC src/recipe.cpp include/mpt/mp4/recipe.h)
add_library(mpt::mp4_recipe ALIAS mpt_mp4_recipe)
target_include_directories(mpt_mp4_recipe PUBLIC include)
target_link_libraries(mpt_mp4_recipe PUBLIC mpt::asset_import)
mpt_set_warnings(mpt_mp4_recipe)

if(MPT_BUILD_TESTS)
  find_package(doctest CONFIG REQUIRED)
  add_executable(mpt_mp4_recipe_tests tests/test_recipe.cpp)
  target_link_libraries(mpt_mp4_recipe_tests PRIVATE mpt::mp4_recipe doctest::doctest)
  target_compile_definitions(mpt_mp4_recipe_tests PRIVATE
    MPT_GC_WII_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../.."
    MPT_RECIPE_TEST_TMP="${CMAKE_CURRENT_BINARY_DIR}/test_tmp")
  mpt_set_warnings(mpt_mp4_recipe_tests)
  add_test(NAME mp4_recipe COMMAND mpt_mp4_recipe_tests)
endif()
