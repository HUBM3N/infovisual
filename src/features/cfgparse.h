#pragma once
#include <algorithm>
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <string>

// Config value parsing. Never throws: a hand-edited config with a typo used to throw out of
// std::stoi/std::stof and take the game down at inject. False = not a number, and the caller
// leaves the setting as it was. Tested by tests/cfgparse_test.cpp.

// "1", "true", or any non-zero integer.
inline bool CfgParseBool(const std::string& text)
{
    if (text == "true") return true;
    char* end = nullptr;
    const long v = std::strtol(text.c_str(), &end, 10);
    return end != text.c_str() && v != 0;
}

inline bool CfgParseInt(const std::string& text, float lo, float hi, int& out)
{
    const char* s = text.c_str();
    char* end = nullptr;
    const long long v = std::strtoll(s, &end, 10);
    if (end == s) return false;
    const double l = std::max((double)lo, (double)INT_MIN);
    const double h = std::min((double)hi, (double)INT_MAX);
    out = (int)std::clamp((double)v, l, h);
    return true;
}

inline bool CfgParseFloat(const std::string& text, float lo, float hi, float& out)
{
    const char* s = text.c_str();
    char* end = nullptr;
    const float v = std::strtof(s, &end);
    if (end == s || !std::isfinite(v)) return false;
    out = std::clamp(v, lo, hi);
    return true;
}
