#pragma once
#include <cstdio>
#include <cmath>

extern int g_testsRun;
extern int g_testsFailed;

#define CHECK(cond) do { g_testsRun++; if (!(cond)) { g_testsFailed++; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_NEAR(a, b, eps) do { g_testsRun++; double _a = (a), _b = (b); if (std::fabs(_a - _b) > (eps)) { g_testsFailed++; std::printf("FAIL %s:%d: %s = %g, expected %g\n", __FILE__, __LINE__, #a, _a, _b); } } while (0)
#define TEST_SECTION(name) std::printf("== %s\n", name)
