// Gate test for the config value parser (src/features/cfgparse.h).
// Build + run:  cmake --build build --config Release --target cfgparse_test && build\Release\cfgparse_test.exe
#include "../src/features/cfgparse.h"
#include <cstdio>

static int s_failed = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++s_failed; } } while (0)

int main()
{
    // bools: the values old configs contain, plus garbage that must not throw
    CHECK(CfgParseBool("1"));
    CHECK(CfgParseBool("true"));
    CHECK(CfgParseBool("2"));
    CHECK(!CfgParseBool("0"));
    CHECK(!CfgParseBool(""));
    CHECK(!CfgParseBool("false"));
    CHECK(!CfgParseBool("yes"));

    // ints: clamped, garbage leaves the value alone
    int i = 7;
    CHECK(CfgParseInt("42", -FLT_MAX, FLT_MAX, i) && i == 42);
    CHECK(CfgParseInt("-3", -FLT_MAX, FLT_MAX, i) && i == -3);
    CHECK(CfgParseInt("99", 4.f, 64.f, i) && i == 64);
    CHECK(CfgParseInt("1", 4.f, 64.f, i) && i == 4);
    CHECK(CfgParseInt("12abc", -FLT_MAX, FLT_MAX, i) && i == 12);        // prefix, like std::stoi
    i = 7;
    CHECK(!CfgParseInt("abc", -FLT_MAX, FLT_MAX, i) && i == 7);
    CHECK(!CfgParseInt("", -FLT_MAX, FLT_MAX, i) && i == 7);
    CHECK(CfgParseInt("99999999999999", -FLT_MAX, FLT_MAX, i) && i == INT_MAX);   // stoi threw here
    CHECK(CfgParseInt("-99999999999999", -FLT_MAX, FLT_MAX, i) && i == INT_MIN);

    // floats: clamped, non-finite and garbage rejected
    float f = 0.5f;
    CHECK(CfgParseFloat("2.5", -FLT_MAX, FLT_MAX, f) && f == 2.5f);
    CHECK(CfgParseFloat("-1e3", -FLT_MAX, FLT_MAX, f) && f == -1000.f);
    CHECK(CfgParseFloat("7", 0.f, 1.f, f) && f == 1.f);
    CHECK(CfgParseFloat("-7", 0.25f, 1.f, f) && f == 0.25f);
    CHECK(CfgParseFloat("3", 0.5f, FLT_MAX, f) && f == 3.f);                      // max(lo, x) rows
    f = 0.5f;
    CHECK(!CfgParseFloat("nope", -FLT_MAX, FLT_MAX, f) && f == 0.5f);
    CHECK(!CfgParseFloat("nan", -FLT_MAX, FLT_MAX, f) && f == 0.5f);
    CHECK(!CfgParseFloat("inf", -FLT_MAX, FLT_MAX, f) && f == 0.5f);
    CHECK(!CfgParseFloat("", -FLT_MAX, FLT_MAX, f) && f == 0.5f);

    std::printf(s_failed ? "cfgparse_test: %d FAILED\n" : "cfgparse_test: all passed\n", s_failed);
    return s_failed ? 1 : 0;
}
