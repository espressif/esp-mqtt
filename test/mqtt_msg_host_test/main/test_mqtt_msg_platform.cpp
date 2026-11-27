/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#include <random>

extern "C" int platform_random(int max)
{
    if (max <= 0) {
        return 0;
    }

    static std::mt19937 engine{std::random_device{}()};
    std::uniform_int_distribution<int> distribution(0, max - 1);
    return distribution(engine);
}
