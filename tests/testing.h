#ifndef TESTING_H
#define TESTING_H

/*========================================================
 * A minimal unit test harness.
 *
 * Each test is a void function. A failed check prints a
 * message and returns from the test, and the run carries
 * on with the next test.
 *========================================================
 */

#include <stddef.h>

typedef struct {
    const char* name;
    void (*run)(void);
} TestCase;

#define TEST(name) { #name, name }

/* Record a failure. Used by the check macros. */
void testFail(const char* file, int line, const char* format, ...);

/* Run every test whose name contains filter (or all of them if filter is NULL).
 * setUp and tearDown, if non-NULL, run before and after each test.
 * Returns the process exit status. */
int runTests(const TestCase* tests, size_t count, void (*setUp)(void), void (*tearDown)(void),
        const char* filter);

#define CHECK(cond) do { \
    if(!(cond)) { \
        testFail(__FILE__, __LINE__, "CHECK(%s)", #cond); \
        return; \
    } \
} while(0)

#define CHECK_INT_EQ(expected, actual) do { \
    long e_ = (long)(expected); \
    long a_ = (long)(actual); \
    if(e_ != a_) { \
        testFail(__FILE__, __LINE__, "%s == %s: expected %ld, got %ld", #expected, #actual, e_, a_); \
        return; \
    } \
} while(0)

#define CHECK_STR_EQ(expected, actual) do { \
    const char* e_ = (expected); \
    const char* a_ = (actual); \
    if(strcmp(e_, a_) != 0) { \
        testFail(__FILE__, __LINE__, "%s:\n        expected \"%s\"\n        got      \"%s\"", #actual, e_, a_); \
        return; \
    } \
} while(0)

#endif /* TESTING_H */
