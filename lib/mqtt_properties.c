/*
* Copyright (c) 2022, Lorenzo Consolaro. tiko Energy Solutions
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions
* are met:
*
* 1. Redistributions of source code must retain the above copyright
* notice, this list of conditions and the following disclaimer.
* 2. Redistributions in binary form must reproduce the above copyright
* notice, this list of conditions and the following disclaimer in the
* documentation and/or other materials provided with the distribution.
* 3. Neither the name of the copyright holder nor the names of its
* contributors may be used to endorse or promote products derived
* from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
* ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
* LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
* INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
* CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
* ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
* POSSIBILITY OF SUCH DAMAGE.
*
*/
#include "mqtt_properties.h"
#include "mqtt_msg.h"

#ifdef CONFIG_MQTT_PROTOCOL_50

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

struct nameToType
{
    mqtt_property_names_t name;
    mqtt_property_types_t type;
} namesToTypes[] =
{
  {PAYLOAD_FORMAT_INDICATOR, BYTE},
  {MESSAGE_EXPIRY_INTERVAL, FOUR_BYTE_INTEGER},
  {CONTENT_TYPE, UTF_8_ENCODED_STRING},
  {RESPONSE_TOPIC, UTF_8_ENCODED_STRING},
  {CORRELATION_DATA, BINARY_DATA},
  {SUBSCRIPTION_IDENTIFIER, VARIABLE_BYTE_INTEGER},
  {SESSION_EXPIRY_INTERVAL, FOUR_BYTE_INTEGER},
  {ASSIGNED_CLIENT_IDENTIFER, UTF_8_ENCODED_STRING},
  {SERVER_KEEP_ALIVE, TWO_BYTE_INTEGER},
  {AUTHENTICATION_METHOD, UTF_8_ENCODED_STRING},
  {AUTHENTICATION_DATA, BINARY_DATA},
  {REQUEST_PROBLEM_INFORMATION, BYTE},
  {WILL_DELAY_INTERVAL, FOUR_BYTE_INTEGER},
  {REQUEST_RESPONSE_INFORMATION, BYTE},
  {RESPONSE_INFORMATION, UTF_8_ENCODED_STRING},
  {SERVER_REFERENCE, UTF_8_ENCODED_STRING},
  {REASON_STRING, UTF_8_ENCODED_STRING},
  {RECEIVE_MAXIMUM, TWO_BYTE_INTEGER},
  {TOPIC_ALIAS_MAXIMUM, TWO_BYTE_INTEGER},
  {TOPIC_ALIAS, TWO_BYTE_INTEGER},
  {MAXIMUM_QOS, BYTE},
  {RETAIN_AVAILABLE, BYTE},
  {USER_PROPERTY, UTF_8_STRING_PAIR},
  {MAXIMUM_PACKET_SIZE, FOUR_BYTE_INTEGER},
  {WILDCARD_SUBSCRIPTION_AVAILABLE, BYTE},
  {SUBSCRIPTION_IDENTIFIER_AVAILABLE, BYTE},
  {SHARED_SUBSCRIPTION_AVAILABLE, BYTE}
};

int mqtt_property_get_type(int identifier)
{
    int i, rc = -1;

    for (i = 0; i < ARRAY_SIZE(namesToTypes); ++i) {
        if (namesToTypes[i].name == identifier) {
            rc = namesToTypes[i].type;
            break;
        }
    }
    return rc;
}

int mqtt_property_put_number(mqtt_properties_t* props, mqtt_property_names_t name, int value)
{
    int rc = 0;
    int type;
    if (props->count == CONFIG_MQTT_PROPERTIES_MAX)
        rc = -1;  /* max number of properties already in structure */
    else if ((type = mqtt_property_get_type(name)) < 0) {
        rc = -2;
    } else {
        int len = 0;

        props->array[props->count].identifier = (int)name;
        props->length++;
        
        switch (type) {
        case BYTE:
            len = 1;
            props->array[props->count].value.byte = (char)value;
            break;
        case TWO_BYTE_INTEGER:
            len = 2;
            props->array[props->count].value.integer2 = (uint16_t)value;
            break;
        case FOUR_BYTE_INTEGER:
            len = 4;
            props->array[props->count].value.integer4 = value;
            break;
        case VARIABLE_BYTE_INTEGER:
            if (value >= 0 && value <= 127)
                len = 1;
            else if (value >= 128 && value <= 16383)
                len = 2;
            else if (value >= 16384 && value < 2097151)
                len = 3;
            else if (value >= 2097152 && value < 268435455)
                len = 4;
            props->array[props->count].value.integer4 = value;
            break;
        case BINARY_DATA:
        case UTF_8_ENCODED_STRING:
        case UTF_8_STRING_PAIR:
            rc = -3;
            break;
        }
        props->count++;
        props->length += len;
    }
    return rc;
}

int mqtt_property_put_buffer(mqtt_properties_t* props, mqtt_property_names_t name, uint8_t* in_data, int in_data_len)
{
    int rc = ESP_OK;
    int type;
    if (props->count == CONFIG_MQTT_PROPERTIES_MAX)
        rc = -1;  /* max number of properties already in structure */
    else if ((type = mqtt_property_get_type(name)) < 0) {
        rc = -2;
    } else {
        int len = 0;

        props->array[props->count].identifier = (int)name;
        props->length++;
        
        switch (type) {
        case BYTE:
        case TWO_BYTE_INTEGER:
        case FOUR_BYTE_INTEGER:
        case VARIABLE_BYTE_INTEGER:
        case UTF_8_STRING_PAIR:
            rc = -3;
            break;
        case BINARY_DATA:
        case UTF_8_ENCODED_STRING:
            props->array[props->count].value.data.data = (char*)in_data;
            props->array[props->count].value.data.len = in_data_len;
            len = sizeof(uint16_t) + in_data_len;
            break;
        }
        props->count++;
        props->length += len;
    }
    return rc;
}

int mqtt_property_put_pair(mqtt_properties_t* props, mqtt_property_names_t name, const char* key, const char* data)
{
    int rc = 0;
    int type;
    if (props->count == CONFIG_MQTT_PROPERTIES_MAX)
        rc = -1;  /* max number of properties already in structure */
    else if ((type = mqtt_property_get_type(name)) < 0) {
        rc = -2;
    } else {
        int len = 0;

        props->array[props->count].identifier = (int)name;
        props->length++;
        
        switch (type) {
        case BYTE:
        case TWO_BYTE_INTEGER:
        case FOUR_BYTE_INTEGER:
        case VARIABLE_BYTE_INTEGER:
        case BINARY_DATA:
        case UTF_8_ENCODED_STRING:
            rc = -3;
            break;
        case UTF_8_STRING_PAIR:
            props->array[props->count].value.key.data = (char*)key;
            props->array[props->count].value.key.len = strlen(key);
            
            props->array[props->count].value.data.data = (char*)data;
            props->array[props->count].value.data.len = strlen(data);

            len = sizeof(uint16_t) + props->array[props->count].value.key.len;
            len += sizeof(uint16_t) + props->array[props->count].value.data.len;
            break;
        }
        props->count++;
        props->length += len;
    }
    return rc;
}


int mqtt_msg_write_property(uint8_t* buf, mqtt_property_t* prop)
{
    int type;
    int idx = 0;

    type = mqtt_property_get_type(prop->identifier);
    if (type >= BYTE && type <= UTF_8_STRING_PAIR) {
        idx += mqtt_msg_write_char(&buf[idx], prop->identifier);
        switch (type) {
        case BYTE:
            idx += mqtt_msg_write_char(&buf[idx], prop->value.byte);
            break;
        case TWO_BYTE_INTEGER:
            idx += mqtt_msg_write_int16(&buf[idx], prop->value.integer2);
            break;
        case FOUR_BYTE_INTEGER:
            idx += mqtt_msg_write_int32(&buf[idx], prop->value.integer4);
            break;
        case VARIABLE_BYTE_INTEGER:
            idx += mqtt_msg_encode_int(&buf[idx], prop->value.integer4);
            break;
        case BINARY_DATA:
        case UTF_8_ENCODED_STRING:
            idx += mqtt_msg_write_string(&buf[idx], prop->value.data.data, prop->value.data.len);
            break;
        case UTF_8_STRING_PAIR:
            idx += mqtt_msg_write_string(&buf[idx], prop->value.key.data, prop->value.key.len);
            idx += mqtt_msg_write_string(&buf[idx], prop->value.data.data, prop->value.data.len);
            break;
        }
    }
    return idx;
}

int mqtt_msg_write_properties(uint8_t* buf, mqtt_properties_t* properties)
{
    int i = 0;
    int idx = 0;

    // write the entire property list length first
    idx += mqtt_msg_encode_int(&buf[idx], properties->length);

    for (i = 0; i < properties->count; ++i) {
        idx += mqtt_msg_write_property(&buf[idx], &properties->array[i]);
    }

    return idx;
}

int mqtt_msg_read_property(uint8_t* buf, mqtt_property_t* prop)
{
    int type = -1;
    int idx = 0;

    idx += mqtt_msg_read_char(&buf[idx], (char*)&prop->identifier);
    type = mqtt_property_get_type(prop->identifier);
    if (type >= BYTE && type <= UTF_8_STRING_PAIR) {
        switch (type) {
        case BYTE:
            idx += mqtt_msg_read_char(&buf[idx], &prop->value.byte);
            break;
        case TWO_BYTE_INTEGER:
            idx += mqtt_msg_read_int16(&buf[idx], &prop->value.integer2);
            break;
        case FOUR_BYTE_INTEGER:
            idx += mqtt_msg_read_int32(&buf[idx], &prop->value.integer4);
            break;
        case VARIABLE_BYTE_INTEGER:
            idx += mqtt_msg_decode_int(&buf[idx], &prop->value.integer4);
            break;
        case BINARY_DATA:
        case UTF_8_ENCODED_STRING:
            idx += mqtt_msg_read_string(&buf[idx], &prop->value.data.data, &prop->value.data.len);
            break;
        case UTF_8_STRING_PAIR:
            idx += mqtt_msg_read_string(&buf[idx], &prop->value.key.data, &prop->value.key.len);
            idx += mqtt_msg_read_string(&buf[idx], &prop->value.data.data, &prop->value.data.len);
            break;
        }
    }
    return idx;
}

int mqtt_msg_read_properties(uint8_t* buf, int rem_len, mqtt_properties_t* out_properties)
{
    int idx = 0;
    int prop_len = 0;
    int tlen;

    out_properties->count = 0;

    if (rem_len > 0) {
        idx += mqtt_msg_decode_int(&buf[idx], &prop_len);

        out_properties->length = prop_len;
        while ((out_properties->count < CONFIG_MQTT_PROPERTIES_MAX) && prop_len > 0) {
            tlen = mqtt_msg_read_property(&buf[idx], &out_properties->array[out_properties->count]);
            prop_len -= tlen;
            idx += tlen;
            out_properties->count++;
        }
        // Check we read everything correctly
        if (prop_len != 0) {
            idx = -1;
        }
    }

    return idx;
}

int mqtt_property_get_number(mqtt_properties_t* props, mqtt_property_names_t name, int* out_count)
{
    int value = 0;
    mqtt_property_types_t type;
    int start_from = *out_count;    //Used to skip previously read values
    *out_count = 0;
    bool has_value = false;

    if (start_from >= props->count) {
        return 0;
    }

    for (int i = start_from; i < props->count; ++i) {
        if (name == props->array[i].identifier) {
            (*out_count)++;
            if (has_value) {
                // If we already have one stored value to return we are just counting
                continue;
            }
            // Get the value
            type = mqtt_property_get_type(props->array[i].identifier);
            switch (type) {
            case BYTE: value = props->array[i].value.byte; break;
            case TWO_BYTE_INTEGER: value = props->array[i].value.integer2; break;
            case FOUR_BYTE_INTEGER: value = props->array[i].value.integer4; break;
            case VARIABLE_BYTE_INTEGER: value = props->array[i].value.integer4; break;
            default:
                // Make the count fail
                *out_count = 0;
                break;
            }
            has_value = true;
        }
    }
    return value;
}

void mqtt_property_get_buffer(mqtt_properties_t* props, mqtt_property_names_t name, uint8_t** out_data, int* out_data_len, int* out_count)
{
    mqtt_property_types_t type;
    int start_from = *out_count;    //Used to skip previously read values
    *out_count = 0;
    bool has_value = false;

    if (start_from >= props->count) {
        return;
    }

    for (int i = start_from; i < props->count; ++i) {
        if (name == props->array[i].identifier) {
            (*out_count)++;
            if (has_value) {
                // If we already have one stored value to return we are just counting
                continue;
            }
            // Get the value
            type = mqtt_property_get_type(props->array[i].identifier);
            switch (type) {
            case BINARY_DATA:
            case UTF_8_ENCODED_STRING:
                *out_data = (uint8_t*)props->array[i].value.data.data;
                *out_data_len = props->array[i].value.data.len;
                break;
            default:
                // Make the count fail
                *out_count = 0;
                break;
            }
            has_value = true;
        }
    }
    return;
}

void mqtt_property_get_pair(mqtt_properties_t* props, mqtt_property_names_t name, char** out_key, int* out_key_len, char** out_data, int* out_data_len, int* out_count)
{
    mqtt_property_types_t type;
    int start_from = *out_count;    //Used to skip previously read values
    *out_count = 0;
    bool has_value = false;

    if (start_from >= props->count) {
        return;
    }

    for (int i = start_from; i < props->count; ++i) {
        if (name == props->array[i].identifier) {
            (*out_count)++;
            if (has_value) {
                // If we already have one stored value to return we are just counting
                continue;
            }
            // Get the value
            type = mqtt_property_get_type(props->array[i].identifier);
            switch (type) {
            case UTF_8_STRING_PAIR:
                *out_key = props->array[i].value.key.data;
                *out_key_len = props->array[i].value.key.len;
                *out_data = props->array[i].value.data.data;
                *out_data_len = props->array[i].value.data.len;
                break;
            default:
                // Make the count fail
                *out_count = 0;
                break;
            }
            has_value = true;
        }
    }
    return;
}

#endif
