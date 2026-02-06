#include <cstddef>
#include <string_view>

#include <umirtm/print.hh>
#include <umirtm/printf.hh>
#include <umirtm/rtm.hh>
#include <umitest/test.hh>

using namespace umi::test;

namespace {

using TestMonitor = rt::Monitor<1, 1, 16, 16, rt::Mode::NoBlockSkip>;

bool test_monitor_write_and_capacity(TestContext& t) {
    TestMonitor::init("RT MONITOR");

    bool ok = true;
    ok &= t.assert_eq(TestMonitor::get_available<0>(), std::size_t{0});
    ok &= t.assert_eq(TestMonitor::get_free_space<0>(), std::size_t{15});

    auto first = TestMonitor::write<0>(std::string_view{"hello"});
    ok &= t.assert_eq(first, std::size_t{5});
    ok &= t.assert_eq(TestMonitor::get_available<0>(), std::size_t{5});
    ok &= t.assert_eq(TestMonitor::get_free_space<0>(), std::size_t{10});

    auto second = TestMonitor::write<0>(std::string_view{"0123456789ABCDEFG"});
    ok &= t.assert_eq(second, std::size_t{10});
    ok &= t.assert_eq(TestMonitor::get_available<0>(), std::size_t{15});
    ok &= t.assert_eq(TestMonitor::get_free_space<0>(), std::size_t{0});

    auto third = TestMonitor::write<0>(std::string_view{"x"});
    ok &= t.assert_eq(third, std::size_t{0});

    return ok;
}

bool test_snprintf_formatting(TestContext& t) {
    char buffer[128]{};
    int n = rt::snprintf<rt::DefaultConfig>(buffer, sizeof(buffer), "num=%d hex=%#x str=%s", -12, 0x2a, "ok");

    bool ok = true;
    ok &= t.assert_true(n > 0);
    ok &= t.assert_eq(std::string_view{buffer}, std::string_view{"num=-12 hex=0x2a str=ok"});
    return ok;
}

bool test_print_and_newline(TestContext& t) {
    int n1 = rt::print("value={} hex={:#x}", 42, 0x2a);
    int n2 = rt::println();

    bool ok = true;
    ok &= t.assert_true(n1 > 0);
    ok &= t.assert_eq(n2, 1);
    return ok;
}

} // namespace

int main() {
    Suite suite("umirtm");

    suite.section("monitor");
    suite.run("write/capacity", test_monitor_write_and_capacity);

    suite.section("format");
    suite.run("snprintf/basic", test_snprintf_formatting);
    suite.run("print/newline", test_print_and_newline);

    return suite.summary();
}
