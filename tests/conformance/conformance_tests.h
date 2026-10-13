#pragma once

// Turns the conformance checks into doctest cases for one target. In a test file:
//
//   mpt::conformance::Target my_target();   // builds the target
//   MPT_CONFORMANCE_SUITE("MyRuntime", my_target)
//
// Each suite is one TEST_CASE named "<label> conformance", with one SUBCASE per check, and
// every problem a check finds is reported as a separate failure under that subcase.
//
// One TEST_CASE per suite, not one per check: doctest names its generated functions after
// __LINE__ on Clang 22 and later (DOCTEST_COUNTER), so several TEST_CASEs expanded from
// one macro would collide. Subcases live in separate scopes, so they cannot. Put each
// MPT_CONFORMANCE_SUITE on its own line.

#include <doctest/doctest.h>

#include <string>

#include "conformance.h"

namespace mpt::conformance {

inline void expect_conforms(const Problems& problems) {
  for (const std::string& problem : problems) FAIL_CHECK(problem);
  CHECK(problems.empty());
}

}  // namespace mpt::conformance

#define MPT_CONFORMANCE_CHECK(TARGET_FN, CHECK_NAME, CHECK_FN) \
  SUBCASE(CHECK_NAME) { ::mpt::conformance::expect_conforms(CHECK_FN(TARGET_FN())); }

#define MPT_CONFORMANCE_SUITE(LABEL, TARGET_FN)                                                          \
  TEST_CASE(LABEL " conformance") {                                                                     \
    MPT_CONFORMANCE_CHECK(TARGET_FN, "frame rate validity", ::mpt::conformance::check_frame_rate)              \
    MPT_CONFORMANCE_CHECK(TARGET_FN, "frame numbering and determinism", ::mpt::conformance::check_frame_numbering) \
    MPT_CONFORMANCE_CHECK(TARGET_FN, "save state round trip", ::mpt::conformance::check_save_load)             \
    MPT_CONFORMANCE_CHECK(TARGET_FN, "video and audio views", ::mpt::conformance::check_views)                 \
    MPT_CONFORMANCE_CHECK(TARGET_FN, "presentation options", ::mpt::conformance::check_presentation_options)   \
    MPT_CONFORMANCE_CHECK(TARGET_FN, "save media", ::mpt::conformance::check_save_media)                       \
    MPT_CONFORMANCE_CHECK(TARGET_FN, "stick folding", ::mpt::conformance::check_stick_folding)                 \
    MPT_CONFORMANCE_CHECK(TARGET_FN, "adapter snapshots", ::mpt::conformance::check_adapter)                   \
  }
