/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <format>
#include "rapidcheck.h"
#include "mqtt_utils.h"

struct percent_encoded_string {
    std::string value;
};
struct uri_username {
    std::string value;
};
struct uri_password {
    std::string value;
};
struct uri_host {
    std::string value;
};

struct URI {
    uri_username username;
    uri_password password;
    uri_host host;
};

namespace rc
{

template<typename T>
struct base {
    static Gen<T> arbitrary()
    {
        return gen::build<T>(gen::set(&T::value));
    }
};

template<>
struct Arbitrary<percent_encoded_string> : base<percent_encoded_string> { };

template<>
struct Arbitrary<uri_username> : base<uri_username> { };
template<>
struct Arbitrary<uri_password> : base<uri_password> { };
template<>
struct Arbitrary<uri_host> : base<uri_host> { };

template<>
struct Arbitrary<URI> {
    static Gen<URI> arbitrary()
    {
        return gen::build<URI>(gen::set(&URI::username), gen::set(&URI::password), gen::set(&URI::host));
    }
};
}

std::string percent_encode(std::string input, std::string filter = "`~!@#$%^&*(){}[]:\";'<>?,./\\-+=|")
{
    std::string encoded_str;

    for (char c : input) {
        if (filter.find(c) == std::string::npos && std::isprint(c)) {
            encoded_str.push_back(c);
        } else {
            encoded_str += std::format("%{:02x}", c);
        }
    }

    return encoded_str;
}

TEST_CASE("Parse percent-encoded data")
{
    SECTION("Zero-length string as an input") {
        char c_style_zero_length_string[1] = {0};
        REQUIRE(esp_mqtt_decode_percent_encoded_string(c_style_zero_length_string) == 0);
    }
    SECTION("Constant string as an input") {
        std::string known_value_string = "p%40ssw0rd";
        std::vector<char> known_value_tmp_vector{std::begin(known_value_string), std::end(known_value_string)};
        known_value_tmp_vector.push_back('\0');
        esp_mqtt_decode_percent_encoded_string(known_value_tmp_vector.data());
        std::string result{known_value_tmp_vector.data()};
        REQUIRE(result == "p@ssw0rd");
    }
    SECTION("Decoding random string") {
        rc::check("Testing the decoding of random string",
        [](const std::string & str) {
            RC_PRE(str.length() > 0);
            std::string encoded_str = percent_encode(str);
            RC_PRE([encoded_str]() -> bool {
                std::string filter = "`~!@#$^&*(){}[]:\";'<>?,./\\-+=|";

                for (char c : encoded_str) {
                    if (filter.find(c) != std::string::npos) {
                        return false;
                    }
                }
                return true;
            }());
            char *buffer = (char *) malloc(encoded_str.length() + 1);
            strcpy(buffer, encoded_str.c_str());
            std::vector<char> encoded_str_tmp_vector{std::begin(encoded_str), std::end(encoded_str)};
            encoded_str_tmp_vector.push_back('\0');
            int len = esp_mqtt_decode_percent_encoded_string(encoded_str_tmp_vector.data());
            REQUIRE(len == str.length());
            std::string result{encoded_str_tmp_vector.data()};
            REQUIRE(result == str);
        });
    }
    SECTION("Decoding percent-encoding in URIs") {
        rc::check("Testing the decoding of random URI",
        [](const URI & uri) {
            RC_PRE(uri.host.value.length() > 0);
            RC_PRE(uri.username.value.length() > 0);
            RC_PRE(uri.password.value.length() > 0);
            std::string complete_uri_raw = uri.username.value + ":" + uri.password.value + "@" + uri.host.value;
            std::string complete_uri_enc = percent_encode(uri.username.value) + ":" + percent_encode(uri.password.value) + "@" + percent_encode(uri.host.value);
            // Verify that there are no prohibited characters in the encoded username
            RC_PRE([complete_uri_enc]() -> bool {
                // I have removed /, :, and @ as they are permitted symbols in URI
                std::string filter = "`~!#$^&*(){}[]\";'<>?,.\\-+=|";

                for (char c : complete_uri_enc) {
                    if (filter.find(c) != std::string::npos) {
                        std::cout << "Found '" << c << "' in \"" << complete_uri_enc << "\"" << std::endl;
                        return false;
                    }
                }
                return true;
            }());
            std::vector<char> complete_uri_tmp_vector{std::begin(complete_uri_enc), std::end(complete_uri_enc)};
            complete_uri_tmp_vector.push_back('\0');
            int len = esp_mqtt_decode_percent_encoded_string(complete_uri_tmp_vector.data());
            REQUIRE(len == complete_uri_raw.length());
            std::string result{complete_uri_tmp_vector.data()};
            REQUIRE(result == complete_uri_raw);
        });
    }
}
