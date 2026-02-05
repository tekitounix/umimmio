#include <cassert>
#include <cstring>
#include <iostream>
#include <umistring/umistring.hh>

using namespace umistring;

void test_string_view() {
    constexpr StringView sv{"Hello"};
    assert(sv.size() == 5);
    assert(sv[0] == 'H');
    assert(sv[4] == 'o');
    std::cout << "✓ StringView basic operations" << std::endl;
}

void test_string_view_substr() {
    constexpr StringView sv{"Hello"};
    constexpr auto sub = sv.substr(1, 3);
    assert(sub.size() == 3);
    std::cout << "✓ StringView substr" << std::endl;
}

void test_empty_string_view() {
    constexpr StringView empty_sv;
    assert(empty_sv.empty());
    assert(empty_sv.size() == 0);
    std::cout << "✓ StringView empty" << std::endl;
}

void test_to_string() {
    char buf[16];
    auto len = to_string(42, buf, 16);
    assert(len == 2);
    assert(std::strcmp(buf, "42") == 0);
    std::cout << "✓ to_string(42)" << std::endl;
}

void test_to_string_zero() {
    char buf[16];
    auto len = to_string(0, buf, 16);
    assert(len == 1);
    assert(std::strcmp(buf, "0") == 0);
    std::cout << "✓ to_string(0)" << std::endl;
}

void test_to_string_negative() {
    char buf[16];
    auto len = to_string(-42, buf, 16);
    assert(len == 3);
    assert(std::strcmp(buf, "-42") == 0);
    std::cout << "✓ to_string(-42)" << std::endl;
}

void test_split() {
    constexpr StringView split_sv{"a,b,c"};
    constexpr auto result = split(split_sv, ',');
    assert(result.count == 3);
    assert(result.parts[0].size() == 1);
    assert(result.parts[0][0] == 'a');
    assert(result.parts[1][0] == 'b');
    assert(result.parts[2][0] == 'c');
    std::cout << "✓ split" << std::endl;
}

void test_split_single() {
    constexpr StringView single_sv{"hello"};
    constexpr auto single_result = split(single_sv, ',');
    assert(single_result.count == 1);
    assert(single_result.parts[0].size() == 5);
    std::cout << "✓ split single" << std::endl;
}

void test_trim() {
    constexpr StringView trim_sv{"  hello  "};
    constexpr auto trimmed = trim(trim_sv);
    assert(trimmed.size() == 5);
    assert(trimmed[0] == 'h');
    std::cout << "✓ trim" << std::endl;
}

void test_trim_no_spaces() {
    constexpr StringView no_space_sv{"hello"};
    constexpr auto no_trim = trim(no_space_sv);
    assert(no_trim.size() == 5);
    std::cout << "✓ trim (no spaces)" << std::endl;
}

int main() {
    test_string_view();
    test_string_view_substr();
    test_empty_string_view();
    test_to_string();
    test_to_string_zero();
    test_to_string_negative();
    test_split();
    test_split_single();
    test_trim();
    test_trim_no_spaces();

    std::cout << "\nAll tests passed!" << std::endl;
    return 0;
}
