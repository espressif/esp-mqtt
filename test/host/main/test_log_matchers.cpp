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
#include <format>
#include <span>
#include <string_view>
#include <vector>

namespace test::esp_log::matchers
{

std::string ContainsMessage::describe() const
{
    return std::format(R"(contains message "{}")", expected_message);
}

std::string ContainsMessageWithTag::describe() const
{
    return std::format(R"(contains message "{}" with tag "{}")",
    expected_message, expected_tag);
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
