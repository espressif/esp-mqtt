/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Catch2 matchers over test::esp_log::Capture. These are thin wrappers around
 * Capture's predicates; they add describe() text for Catch2 failure output.
 * For plain boolean checks use Capture::contains / Capture::contains_in_order
 * directly.
 */

#pragma once

#include "test_log_intercept.hpp"
#include <catch2/matchers/catch_matchers_templated.hpp>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace test::esp_log::matchers
{

class ContainsMessage : public Catch::Matchers::MatcherGenericBase
{
public:
    explicit ContainsMessage(std::string_view expected_message)
        : expected_message(expected_message) {}

    bool match(const Capture &captured_log) const
    {
        return captured_log.contains(expected_message);
    }

    std::string describe() const override;

private:
    std::string expected_message;
};

class ContainsMessageWithTag : public Catch::Matchers::MatcherGenericBase
{
public:
    ContainsMessageWithTag(std::string_view expected_tag, std::string_view expected_message)
        : expected_tag(expected_tag), expected_message(expected_message) {}

    bool match(const Capture &captured_log) const
    {
        return captured_log.contains(expected_tag, expected_message);
    }

    std::string describe() const override;

private:
    std::string expected_tag;
    std::string expected_message;
};

class LogsInOrder : public Catch::Matchers::MatcherGenericBase
{
public:
    explicit LogsInOrder(std::initializer_list<std::string_view> expected_messages)
        : expected_messages(expected_messages.begin(), expected_messages.end()) {}
    explicit LogsInOrder(std::string_view expected_tag,
                         std::initializer_list<std::string_view> expected_messages)
        : expected_messages(expected_messages.begin(), expected_messages.end()),
          expected_tag(std::string{expected_tag}) {}

    bool match(const Capture &captured_log) const;

    std::string describe() const override;

private:
    std::vector<std::string> expected_messages;
    std::optional<std::string> expected_tag;
};

inline ContainsMessage HasMessage(std::string_view expected_message)
{
    return ContainsMessage{expected_message};
}

inline ContainsMessageWithTag HasMessageIn(std::string_view expected_tag,
                                           std::string_view expected_message)
{
    return ContainsMessageWithTag{expected_tag, expected_message};
}

inline LogsInOrder LogsInOrderAny(std::initializer_list<std::string_view> expected_messages)
{
    return LogsInOrder{expected_messages};
}

inline LogsInOrder LogsInOrderIn(std::string_view expected_tag,
                                 std::initializer_list<std::string_view> expected_messages)
{
    return LogsInOrder{expected_tag, expected_messages};
}

} // namespace test::esp_log::matchers
