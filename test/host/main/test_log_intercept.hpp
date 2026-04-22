/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Log interceptor for host tests: RAII guard that installs a vprintf hook,
 * coalesces log fragments into structured entries, and restores the original
 * vprintf on destruction.
 */

#pragma once

#include <esp_log_level.h>
#include <esp_log_write.h>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace test::esp_log
{

struct Entry {
    esp_log_level_t level = ESP_LOG_NONE;
    std::string tag;
    std::string message;
};

/**
 * Parse one fully-formatted esp-log line (without trailing newline) into an
 * Entry. Expected shape with colors disabled: "X (ts) tag: message" or
 * "X tag: message" when timestamps are disabled. Returns nullopt for lines
 * that do not match the esp-log format.
 */
std::optional<Entry> parse_line(std::string_view line);

/**
 * Capture installs a global esp-log vprintf hook on construction and restores
 * the previous one on destruction. Only one Capture may be active at a time;
 * overlapping instances trigger an assertion. The hook is single-threaded by
 * design: if your test emits logs from a secondary thread, serialize them
 * before asserting (this matches host-test usage where tasks are mocked).
 */
class Capture
{
public:
    Capture();
    ~Capture();

    Capture(const Capture &) = delete;
    Capture &operator=(const Capture &) = delete;

    /** Clear captured entries so assertions see only logs after this call. */
    void clear();

    /** Access the raw list of parsed entries, in capture order. */
    const std::vector<Entry> &entries() const
    {
        return captured_entries;
    }

    /** True if any captured entry's message contains substring. */
    bool contains(std::string_view substring) const;

    /** True if any captured entry with the given tag has a message containing substring. */
    bool contains(std::string_view tag, std::string_view substring) const;

    /**
     * True if all substrings appear in order across captured messages (each as
     * a substring of some entry's message). If tag is non-empty, only entries
     * with that tag are considered.
     */
    bool contains_in_order(std::span<const std::string_view> substrings,
                           std::string_view tag = {}) const;

    bool contains_in_order(std::initializer_list<std::string_view> substrings,
                           std::string_view tag = {}) const
    {
        return contains_in_order(std::span<const std::string_view> {substrings}, tag);
    }

private:
    static int capture_vprintf_cb(const char *format, va_list args);
    int register_and_forward(const char *format, va_list args);
    void ingest(std::string_view chunk);

    static Capture *s_current;

    vprintf_like_t original_vprintf = nullptr;
    std::string partial_line{};
    std::vector<Entry> captured_entries{};
};

} // namespace test::esp_log
