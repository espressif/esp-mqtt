/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Log interceptor: RAII install/restore, coalesce esp-log fragments into full
 * lines, parse them into structured Entry records, and forward to the original
 * vprintf.
 */

#include "test_log_intercept.hpp"
#include "esp_log.h"
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <stdexcept>
#include <string_view>

namespace test::esp_log
{

namespace
{
std::optional<esp_log_level_t> level_from_char(char c)
{
    switch (c) {
    case 'E': return ESP_LOG_ERROR;

    case 'W': return ESP_LOG_WARN;

    case 'I': return ESP_LOG_INFO;

    case 'D': return ESP_LOG_DEBUG;

    case 'V': return ESP_LOG_VERBOSE;

    default:  return std::nullopt;
    }
}
} // namespace

std::optional<Entry> parse_line(std::string_view line)
{
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
        line.remove_suffix(1);
    }

    if (line.size() < 2) {
        return std::nullopt;
    }

    auto level = level_from_char(line[0]);

    if (!level) {
        return std::nullopt;
    }

    if (line[1] != ' ') {
        return std::nullopt;
    }

    line.remove_prefix(2);

    // Optional "(timestamp) " segment.
    if (!line.empty() && line.front() == '(') {
        auto close = line.find(") ");

        if (close == std::string_view::npos) {
            return std::nullopt;
        }

        line.remove_prefix(close + 2);
    }

    auto sep = line.find(": ");

    if (sep == std::string_view::npos) {
        return std::nullopt;
    }

    Entry e;
    e.level = *level;
    e.tag.assign(line.substr(0, sep));
    e.message.assign(line.substr(sep + 2));
    return e;
}

Capture *Capture::s_current = nullptr;

int Capture::capture_vprintf_cb(const char *format, va_list args)
{
    if (s_current == nullptr) {
        return vprintf(format, args);
    }

    return s_current->register_and_forward(format, args);
}

Capture::Capture()
{
    if (s_current != nullptr) {
        throw std::logic_error{"Another test::esp_log::Capture is already active"};
    }

    original_vprintf = esp_log_set_vprintf(capture_vprintf_cb);
    s_current = this;
}

Capture::~Capture()
{
    if (s_current == this) {
        s_current = nullptr;

        if (original_vprintf != nullptr) {
            esp_log_set_vprintf(original_vprintf);
        }
    }
}

void Capture::clear()
{
    captured_entries.clear();
    partial_line.clear();
}

bool Capture::contains(std::string_view substring) const
{
    return std::ranges::any_of(captured_entries, [substring](const Entry & e) {
        return e.message.contains(substring);
    });
}

bool Capture::contains(std::string_view tag, std::string_view substring) const
{
    return std::ranges::any_of(captured_entries, [tag, substring](const Entry & e) {
        return e.tag == tag && e.message.contains(substring);
    });
}

bool Capture::contains_in_order(std::span<const std::string_view> substrings,
                                std::string_view tag) const
{
    auto it = captured_entries.begin();

    for (const auto &expected : substrings) {
        it = std::find_if(it, captured_entries.end(), [&](const Entry & e) {
            return (tag.empty() || e.tag == tag) && e.message.contains(expected);
        });

        if (it == captured_entries.end()) {
            return false;
        }

        ++it;
    }

    return true;
}

void Capture::ingest(std::string_view chunk)
{
    partial_line.append(chunk);
    size_t start = 0;

    while (true) {
        auto nl = partial_line.find('\n', start);

        if (nl == std::string::npos) {
            break;
        }

        std::string_view line{partial_line.data() + start, nl - start};

        if (auto entry = parse_line(line)) {
            captured_entries.push_back(std::move(*entry));
        }

        start = nl + 1;
    }

    if (start > 0) {
        partial_line.erase(0, start);
    }
}

int Capture::register_and_forward(const char *format, va_list args)
{
    va_list measure_args;
    va_copy(measure_args, args);
    auto needed = static_cast<size_t>(vsnprintf(nullptr, 0, format, measure_args));
    va_end(measure_args);

    if (needed > 0) {
        std::string fragment;
        fragment.resize_and_overwrite(needed, [&](char *buf, size_t cap) -> size_t {
            va_list args_copy;
            va_copy(args_copy, args);
            int n = vsnprintf(buf, cap + 1, format, args_copy);
            va_end(args_copy);

            if (n < 0) {
                return 0;
            }

            return std::min(static_cast<size_t>(n), cap);
        });
        ingest(fragment);
    }

    return (original_vprintf != nullptr) ? original_vprintf(format, args) : 0;
}

} // namespace test::esp_log
