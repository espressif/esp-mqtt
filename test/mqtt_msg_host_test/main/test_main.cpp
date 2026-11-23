/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#include <catch2/catch_session.hpp>

int main(int argc, char **argv)
{
    return Catch::Session().run(argc, argv);
}

// The linux FreeRTOS port adds -u app_main to the link.
extern "C" void app_main(void)
{
}
