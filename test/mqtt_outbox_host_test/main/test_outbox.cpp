/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * Isolated host tests for lib/mqtt_outbox.c.
 * All outbox API functions are called directly — no MQTT client, no transport,
 * no network stack required.
 */
#include <exception>
#include <catch2/catch_test_macros.hpp>
#include <rapidcheck.h>
#include <rapidcheck/catch.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

extern "C" {
#include "mqtt_outbox.h"
}

struct OutboxGuard {
    explicit OutboxGuard() : handle(outbox_init())
    {
        REQUIRE(handle != nullptr);
    }
    ~OutboxGuard()
    {
        outbox_destroy(handle);
    }
    OutboxGuard(const OutboxGuard &) = delete;
    OutboxGuard &operator=(const OutboxGuard &) = delete;

    outbox_handle_t handle;
};

static outbox_message_t make_msg(int msg_id, int qos, int msg_type,
                                 const char *payload, int len)
{
    outbox_message_t message{};
    message.msg_id   = msg_id;
    message.msg_qos  = qos;
    message.msg_type = msg_type;
    message.data     = reinterpret_cast<uint8_t *>(const_cast<char *>(payload));
    message.len      = len;
    message.remaining_data = nullptr;
    message.remaining_len  = 0;
    return message;
}

TEST_CASE("Outbox lifecycle")
{
    SECTION("init returns a non-null handle") {
        outbox_handle_t outbox = outbox_init();
        REQUIRE(outbox != nullptr);
        outbox_destroy(outbox);
    }
    SECTION("destroy on a non-empty outbox reclaims all items") {
        OutboxGuard outbox;
        auto message = make_msg(1, 1, 3, "hello", 5);
        REQUIRE(outbox_enqueue(outbox.handle, &message, 0) != nullptr);
        // destructor calls outbox_destroy — LSan catches leaks if it is omitted
    }
}

TEST_CASE("Outbox enqueue")
{
    OutboxGuard outbox;
    SECTION("enqueued item starts in QUEUED state") {
        auto message = make_msg(42, 1, 3, "data", 4);
        outbox_item_handle_t item = outbox_enqueue(outbox.handle, &message, 0);
        REQUIRE(item != nullptr);
        REQUIRE(outbox_item_get_pending(item) == QUEUED);
    }
    SECTION("size increases by item length after enqueue") {
        REQUIRE(outbox_get_size(outbox.handle) == 0);
        const char payload[] = "hello";
        auto message = make_msg(1, 1, 3, payload, static_cast<int>(sizeof(payload) - 1));
        outbox_enqueue(outbox.handle, &message, 0);
        REQUIRE(outbox_get_size(outbox.handle) == sizeof(payload) - 1);
    }
    SECTION("multiple enqueues accumulate size") {
        auto message1 = make_msg(1, 1, 3, "abc", 3);
        auto message2 = make_msg(2, 1, 3, "de",  2);
        outbox_enqueue(outbox.handle, &message1, 0);
        outbox_enqueue(outbox.handle, &message2, 0);
        REQUIRE(outbox_get_size(outbox.handle) == 5);
    }
}

TEST_CASE("Outbox FIFO dequeue")
{
    OutboxGuard outbox;
    SECTION("dequeue returns items in enqueue order") {
        auto message1 = make_msg(10, 1, 3, "first",  5);
        auto message2 = make_msg(20, 1, 3, "second", 6);
        auto message3 = make_msg(30, 1, 3, "third",  5);
        outbox_enqueue(outbox.handle, &message1, 0);
        outbox_enqueue(outbox.handle, &message2, 0);
        outbox_enqueue(outbox.handle, &message3, 0);
        uint16_t id;
        int type, qos;
        size_t len;
        outbox_item_handle_t item1 = outbox_dequeue(outbox.handle, QUEUED, nullptr);
        REQUIRE(item1 != nullptr);
        outbox_item_get_data(item1, &len, &id, &type, &qos);
        REQUIRE(id == 10);
        // promote first item so next dequeue skips it
        outbox_set_pending(outbox.handle, 10, TRANSMITTED);
        outbox_item_handle_t item2 = outbox_dequeue(outbox.handle, QUEUED, nullptr);
        REQUIRE(item2 != nullptr);
        outbox_item_get_data(item2, &len, &id, &type, &qos);
        REQUIRE(id == 20);
    }
    SECTION("dequeue returns nullptr when no item matches state") {
        auto message = make_msg(1, 1, 3, "x", 1);
        outbox_enqueue(outbox.handle, &message, 0);
        REQUIRE(outbox_dequeue(outbox.handle, TRANSMITTED, nullptr) == nullptr);
        REQUIRE(outbox_dequeue(outbox.handle, ACKNOWLEDGED, nullptr) == nullptr);
    }
    SECTION("dequeue on empty outbox returns nullptr") {
        REQUIRE(outbox_dequeue(outbox.handle, QUEUED, nullptr) == nullptr);
    }
}

TEST_CASE("Outbox state transitions")
{
    OutboxGuard outbox;
    SECTION("set_pending changes the state of an item") {
        auto message = make_msg(5, 1, 3, "msg", 3);
        outbox_enqueue(outbox.handle, &message, 0);
        REQUIRE(outbox_set_pending(outbox.handle, 5, TRANSMITTED) == ESP_OK);
        outbox_item_handle_t item = outbox_dequeue(outbox.handle, TRANSMITTED, nullptr);
        REQUIRE(item != nullptr);
        uint16_t id;
        int type, qos;
        size_t len;
        outbox_item_get_data(item, &len, &id, &type, &qos);
        REQUIRE(id == 5);
    }
    SECTION("full state cycle: QUEUED -> TRANSMITTED -> ACKNOWLEDGED -> CONFIRMED") {
        auto message = make_msg(7, 2, 3, "qos2", 4);
        outbox_enqueue(outbox.handle, &message, 0);
        REQUIRE(outbox_set_pending(outbox.handle, 7, TRANSMITTED)  == ESP_OK);
        REQUIRE(outbox_set_pending(outbox.handle, 7, ACKNOWLEDGED) == ESP_OK);
        REQUIRE(outbox_set_pending(outbox.handle, 7, CONFIRMED)    == ESP_OK);
        REQUIRE(outbox_dequeue(outbox.handle, CONFIRMED, nullptr) != nullptr);
    }
    SECTION("set_pending on unknown msg_id returns ESP_FAIL") {
        REQUIRE(outbox_set_pending(outbox.handle, 999, TRANSMITTED) == ESP_FAIL);
    }
}

TEST_CASE("Outbox concurrent states")
{
    OutboxGuard outbox;
    SECTION("QUEUED head does not shadow a later TRANSMITTED item") {
        auto message1 = make_msg(1, 1, 3, "first",  5);
        auto message2 = make_msg(2, 1, 3, "second", 6);
        outbox_enqueue(outbox.handle, &message1, 0);
        outbox_enqueue(outbox.handle, &message2, 0);
        // Promote the second item only
        outbox_set_pending(outbox.handle, 2, TRANSMITTED);
        // dequeue(QUEUED) must still return msg 1
        outbox_item_handle_t queued_item = outbox_dequeue(outbox.handle, QUEUED, nullptr);
        REQUIRE(queued_item != nullptr);
        uint16_t id; int type, qos; size_t len;
        outbox_item_get_data(queued_item, &len, &id, &type, &qos);
        REQUIRE(id == 1);
        // dequeue(TRANSMITTED) must return msg 2
        outbox_item_handle_t transmitted_item = outbox_dequeue(outbox.handle, TRANSMITTED, nullptr);
        REQUIRE(transmitted_item != nullptr);
        outbox_item_get_data(transmitted_item, &len, &id, &type, &qos);
        REQUIRE(id == 2);
    }
    SECTION("TRANSMITTED item is not visible to dequeue(QUEUED)") {
        auto message = make_msg(3, 1, 3, "only", 4);
        outbox_enqueue(outbox.handle, &message, 0);
        outbox_set_pending(outbox.handle, 3, TRANSMITTED);
        REQUIRE(outbox_dequeue(outbox.handle, QUEUED, nullptr) == nullptr);
    }
}

TEST_CASE("Outbox lookup by msg_id")
{
    OutboxGuard outbox;
    SECTION("outbox_get returns the correct item") {
        auto message1 = make_msg(100, 1, 3, "a", 1);
        auto message2 = make_msg(200, 1, 3, "b", 1);
        outbox_enqueue(outbox.handle, &message1, 0);
        outbox_enqueue(outbox.handle, &message2, 0);
        outbox_item_handle_t item = outbox_get(outbox.handle, 200);
        REQUIRE(item != nullptr);
        uint16_t id; int type, qos; size_t len;
        outbox_item_get_data(item, &len, &id, &type, &qos);
        REQUIRE(id == 200);
    }
    SECTION("outbox_get returns nullptr for unknown msg_id") {
        auto message = make_msg(1, 1, 3, "x", 1);
        outbox_enqueue(outbox.handle, &message, 0);
        REQUIRE(outbox_get(outbox.handle, 999) == nullptr);
    }
}

TEST_CASE("Outbox delete by msg_id and type")
{
    OutboxGuard outbox;
    SECTION("deleted item is no longer found") {
        auto message = make_msg(55, 1, 3, "del", 3);
        outbox_enqueue(outbox.handle, &message, 0);
        REQUIRE(outbox_get_size(outbox.handle) == 3);
        REQUIRE(outbox_delete(outbox.handle, 55, 3) == ESP_OK);
        REQUIRE(outbox_get(outbox.handle, 55) == nullptr);
        REQUIRE(outbox_get_size(outbox.handle) == 0);
    }
    SECTION("delete with wrong type returns ESP_FAIL") {
        auto message = make_msg(56, 1, 3, "x", 1);
        outbox_enqueue(outbox.handle, &message, 0);
        REQUIRE(outbox_delete(outbox.handle, 56, 99) == ESP_FAIL);
        REQUIRE(outbox_get(outbox.handle, 56) != nullptr);
    }
    SECTION("delete unknown msg_id returns ESP_FAIL") {
        REQUIRE(outbox_delete(outbox.handle, 9999, 3) == ESP_FAIL);
    }
}

TEST_CASE("Outbox delete by item handle")
{
    OutboxGuard outbox;
    SECTION("item is removed and size decreases") {
        auto message = make_msg(77, 1, 3, "item", 4);
        outbox_item_handle_t item = outbox_enqueue(outbox.handle, &message, 0);
        REQUIRE(item != nullptr);
        REQUIRE(outbox_get_size(outbox.handle) == 4);
        REQUIRE(outbox_delete_item(outbox.handle, item) == ESP_OK);
        REQUIRE(outbox_get(outbox.handle, 77) == nullptr);
        REQUIRE(outbox_get_size(outbox.handle) == 0);
    }
}

TEST_CASE("Outbox expiry")
{
    OutboxGuard outbox;
    SECTION("delete_expired removes items older than timeout") {
        // enqueue with tick=0; current_tick=200, timeout=100 → age=200 > 100
        auto message1 = make_msg(1, 1, 3, "old",   3);
        auto message2 = make_msg(2, 1, 3, "fresh", 5);
        outbox_enqueue(outbox.handle, &message1, 0);
        outbox_enqueue(outbox.handle, &message2, 150);  // tick=150, age=50 at current_tick=200
        int deleted = outbox_delete_expired(outbox.handle, 200, 100);
        REQUIRE(deleted == 1);
        REQUIRE(outbox_get(outbox.handle, 1) == nullptr);
        REQUIRE(outbox_get(outbox.handle, 2) != nullptr);
    }
    SECTION("delete_single_expired removes exactly one item") {
        auto message1 = make_msg(10, 1, 3, "old1", 4);
        auto message2 = make_msg(20, 1, 3, "old2", 4);
        outbox_enqueue(outbox.handle, &message1, 0);
        outbox_enqueue(outbox.handle, &message2, 0);
        // Both are expired at current_tick=500, timeout=100; only one is removed
        int id = outbox_delete_single_expired(outbox.handle, 500, 100);
        REQUIRE(id >= 0);
        // Exactly one item was removed — the other is still present
        int remaining = (outbox_get(outbox.handle, 10) != nullptr ? 1 : 0)
                        + (outbox_get(outbox.handle, 20) != nullptr ? 1 : 0);
        REQUIRE(remaining == 1);
    }
    SECTION("delete_expired returns 0 when no items are expired") {
        auto message = make_msg(1, 1, 3, "fresh", 5);
        outbox_enqueue(outbox.handle, &message, 1000);
        int deleted = outbox_delete_expired(outbox.handle, 1050, 100);
        REQUIRE(deleted == 0);
    }
}

// ---------------------------------------------------------------------------
// Property-based tests
// ---------------------------------------------------------------------------

TEST_CASE("Outbox size invariant (RapidCheck)")
{
    rc::prop("size equals sum of surviving item lengths after arbitrary enqueue/delete",
    []() {
        OutboxGuard outbox;
        // Generate between 1 and 10 items
        int count = *rc::gen::inRange(1, 11);
        uint64_t expected_size = 0;
        std::vector<std::pair<int, int>> items; // {msg_id, len}

        for (int i = 0; i < count; ++i) {
            int len = *rc::gen::inRange(1, 32);
            std::string payload(len, 'x');
            auto message = make_msg(i + 1, 1, 3, payload.c_str(), len);
            const bool enqueued = outbox_enqueue(outbox.handle, &message, 0) != nullptr;
            RC_ASSERT(enqueued);
            items.push_back({i + 1, len});
            expected_size += len;
        }

        RC_ASSERT(outbox_get_size(outbox.handle) == expected_size);

        // Randomly delete some items
        for (auto &[id, len] : items) {
            bool del = *rc::gen::arbitrary<bool>();

            if (del) {
                outbox_delete(outbox.handle, id, 3);
                expected_size -= len;
            }
        }

        RC_ASSERT(outbox_get_size(outbox.handle) == expected_size);
    });
}

TEST_CASE("Outbox FIFO ordering property (RapidCheck)")
{
    rc::prop("dequeue(QUEUED) returns items in exact enqueue order",
    []() {
        OutboxGuard outbox;
        int count = *rc::gen::inRange(1, 16);
        std::vector<int> msg_ids;
        msg_ids.reserve(count);

        for (int i = 0; i < count; ++i) {
            int id = i + 1;
            msg_ids.push_back(id);
            std::string payload = "p" + std::to_string(id);
            auto message = make_msg(id, 1, 3, payload.c_str(),
                                    static_cast<int>(payload.size()));
            const bool enqueued = outbox_enqueue(outbox.handle, &message, 0) != nullptr;
            RC_ASSERT(enqueued);
        }

        // Drain in order: promote each head item to TRANSMITTED after inspecting it
        // so the next dequeue(QUEUED) advances to the next item.
        for (int i = 0; i < count; ++i) {
            outbox_item_handle_t item = outbox_dequeue(outbox.handle, QUEUED, nullptr);
            const bool item_valid = item != nullptr;
            RC_ASSERT(item_valid);
            uint16_t id; int type, qos; size_t len;
            outbox_item_get_data(item, &len, &id, &type, &qos);
            RC_ASSERT(static_cast<int>(id) == msg_ids[i]);
            // Advance past this item for the next iteration
            outbox_set_pending(outbox.handle, id, TRANSMITTED);
        }

        // No more QUEUED items
        const bool no_queued_items = outbox_dequeue(outbox.handle, QUEUED, nullptr) == nullptr;
        RC_ASSERT(no_queued_items);
    });
}
