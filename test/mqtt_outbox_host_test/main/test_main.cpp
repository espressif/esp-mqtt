/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#include <cstdio>
#include <cstdlib>
#include <catch2/catch_session.hpp>

extern "C" void app_main(void)
{
    int argc = 1;
    const char *argv[2] = {"mqtt_outbox_host_test", nullptr};
    int result = Catch::Session().run(argc, argv);

    if (result != 0) {
        printf("Test failed with result %d\n", result);
        exit(1);
    } else {
        printf("Test passed.\n");
        exit(0);
    }
}
