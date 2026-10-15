#include "testing.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 1 if the test currently running has failed */
static int currentFailed;

void testFail(const char* file, int line, const char* format, ...) {
    va_list args;

    currentFailed = 1;
    printf("\n    %s:%d: ", file, line);
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    printf("\n");
}

int runTests(const TestCase* tests, size_t count, void (*setUp)(void), void (*tearDown)(void),
        const char* filter) {
    size_t i;
    int ran = 0;
    int failed = 0;

    for(i = 0; i < count; i++) {
        if(filter && !strstr(tests[i].name, filter))
            continue;

        printf("%s ... ", tests[i].name);
        fflush(stdout);

        currentFailed = 0;
        if(setUp)
            setUp();
        tests[i].run();
        if(tearDown)
            tearDown();

        ran++;
        if(currentFailed) {
            failed++;
            printf("  FAILED\n");
        } else {
            printf("ok\n");
        }
    }

    printf("\n%d tests, %d passed, %d failed\n", ran, ran - failed, failed);

    if(ran == 0)
        return EXIT_FAILURE;
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
