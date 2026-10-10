#pragma once

#include <vector>

struct TestCase {
    const char* name;
    void (*fn)();
};

std::vector<TestCase>& Registry();
void ReportFailure(const char* expr, const char* file, int line);

struct Registrar {
    Registrar(const char* name, void (*fn)()) { Registry().push_back({name, fn}); }
};

#define TEST(name)                                         \
    static void test_##name();                             \
    static Registrar registrar_##name(#name, &test_##name); \
    static void test_##name()

#define CHECK(expr) \
    do { if (!(expr)) ReportFailure(#expr, __FILE__, __LINE__); } while (0)
