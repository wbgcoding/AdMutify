#pragma once

#include <vector>

// Layer of a test: pure logic, UI drawing and strings, or real system resources (audio devices,
// registry, windows, threads, media playback). Each group runs alone with --group.
enum class TestGroup { Logic, Ui, Integration };

struct TestCase {
    const char* name;
    void (*fn)();
    TestGroup group;
};

std::vector<TestCase>& Registry();
void ReportFailure(const char* expr, const char* file, int line);

struct Registrar {
    Registrar(const char* name, void (*fn)(), TestGroup group) { Registry().push_back({name, fn, group}); }
};

// Every test file names its default group once: `TEST_FILE_GROUP(Logic);` after the includes.
#define TEST_FILE_GROUP(group) static constexpr TestGroup kFileTestGroup = TestGroup::group

#define TEST_IN_GROUP(name, group)                                   \
    static void test_##name();                                       \
    static Registrar registrar_##name(#name, &test_##name, group);   \
    static void test_##name()

#define TEST(name) TEST_IN_GROUP(name, kFileTestGroup)
// A test that touches real system resources inside a file of another group.
#define TEST_INTEGRATION(name) TEST_IN_GROUP(name, TestGroup::Integration)

#define CHECK(expr) \
    do { if (!(expr)) ReportFailure(#expr, __FILE__, __LINE__); } while (0)
