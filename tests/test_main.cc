// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Test runner entry point for umimmio tests.
/// @author Shota Moriguchi @tekitounix

#include "test_fixture.hh"

int main() {
    umi::test::Suite suite("umimmio");

    umimmio::test::run_register_field_tests(suite);
    umimmio::test::run_transport_tests(suite);
    umimmio::test::run_access_policy_tests(suite);
    umimmio::test::run_byte_transport_tests(suite);

    return suite.summary();
}
