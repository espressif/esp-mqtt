/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Matchers are thin wrappers over Capture predicates; the heavy lifting lives
 * in Capture. This file only holds describe() text and the LogsInOrder glue
 * from stored std::string messages to a span<const std::string_view>.
 */

#include "test_log_matchers.hpp"
#include <algorithm>
#include <format>
#include <span>
#include <string_view>
#include <vector>

namespace test::esp_log::matchers
{

static const char *level_name(esp_log_level_t level)
{
    switch (level) {
    case ESP_LOG_ERROR: return "ERROR";

    case ESP_LOG_WARN:  return "WARN";

    case ESP_LOG_INFO:  return "INFO";

    case ESP_LOG_DEBUG: return "DEBUG";

    case ESP_LOG_VERBOSE: return "VERBOSE";

    default: return "?";
    }
}

std::string ContainsMessage::describe() const
{
    return std::format(R"(contains message "{}")", expected_message);
}

std::string ContainsMessageWithTag::describe() const
{
    return std::format(R"(contains message "{}" with tag "{}")",
    expected_message, expected_tag);
}

bool ContainsMessageAtLevel::match(const Capture &captured_log) const
{
    return std::any_of(captured_log.entries().begin(), captured_log.entries().end(),
    [&](const Entry & e) {
        return e.level == expected_level &&
               e.tag == expected_tag &&
               e.message.find(expected_message) != std::string::npos;
    });
}

std::string ContainsMessageAtLevel::describe() const
{
    return std::format(R"(contains {} message "{}" with tag "{}")",
    level_name(expected_level), expected_message, expected_tag);
}

bool LogsInOrder::match(const Capture &captured_log) const
{
    std::vector<std::string_view> views;
    views.reserve(expected_messages.size());

    for (const auto &s : expected_messages) {
        views.emplace_back(s);
    }

    return captured_log.contains_in_order(std::span<const std::string_view> {views},
                                          expected_tag ? std::string_view{*expected_tag}
                                          : std::string_view{});
}

std::string LogsInOrder::describe() const
{
    std::string list = "[";

    if (!expected_messages.empty()) {
        for (const auto &expected : expected_messages) {
            list += std::format(R"("{}", )", expected);
        }

        list.resize(list.size() - 2);
    }

    list += ']';
    const std::string scope = expected_tag
                              ? std::format(R"(with tag "{}")", *expected_tag)
                              : std::string{"(any tag)"};
    return std::format("logs {} in order {}", list, scope);
}

} // namespace test::esp_log::matchers
