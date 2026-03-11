// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Test runner entry point for umimmio tests.
/// @author Shota Moriguchi @tekitounix

#include <umitest/test.hh>

#include "test_access_policy.hh"
#include "test_byte_transport.hh"
#include "test_register_field.hh"
#include "test_transport.hh"

int main() {
    umi::test::Suite suite("umimmio");

    umimmio::test::register_register_field_tests(suite);
    umimmio::test::register_transport_tests(suite);
    umimmio::test::register_access_policy_tests(suite);
    umimmio::test::register_byte_transport_tests(suite);

    return suite.summary();
}
