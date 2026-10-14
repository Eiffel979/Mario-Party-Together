#include <doctest/doctest.h>
#include "mpt/gc/musyx_engine.h"
TEST_CASE("musyx engine links") { CHECK(mpt::gc::MusyxEngine::stateRegions().size() == 2); }
