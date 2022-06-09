/*
 * Copyright (c) ????-2022 - rightful owner
 * Contributors 2022, Lorenzo Consolaro. tiko Energy Solutions
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

#include "mqtt_client_internal.h"

#include "esp_event.h"
#include "esp_transport_tcp.h"
#include "esp_transport_ssl.h"
#include "esp_transport_ws.h"
#include "esp_log.h"
#include <errno.h>
#include <string.h>

#include "mqtt_supported_features.h"

/* using uri parser */
#include "http_parser.h"

#ifdef MQTT_DISABLE_API_LOCKS
# define MQTT_API_LOCK(c)
# define MQTT_API_UNLOCK(c)
#else
# define MQTT_API_LOCK(c)          xSemaphoreTakeRecursive(c->api_lock, portMAX_DELAY)
# define MQTT_API_UNLOCK(c)        xSemaphoreGiveRecursive(c->api_lock)
#endif /* MQTT_USE_API_LOCKS */

_Static_assert(sizeof(uint64_t) == sizeof(outbox_tick_t), "mqtt-client tick type size different from outbox tick type");
#ifdef ESP_EVENT_ANY_ID
_Static_assert(MQTT_EVENT_ANY == ESP_EVENT_ANY_ID, "mqtt-client event enum does not match the global EVENT_ANY_ID");
#endif

static const char *TAG = "mqtt_client";

#ifdef MQTT_SUPPORTED_FEATURE_EVENT_LOOP
/**
 * @brief Define of MQTT Event base
 *
 */
ESP_EVENT_DEFINE_BASE(MQTT_EVENTS);
#endif

#define MQTT_OVER_TCP_SCHEME "mqtt"
#define MQTT_OVER_SSL_SCHEME "mqtts"
#define MQTT_OVER_WS_SCHEME  "ws"
#define MQTT_OVER_WSS_SCHEME "wss"

const static int STOPPED_BIT = (1 << 0);
const static int RECONNECT_BIT = (1 << 1);
const static int DISCONNECT_BIT = (1 << 2);

static void esp_mqtt_destroy_config(esp_mqtt_client_handle_t client);
static esp_err_t esp_mqtt_connect(esp_mqtt_client_handle_t client, int timeout_ms);
static esp_err_t esp_mqtt_client_ping(esp_mqtt_client_handle_t client);
static char *create_string(const char *ptr, int len);
static int mqtt_message_receive(esp_mqtt_client_handle_t client, int read_poll_timeout_ms);
static void esp_mqtt_client_dispatch_transport_error(esp_mqtt_client_handle_t client);

#if MQTT_ENABLE_SSL
enum esp_mqtt_ssl_cert_key_api {
    MQTT_SSL_DATA_API_CA_CERT,
    MQTT_SSL_DATA_API_CLIENT_CERT,
    MQTT_SSL_DATA_API_CLIENT_KEY,
    MQTT_SSL_DATA_API_MAX,
};

static esp_err_t esp_mqtt_set_cert_key_data(esp_transport_handle_t ssl, enum esp_mqtt_ssl_cert_key_api what, const char *cert_key_data, int cert_key_len)
{
    char *data = (char *)cert_key_data;
    int ssl_transport_api_id = what;
    int len = cert_key_len;

    if (!data) {
        return ESP_OK;
    }

    if (len == 0) {
        // if length not specified, expect 0-terminated PEM string
        // and the original transport_api_id (by convention after the last api_id in the enum)
        ssl_transport_api_id += MQTT_SSL_DATA_API_MAX;
        len = strlen(data);
    }
#ifndef MQTT_SUPPORTED_FEATURE_DER_CERTIFICATES
    else {
        ESP_LOGE(TAG, "Explicit cert-/key-len is not available in IDF version %s", IDF_VER);
        return ESP_ERR_NOT_SUPPORTED;
    }
#endif

    // option to force the cert/key config to null (i.e. skip validation) when existing config updates
    if (0 == strcmp(data, "NULL")) {
        data = NULL;
        len = 0;
    }

    switch (ssl_transport_api_id) {
#ifdef MQTT_SUPPORTED_FEATURE_DER_CERTIFICATES
    case MQTT_SSL_DATA_API_CA_CERT:
        esp_transport_ssl_set_cert_data_der(ssl, data, len);
        break;
    case MQTT_SSL_DATA_API_CLIENT_CERT:
        esp_transport_ssl_set_client_cert_data_der(ssl, data, len);
        break;
    case MQTT_SSL_DATA_API_CLIENT_KEY:
        esp_transport_ssl_set_client_key_data_der(ssl, data, len);
        break;
#endif
    case MQTT_SSL_DATA_API_CA_CERT + MQTT_SSL_DATA_API_MAX:
        esp_transport_ssl_set_cert_data(ssl, data, len);
        break;
    case MQTT_SSL_DATA_API_CLIENT_CERT + MQTT_SSL_DATA_API_MAX:
        esp_transport_ssl_set_client_cert_data(ssl, data, len);
        break;
    case MQTT_SSL_DATA_API_CLIENT_KEY + MQTT_SSL_DATA_API_MAX:
        esp_transport_ssl_set_client_key_data(ssl, data, len);
        break;
    default:
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

static esp_err_t esp_mqtt_set_ssl_transport_properties(esp_transport_list_handle_t transport_list, mqtt_config_storage_t *cfg)
{
    esp_transport_handle_t ssl = esp_transport_list_get_transport(transport_list, MQTT_OVER_SSL_SCHEME);

    if (cfg->use_global_ca_store == true) {
        esp_transport_ssl_enable_global_ca_store(ssl);
    } else if (cfg->crt_bundle_attach != NULL) {
#ifdef MQTT_SUPPORTED_FEATURE_CERTIFICATE_BUNDLE
#ifdef CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
        esp_transport_ssl_crt_bundle_attach(ssl, cfg->crt_bundle_attach);
#else
        ESP_LOGE(TAG, "Certificate bundle is not enabled for mbedTLS in menuconfig");
        goto esp_mqtt_set_transport_failed;
#endif /* CONFIG_MBEDTLS_CERTIFICATE_BUNDLE */
#else
        ESP_LOGE(TAG, "Certificate bundle feature is not available in IDF version %s", IDF_VER);
        goto esp_mqtt_set_transport_failed;
#endif /* MQTT_SUPPORTED_FEATURE_CERTIFICATE_BUNDLE */
    } else {
        ESP_OK_CHECK(TAG, esp_mqtt_set_cert_key_data(ssl, MQTT_SSL_DATA_API_CA_CERT, cfg->cacert_buf, cfg->cacert_bytes),
                     goto esp_mqtt_set_transport_failed);

    }

    if (cfg->use_secure_element) {
#ifdef MQTT_SUPPORTED_FEATURE_SECURE_ELEMENT
#ifdef CONFIG_ESP_TLS_USE_SECURE_ELEMENT
        esp_transport_ssl_use_secure_element(ssl);
#else
        ESP_LOGE(TAG, "Secure element not enabled for esp-tls in menuconfig");
        goto esp_mqtt_set_transport_failed;
#endif /* CONFIG_ESP_TLS_USE_SECURE_ELEMENT */
#else
        ESP_LOGE(TAG, "Secure element feature is not available in IDF version %s", IDF_VER);
        goto esp_mqtt_set_transport_failed;
#endif /* MQTT_SUPPORTED_FEATURE_SECURE_ELEMENT */
    }

    if (cfg->ds_data != NULL) {
#ifdef MQTT_SUPPORTED_FEATURE_DIGITAL_SIGNATURE
#ifdef CONFIG_ESP_TLS_USE_DS_PERIPHERAL
        esp_transport_ssl_set_ds_data(ssl, cfg->ds_data);
#else
        ESP_LOGE(TAG, "Digital Signature not enabled for esp-tls in menuconfig");
        goto esp_mqtt_set_transport_failed;
#endif /* CONFIG_ESP_TLS_USE_DS_PERIPHERAL */
#else
        ESP_LOGE(TAG, "Digital Signature feature is not available in IDF version %s", IDF_VER);
        goto esp_mqtt_set_transport_failed;
#endif
    }
    ESP_OK_CHECK(TAG, esp_mqtt_set_cert_key_data(ssl, MQTT_SSL_DATA_API_CLIENT_CERT, cfg->clientcert_buf, cfg->clientcert_bytes),
                 goto esp_mqtt_set_transport_failed);
    ESP_OK_CHECK(TAG, esp_mqtt_set_cert_key_data(ssl, MQTT_SSL_DATA_API_CLIENT_KEY, cfg->clientkey_buf, cfg->clientkey_bytes),
                 goto esp_mqtt_set_transport_failed);

    if (cfg->clientkey_password && cfg->clientkey_password_len) {
#if defined(MQTT_SUPPORTED_FEATURE_CLIENT_KEY_PASSWORD) && MQTT_ENABLE_SSL
        esp_transport_ssl_set_client_key_password(ssl,
                cfg->clientkey_password,
                cfg->clientkey_password_len);
#else
        ESP_LOGE(TAG, "Password protected keys are not available in IDF version %s", IDF_VER);
        goto esp_mqtt_set_transport_failed;
#endif
    }

    if (cfg->psk_hint_key) {
#if defined(MQTT_SUPPORTED_FEATURE_PSK_AUTHENTICATION) && MQTT_ENABLE_SSL
#ifdef CONFIG_ESP_TLS_PSK_VERIFICATION
        esp_transport_ssl_set_psk_key_hint(ssl, cfg->psk_hint_key);
#else
        ESP_LOGE(TAG, "PSK authentication configured but not enabled in menuconfig: Please enable ESP_TLS_PSK_VERIFICATION option");
        goto esp_mqtt_set_transport_failed;
#endif
#else
        ESP_LOGE(TAG, "PSK authentication is not available in IDF version %s", IDF_VER);
        goto esp_mqtt_set_transport_failed;
#endif
    }

    if (cfg->alpn_protos) {
#if defined(MQTT_SUPPORTED_FEATURE_ALPN) && MQTT_ENABLE_SSL
#if defined(CONFIG_MBEDTLS_SSL_ALPN) || defined(CONFIG_WOLFSSL_HAVE_ALPN)
        esp_transport_ssl_set_alpn_protocol(ssl, (const char **)cfg->alpn_protos);
#else
        ESP_LOGE(TAG, "APLN configured but not enabled in menuconfig: Please enable MBEDTLS_SSL_ALPN or WOLFSSL_HAVE_ALPN option");
        goto esp_mqtt_set_transport_failed;
#endif
#else
        ESP_LOGE(TAG, "APLN is not available in IDF version %s", IDF_VER);
        goto esp_mqtt_set_transport_failed;
#endif
    }


    if (cfg->skip_cert_common_name_check) {
#if defined(MQTT_SUPPORTED_FEATURE_SKIP_CRT_CMN_NAME_CHECK) && MQTT_ENABLE_SSL
        esp_transport_ssl_skip_common_name_check(ssl);
#else
        ESP_LOGE(TAG, "Skip certificate common name check is not available in IDF version %s", IDF_VER);
        goto esp_mqtt_set_transport_failed;
#endif
    }

    return ESP_OK;

esp_mqtt_set_transport_failed:
    return ESP_FAIL;
}
#endif // MQTT_ENABLE_SSL

/* Checks if the user supplied config values are internally consistent */
static esp_err_t esp_mqtt_check_cfg_conflict(const mqtt_config_storage_t *cfg, const esp_mqtt_client_config_t *user_cfg)
{
    esp_err_t ret = ESP_OK;

    bool ssl_cfg_enabled = cfg->use_global_ca_store || cfg->cacert_buf || cfg->clientcert_buf || cfg->psk_hint_key || cfg->alpn_protos;
    bool is_ssl_scheme = false;
    if (cfg->scheme) {
        is_ssl_scheme = (strcasecmp(cfg->scheme, MQTT_OVER_SSL_SCHEME) == 0) || (strcasecmp(cfg->scheme, MQTT_OVER_WSS_SCHEME) == 0);
    }

    if (!is_ssl_scheme && ssl_cfg_enabled) {
        if (cfg->uri) {
            ESP_LOGW(TAG, "SSL related configs set, but the URI scheme specifies a non-SSL scheme, scheme = %s", cfg->scheme);
        } else {
            ESP_LOGW(TAG, "SSL related configs set, but the transport protocol is a non-SSL scheme, transport = %d", user_cfg->transport);
        }
        ret = ESP_ERR_INVALID_ARG;
    }

    if (cfg->uri && user_cfg->transport) {
        ESP_LOGW(TAG, "Transport config set, but overridden by scheme from URI: transport = %d, uri scheme = %s", user_cfg->transport, cfg->scheme);
        ret = ESP_ERR_INVALID_ARG;
    }

    return ret;
}

static bool set_if_config(char const *const new_config, char **old_config)
{
    if (new_config) {
        free(*old_config);
        *old_config = strdup(new_config);
        if (*old_config == NULL) {
            return false;
        }
    }
    return true;
}

static esp_err_t esp_mqtt_client_create_transport(esp_mqtt_client_handle_t client)
{
    esp_err_t ret = ESP_OK;
    if (client->transport_list) {
        esp_transport_list_destroy(client->transport_list);
        client->transport_list = NULL;
    }
    if (client->config->scheme) {
        client->transport_list = esp_transport_list_init();
        ESP_MEM_CHECK(TAG, client->transport_list, return ESP_ERR_NO_MEM);

        if ((strcasecmp(client->config->scheme, MQTT_OVER_TCP_SCHEME) == 0) || (strcasecmp(client->config->scheme, MQTT_OVER_WS_SCHEME) == 0)) {
            esp_transport_handle_t tcp = esp_transport_tcp_init();
            ESP_MEM_CHECK(TAG, tcp, return ESP_ERR_NO_MEM);
            esp_transport_set_default_port(tcp, MQTT_TCP_DEFAULT_PORT);
            esp_transport_list_add(client->transport_list, tcp, MQTT_OVER_TCP_SCHEME);
            if (strcasecmp(client->config->scheme, MQTT_OVER_WS_SCHEME) == 0) {
#if MQTT_ENABLE_WS
                esp_transport_handle_t ws = esp_transport_ws_init(tcp);
                ESP_MEM_CHECK(TAG, ws, return ESP_ERR_NO_MEM);
                esp_transport_set_default_port(ws, MQTT_WS_DEFAULT_PORT);
                if (client->config->path) {
                    esp_transport_ws_set_path(ws, client->config->path);
                }
#ifdef MQTT_SUPPORTED_FEATURE_WS_SUBPROTOCOL
                esp_transport_ws_set_subprotocol(ws, MQTT_OVER_TCP_SCHEME);
#endif
                esp_transport_list_add(client->transport_list, ws, MQTT_OVER_WS_SCHEME);
#else
                ESP_LOGE(TAG, "Please enable MQTT_ENABLE_WS to use %s", client->config->scheme);
                ret = ESP_FAIL;
#endif
            }
        } else if ((strcasecmp(client->config->scheme, MQTT_OVER_SSL_SCHEME) == 0) || (strcasecmp(client->config->scheme, MQTT_OVER_WSS_SCHEME) == 0)) {
#if MQTT_ENABLE_SSL
            esp_transport_handle_t ssl = esp_transport_ssl_init();
            ESP_MEM_CHECK(TAG, ssl, return ESP_ERR_NO_MEM);
            esp_transport_set_default_port(ssl, MQTT_SSL_DEFAULT_PORT);
            esp_transport_list_add(client->transport_list, ssl, MQTT_OVER_SSL_SCHEME);
            if (strcasecmp(client->config->scheme, MQTT_OVER_WSS_SCHEME) == 0) {
#if MQTT_ENABLE_WS
                esp_transport_handle_t wss = esp_transport_ws_init(ssl);
                ESP_MEM_CHECK(TAG, wss, return ESP_ERR_NO_MEM);
                esp_transport_set_default_port(wss, MQTT_WSS_DEFAULT_PORT);
                if (client->config->path) {
                    esp_transport_ws_set_path(wss, client->config->path);
                }
#ifdef MQTT_SUPPORTED_FEATURE_WS_SUBPROTOCOL
                esp_transport_ws_set_subprotocol(wss, MQTT_OVER_TCP_SCHEME);
#endif
                esp_transport_list_add(client->transport_list, wss, MQTT_OVER_WSS_SCHEME);
#else
                ESP_LOGE(TAG, "Please enable MQTT_ENABLE_WS to use %s", client->config->scheme);
                ret = ESP_FAIL;
#endif
            }
#else
            ESP_LOGE(TAG, "Please enable MQTT_ENABLE_SSL to use %s", client->config->scheme);
            ret = ESP_FAIL;
#endif
        } else {
            ESP_LOGE(TAG, "Not support this mqtt scheme %s", client->config->scheme);
            ret = ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "No scheme found");
        ret = ESP_FAIL;
    }
    return ret;
}

esp_err_t esp_mqtt_set_config(esp_mqtt_client_handle_t client, const esp_mqtt_client_config_t *config)
{
    if (!client) {
        ESP_LOGE(TAG, "Client was not initialized");
        return ESP_ERR_INVALID_ARG;
    }
    MQTT_API_LOCK(client);
    //Copy user configurations to client context
    esp_err_t err = ESP_OK;
    if (!client->config) {
        client->config = calloc(1, sizeof(mqtt_config_storage_t));
        ESP_MEM_CHECK(TAG, client->config, {
            MQTT_API_UNLOCK(client);
            return ESP_ERR_NO_MEM;
        });
    }

    client->config->message_retransmit_timeout = config->message_retransmit_timeout;
    if (config->message_retransmit_timeout <= 0) {
        client->config->message_retransmit_timeout = 1000;
    }

    client->config->task_prio = config->task_prio;
    if (client->config->task_prio <= 0) {
        client->config->task_prio = MQTT_TASK_PRIORITY;
    }

    client->config->task_stack = config->task_stack;
    if (client->config->task_stack <= 0) {
        client->config->task_stack = MQTT_TASK_STACK;
    }

    if (config->port) {
        client->config->port = config->port;
    }

    err = ESP_ERR_NO_MEM;
    ESP_MEM_CHECK(TAG, set_if_config(config->host, &client->config->host), goto _mqtt_set_config_failed);
    ESP_MEM_CHECK(TAG, set_if_config(config->path, &client->config->path), goto _mqtt_set_config_failed);
    ESP_MEM_CHECK(TAG, set_if_config(config->connect.username, &client->connect_info.username), goto _mqtt_set_config_failed);
    ESP_MEM_CHECK(TAG, set_if_config(config->connect.password, &client->connect_info.password), goto _mqtt_set_config_failed);

    if (!config->set_null_client_id) {
        if (config->connect.client_id) {
            ESP_MEM_CHECK(TAG, set_if_config(config->connect.client_id, &client->connect_info.client_id), goto _mqtt_set_config_failed);
        } else if (client->connect_info.client_id == NULL) {
            client->connect_info.client_id = platform_create_id_string();
        }
        ESP_MEM_CHECK(TAG, client->connect_info.client_id, goto _mqtt_set_config_failed);
        ESP_LOGD(TAG, "MQTT client_id=%s", client->connect_info.client_id);
    }

    ESP_MEM_CHECK(TAG, set_if_config(config->uri, &client->config->uri), goto _mqtt_set_config_failed);
    ESP_MEM_CHECK(TAG, set_if_config(config->connect.will.topic, &client->connect_info.will.topic), goto _mqtt_set_config_failed);

    if (config->connect.will.message_length && config->connect.will.message) {
        free(client->connect_info.will.message);
        client->connect_info.will.message = malloc(config->connect.will.message_length);
        ESP_MEM_CHECK(TAG, client->connect_info.will.message, goto _mqtt_set_config_failed);
        memcpy(client->connect_info.will.message, config->connect.will.message, config->connect.will.message_length);
        client->connect_info.will.message_length = config->connect.will.message_length;
    } else if (config->connect.will.message) {
        free(client->connect_info.will.message);
        client->connect_info.will.message = strdup(config->connect.will.message);
        ESP_MEM_CHECK(TAG, client->connect_info.will.message, goto _mqtt_set_config_failed);
        client->connect_info.will.message_length = strlen(config->connect.will.message);
    }
    if (config->connect.will.qos) {
        client->connect_info.will.qos = config->connect.will.qos;
    }
    if (config->connect.will.retain) {
        client->connect_info.will.retain = config->connect.will.retain;
    }

#ifdef CONFIG_MQTT_PROTOCOL_50
    client->config->send_will_on_disconnect = config->send_will_on_disconnect;

    client->connect_info.session_expiry_interval = config->connect.session_expiry_interval;
    client->connect_info.client_receive_maximum = config->connect.client_receive_maximum;
    client->connect_info.client_maximum_packet_size = config->connect.client_maximum_packet_size;
    client->connect_info.client_topic_alias_maximum = config->connect.client_topic_alias_maximum;
    client->connect_info.request_response_info = config->connect.request_response_info;
    client->connect_info.request_problem_info = config->connect.request_problem_info;

    for (int i = 0; i < config->connect.user_props_count; ++i) {
        if (client->connect_info.user_props[i].key) {
            free(client->connect_info.user_props[i].key);
            free(client->connect_info.user_props[i].value);
        }
        client->connect_info.user_props[i].key = strdup(config->connect.user_props[i].key);
        client->connect_info.user_props[i].value = strdup(config->connect.user_props[i].value);
        ESP_MEM_CHECK(TAG, client->connect_info.user_props[i].key, goto _mqtt_set_config_failed);
        ESP_MEM_CHECK(TAG, client->connect_info.user_props[i].value, goto _mqtt_set_config_failed);
    }
    client->connect_info.user_props_count = config->connect.user_props_count;

    if (config->connect.auth.method) {
        if (client->connect_info.auth.method) {
            free(client->connect_info.auth.method);
        }
        client->connect_info.auth.method = strdup(config->connect.auth.method);
        ESP_MEM_CHECK(TAG, client->connect_info.auth.method, goto _mqtt_set_config_failed);
    }

    if (config->connect.auth.data && config->connect.auth.data_len) {
        if (client->connect_info.auth.data) {
            free(client->connect_info.auth.data);
        }
        client->connect_info.auth.data = malloc(config->connect.auth.data_len);
        ESP_MEM_CHECK(TAG, client->connect_info.auth.data, goto _mqtt_set_config_failed);
        memcpy(client->connect_info.auth.data, config->connect.auth.data, config->connect.auth.data_len);
        client->connect_info.auth.data_len = config->connect.auth.data_len;
    }

    client->connect_info.will.delay_interval = config->connect.will.delay_interval;
    client->connect_info.will.payload_format_indicator = config->connect.will.payload_format_indicator;
    client->connect_info.will.message_expiry_interval = config->connect.will.message_expiry_interval;
    
    if (config->connect.will.content_type) {
        if (client->connect_info.will.content_type) {
            free(client->connect_info.will.content_type);
        }
        client->connect_info.will.content_type = strdup(config->connect.will.content_type);
        ESP_MEM_CHECK(TAG, client->connect_info.will.content_type, goto _mqtt_set_config_failed);
    }

    if (config->connect.will.response_topic) {
        if (client->connect_info.will.response_topic) {
            free(client->connect_info.will.response_topic);
        }
        client->connect_info.will.response_topic = strdup(config->connect.will.response_topic);
        ESP_MEM_CHECK(TAG, client->connect_info.will.response_topic, goto _mqtt_set_config_failed);
    }

    if (config->connect.will.correlation_data && config->connect.will.correlation_data_len) {
        if (client->connect_info.will.correlation_data) {
            free(client->connect_info.will.correlation_data);
        }
        client->connect_info.will.correlation_data = malloc(config->connect.will.correlation_data_len);
        ESP_MEM_CHECK(TAG, client->connect_info.will.correlation_data, goto _mqtt_set_config_failed);
        memcpy(client->connect_info.will.correlation_data, config->connect.will.correlation_data, config->connect.will.correlation_data_len);
        client->connect_info.will.correlation_data_len = config->connect.will.correlation_data_len;
    }

    for (int i = 0; i < config->connect.will.user_props_count; ++i) {
        if (client->connect_info.will.user_props[i].key) {
            free(client->connect_info.will.user_props[i].key);
            free(client->connect_info.will.user_props[i].value);
        }
        client->connect_info.will.user_props[i].key = strdup(config->connect.will.user_props[i].key);
        client->connect_info.will.user_props[i].value = strdup(config->connect.will.user_props[i].value);
        ESP_MEM_CHECK(TAG, client->connect_info.will.user_props[i].key, goto _mqtt_set_config_failed);
        ESP_MEM_CHECK(TAG, client->connect_info.will.user_props[i].value, goto _mqtt_set_config_failed);
    }
    client->connect_info.will.user_props_count = config->connect.will.user_props_count;
#endif

    if (config->disable_clean_session == client->connect_info.clean_session) {
        client->connect_info.clean_session = !config->disable_clean_session;
        if (!client->connect_info.clean_session && config->set_null_client_id) {
            ESP_LOGE(TAG, "Clean Session flag must be true if client has a null id");
        }
    }
    if (config->connect.keepalive) {
        client->connect_info.keepalive = config->connect.keepalive;
    }
    if (client->connect_info.keepalive == 0) {
        client->connect_info.keepalive = MQTT_KEEPALIVE_TICK;
    }
    if (config->disable_keepalive) {
        // internal `keepalive` value (in connect_info) is in line with 3.1.2.10 Keep Alive from mqtt spec:
        //      * keepalive=0: Keep alive mechanism disabled (server not to disconnect the client on its inactivity)
        //      * period in seconds to send a Control packet if inactive
        client->connect_info.keepalive = 0;
    }

    client->config->network_timeout_ms = config->network_timeout_ms;
    if (client->config->network_timeout_ms <= 0) {
        client->config->network_timeout_ms = MQTT_NETWORK_TIMEOUT_MS;
    }

    if (config->user_context) {
        client->config->user_context = config->user_context;
    }

    if (config->event_handle) {
        client->config->event_handle = config->event_handle;
    }

    if (config->refresh_connection_after_ms) {
        client->config->refresh_connection_after_ms = config->refresh_connection_after_ms;
    }

    client->config->auto_reconnect = true;
    if (config->disable_auto_reconnect == client->config->auto_reconnect) {
        client->config->auto_reconnect = !config->disable_auto_reconnect;
    }

    if (config->reconnect_timeout_ms) {
        client->config->reconnect_timeout_ms = config->reconnect_timeout_ms;
    } else {
        client->config->reconnect_timeout_ms = MQTT_RECON_DEFAULT_MS;
    }

    if (config->alpn_protos) {
        for (int i = 0; i < client->config->num_alpn_protos; i++) {
            free(client->config->alpn_protos[i]);
        }
        free(client->config->alpn_protos);
        client->config->num_alpn_protos = 0;

        const char **p;

        for (p = config->alpn_protos; *p != NULL; p++ ) {
            client->config->num_alpn_protos++;
        }
        // mbedTLS expects the list to be null-terminated
        client->config->alpn_protos = calloc(client->config->num_alpn_protos + 1, sizeof(config->alpn_protos));
        ESP_MEM_CHECK(TAG, client->config->alpn_protos, goto _mqtt_set_config_failed);

        for (int i = 0; i < client->config->num_alpn_protos; i++) {
            client->config->alpn_protos[i] = strdup(config->alpn_protos[i]);
            ESP_MEM_CHECK(TAG, client->config->alpn_protos[i], goto _mqtt_set_config_failed);
        }
    }

    // configure ssl related parameters
    client->config->use_global_ca_store = config->use_global_ca_store;
    client->config->cacert_buf = config->cert_pem;
    client->config->cacert_bytes = config->cert_len;
    client->config->clientcert_buf = config->client_cert_pem;
    client->config->clientcert_bytes = config->client_cert_len;
    client->config->clientkey_buf = config->client_key_pem;
    client->config->clientkey_bytes = config->client_key_len;
    client->config->psk_hint_key = config->psk_hint_key;
    client->config->skip_cert_common_name_check = config->skip_cert_common_name_check;
    client->config->use_secure_element = config->use_secure_element;
    client->config->crt_bundle_attach = config->crt_bundle_attach;
    client->config->ds_data = config->ds_data;

    if (config->clientkey_password && config->clientkey_password_len) {
        client->config->clientkey_password_len = config->clientkey_password_len;
        client->config->clientkey_password = malloc(client->config->clientkey_password_len);
        ESP_MEM_CHECK(TAG, client->config->clientkey_password, goto _mqtt_set_config_failed);
        memcpy(client->config->clientkey_password, config->clientkey_password, client->config->clientkey_password_len);
    }

    if (config->transport) {
        free(client->config->scheme);
        if (config->transport == MQTT_TRANSPORT_OVER_TCP) {
            client->config->scheme = create_string(MQTT_OVER_TCP_SCHEME, strlen(MQTT_OVER_TCP_SCHEME));
            ESP_MEM_CHECK(TAG, client->config->scheme, goto _mqtt_set_config_failed);
        }
#if MQTT_ENABLE_WS
        else if (config->transport == MQTT_TRANSPORT_OVER_WS) {
            client->config->scheme = create_string(MQTT_OVER_WS_SCHEME, strlen(MQTT_OVER_WS_SCHEME));
            ESP_MEM_CHECK(TAG, client->config->scheme, goto _mqtt_set_config_failed);
        }
#endif
#if MQTT_ENABLE_SSL
        else if (config->transport == MQTT_TRANSPORT_OVER_SSL) {
            client->config->scheme = create_string(MQTT_OVER_SSL_SCHEME, strlen(MQTT_OVER_SSL_SCHEME));
            ESP_MEM_CHECK(TAG, client->config->scheme, goto _mqtt_set_config_failed);
        }
#endif
#if MQTT_ENABLE_WSS
        else if (config->transport == MQTT_TRANSPORT_OVER_WSS) {
            client->config->scheme = create_string(MQTT_OVER_WSS_SCHEME, strlen(MQTT_OVER_WSS_SCHEME));
            ESP_MEM_CHECK(TAG, client->config->scheme, goto _mqtt_set_config_failed);
        }
#endif
    }

    // Set uri at the end of config to override separately configured uri elements
    if (config->uri) {
        if (esp_mqtt_client_set_uri(client, client->config->uri) != ESP_OK) {
            err = ESP_FAIL;
            goto _mqtt_set_config_failed;
        }
    }
    esp_err_t config_has_conflict = esp_mqtt_check_cfg_conflict(client->config, config);

    MQTT_API_UNLOCK(client);

    return config_has_conflict;
_mqtt_set_config_failed:
    esp_mqtt_destroy_config(client);
    MQTT_API_UNLOCK(client);
    return err;
}

static void esp_mqtt_destroy_config(esp_mqtt_client_handle_t client)
{
    if (client->config == NULL) {
        return;
    }
    free(client->config->host);
    free(client->config->uri);
    free(client->config->path);
    free(client->config->scheme);
    for (int i = 0; i < client->config->num_alpn_protos; i++) {
        free(client->config->alpn_protos[i]);
    }
    free(client->config->alpn_protos);
    free(client->config->clientkey_password);
    free(client->connect_info.will.topic);
    free(client->connect_info.will.message);
    free(client->connect_info.client_id);
    free(client->connect_info.username);
    free(client->connect_info.password);
#ifdef CONFIG_MQTT_PROTOCOL_50
    for (int i = 0; i < client->connect_info.user_props_count; ++i) {
        if (client->connect_info.user_props[i].key) {
            free(client->connect_info.user_props[i].key);
            free(client->connect_info.user_props[i].value);
        }
    }
    if (client->connect_info.auth.method) {
        free(client->connect_info.auth.method);
    }
    if (client->connect_info.auth.data) {
        free(client->connect_info.auth.data);
    }
    if (client->connect_info.will.content_type) {
        free(client->connect_info.will.content_type);
    }
    if (client->connect_info.will.response_topic) {
        free(client->connect_info.will.response_topic);
    }
    if (client->connect_info.will.correlation_data) {
        free(client->connect_info.will.correlation_data);
    }
    for (int i = 0; i < client->connect_info.will.user_props_count; ++i) {
        if (client->connect_info.will.user_props[i].key) {
            free(client->connect_info.will.user_props[i].key);
            free(client->connect_info.will.user_props[i].value);
        }
    }
#endif
    memset(&client->connect_info, 0, sizeof(mqtt_connect_info_t));
#ifdef MQTT_SUPPORTED_FEATURE_EVENT_LOOP
    if (client->config->event_loop_handle) {
        esp_event_loop_delete(client->config->event_loop_handle);
    }
#endif
    memset(client->config, 0, sizeof(mqtt_config_storage_t));
    free(client->config);
    client->config = NULL;
}

static esp_err_t process_keepalive(esp_mqtt_client_handle_t client)
{
    if (client->connect_info.keepalive > 0) {
        const int64_t time_since_last_keepalive = platform_tick_get_ms() - client->keepalive_tick;
        const int64_t keepalive_ms = client->connect_info.keepalive * 1000;

        if (client->wait_for_ping_resp == true ) {
            if ( time_since_last_keepalive > keepalive_ms) {
                ESP_LOGE(TAG, "No PING_RESP, disconnected");
                esp_mqtt_abort_connection(client);
                client->wait_for_ping_resp = false;
                return ESP_FAIL;
            }
            return ESP_OK;
        }

        if (time_since_last_keepalive > keepalive_ms / 2) {
            if (esp_mqtt_client_ping(client) == ESP_FAIL) {
                ESP_LOGE(TAG, "Can't send ping, disconnected");
                esp_mqtt_abort_connection(client);
                return ESP_FAIL;
            }
            client->wait_for_ping_resp = true;
            return ESP_OK;
        }
    }
    return ESP_OK;
}

static inline esp_err_t esp_mqtt_write(esp_mqtt_client_handle_t client)
{
    int wlen = 0, widx = 0, len = client->mqtt_state.outbound_message->length;
    while (len > 0) {
        wlen = esp_transport_write(client->transport,
                                   (char *)client->mqtt_state.outbound_message->data + widx,
                                   len,
                                   client->config->network_timeout_ms);
        if (wlen < 0) {
            ESP_LOGE(TAG, "Writing failed: errno=%d", errno);
            return ESP_FAIL;
        } else if (wlen == 0) {
            ESP_LOGE(TAG, "Writing didn't complete in specified timeout: errno=%d", errno);
            return ESP_ERR_TIMEOUT;
        }
        widx += wlen;
        len -= wlen;
    }
    return ESP_OK;
}

static esp_err_t esp_mqtt_connect(esp_mqtt_client_handle_t client, int timeout_ms)
{
    esp_err_t err;
    int read_len;
    client->wait_for_ping_resp = false;
    client->mqtt_state.outbound_message = mqtt_msg_connect(&client->mqtt_state.mqtt_connection,
                                          &client->connect_info);
    if (client->mqtt_state.outbound_message->length == 0) {
        ESP_LOGE(TAG, "Connect message cannot be created");
        return ESP_FAIL;
    }

    client->mqtt_state.pending_msg_type = mqtt_get_type(client->mqtt_state.outbound_message->data);
    client->mqtt_state.pending_msg_id = 0;
    ESP_LOGD(TAG, "Sending MQTT CONNECT message, type: %d, id: %04X",
             client->mqtt_state.pending_msg_type,
             client->mqtt_state.pending_msg_id);

    if (esp_mqtt_write(client) != ESP_OK) {
        return ESP_FAIL;
    }

    client->mqtt_state.in_buffer_read_len = 0;
    client->mqtt_state.message_length = 0;

    /* wait configured network timeout for broker connection response */
    uint64_t connack_recv_started = platform_tick_get_ms();
    do {
        read_len = mqtt_message_receive(client, client->config->network_timeout_ms);
    } while (read_len == 0 && platform_tick_get_ms() - connack_recv_started < client->config->network_timeout_ms);

    if (read_len <= 0) {
        ESP_LOGE(TAG, "%s: mqtt_message_receive() returned %d", __func__, read_len);
        return ESP_FAIL;
    }

    // Clear the event struct
    memset(&client->event, 0, sizeof(esp_mqtt_event_t));

    err = handle_connack(client, read_len);
    return err;
}

void esp_mqtt_abort_connection(esp_mqtt_client_handle_t client)
{
    MQTT_API_LOCK(client);
    esp_transport_close(client->transport);
    client->wait_timeout_ms = client->config->reconnect_timeout_ms;
    client->reconnect_tick = platform_tick_get_ms();
    client->state = MQTT_STATE_WAIT_RECONNECT;
    ESP_LOGD(TAG, "Reconnect after %d ms", client->wait_timeout_ms);
    client->event.event_id = MQTT_EVENT_DISCONNECTED;
    client->wait_for_ping_resp = false;

    esp_mqtt_dispatch_event(client);
    MQTT_API_UNLOCK(client);
}

static bool create_client_data(esp_mqtt_client_handle_t client)
{
    client->api_lock = xSemaphoreCreateRecursiveMutex();
    ESP_MEM_CHECK(TAG, client->api_lock, return false);

    return true;
}

esp_mqtt_client_handle_t esp_mqtt_client_init(const esp_mqtt_client_config_t *config)
{
    esp_mqtt_client_handle_t client = calloc(1, sizeof(struct esp_mqtt_client));
    ESP_MEM_CHECK(TAG, client, return NULL);
    if (!create_client_data(client)) {
        goto _mqtt_init_failed;
    }

    if (esp_mqtt_set_config(client, config) != ESP_OK) {
        goto _mqtt_init_failed;
    }
#ifdef MQTT_SUPPORTED_FEATURE_EVENT_LOOP
    esp_event_loop_args_t no_task_loop = {
        .queue_size = 1,
        .task_name = NULL,
    };
    esp_event_loop_create(&no_task_loop, &client->config->event_loop_handle);
#endif

    client->keepalive_tick = platform_tick_get_ms();
    client->reconnect_tick = platform_tick_get_ms();
    client->refresh_connection_tick = platform_tick_get_ms();
    client->wait_for_ping_resp = false;
    int buffer_size = config->buffer_size;
    if (buffer_size <= 0) {
        buffer_size = MQTT_BUFFER_SIZE_BYTE;
    }
    // use separate value for output buffer size if configured
    int out_buffer_size = config->out_buffer_size > 0 ? config->out_buffer_size : buffer_size;

    client->mqtt_state.in_buffer = (uint8_t *)malloc(buffer_size);
    ESP_MEM_CHECK(TAG, client->mqtt_state.in_buffer, goto _mqtt_init_failed);
    client->mqtt_state.in_buffer_length = buffer_size;
    client->mqtt_state.out_buffer = (uint8_t *)malloc(out_buffer_size);
    ESP_MEM_CHECK(TAG, client->mqtt_state.out_buffer, goto _mqtt_init_failed);

    client->mqtt_state.out_buffer_length = out_buffer_size;
    client->outbox = outbox_init();
    ESP_MEM_CHECK(TAG, client->outbox, goto _mqtt_init_failed);
    client->status_bits = xEventGroupCreate();
    ESP_MEM_CHECK(TAG, client->status_bits, goto _mqtt_init_failed);

    mqtt_msg_init(&client->mqtt_state.mqtt_connection, client->mqtt_state.out_buffer,
                  client->mqtt_state.out_buffer_length);
    return client;
_mqtt_init_failed:
    esp_mqtt_client_destroy(client);
    return NULL;
}

esp_err_t esp_mqtt_client_destroy(esp_mqtt_client_handle_t client)
{
    if (client == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (client->api_lock) {
        esp_mqtt_client_stop(client);
    }
    esp_mqtt_destroy_config(client);
    if (client->transport_list) {
        esp_transport_list_destroy(client->transport_list);
    }
    if (client->outbox) {
        outbox_destroy(client->outbox);
    }
    if (client->status_bits) {
        vEventGroupDelete(client->status_bits);
    }
    free(client->mqtt_state.in_buffer);
    free(client->mqtt_state.out_buffer);
    if (client->api_lock) {
        vSemaphoreDelete(client->api_lock);
    }
    free(client);
    return ESP_OK;
}

static char *create_string(const char *ptr, int len)
{
    char *ret;
    if (len <= 0) {
        return NULL;
    }
    ret = calloc(1, len + 1);
    ESP_MEM_CHECK(TAG, ret, return NULL);
    memcpy(ret, ptr, len);
    return ret;
}

esp_err_t esp_mqtt_client_set_uri(esp_mqtt_client_handle_t client, const char *uri)
{
    struct http_parser_url puri;
    http_parser_url_init(&puri);
    int parser_status = http_parser_parse_url(uri, strlen(uri), 0, &puri);
    if (parser_status != 0) {
        ESP_LOGE(TAG, "Error parse uri = %s", uri);
        return ESP_FAIL;
    }

    // This API could be also executed when client is active (need to protect config fields)
    MQTT_API_LOCK(client);
    // set uri overrides actual scheme, host, path if configured previously
    free(client->config->scheme);
    free(client->config->host);
    free(client->config->path);

    client->config->scheme = create_string(uri + puri.field_data[UF_SCHEMA].off, puri.field_data[UF_SCHEMA].len);
    client->config->host = create_string(uri + puri.field_data[UF_HOST].off, puri.field_data[UF_HOST].len);
    client->config->path = NULL;

    if (puri.field_data[UF_PATH].len || puri.field_data[UF_QUERY].len) {
        if (puri.field_data[UF_QUERY].len == 0) {
            asprintf(&client->config->path, "%.*s", puri.field_data[UF_PATH].len, uri + puri.field_data[UF_PATH].off);
        } else if (puri.field_data[UF_PATH].len == 0)  {
            asprintf(&client->config->path, "/?%.*s", puri.field_data[UF_QUERY].len, uri + puri.field_data[UF_QUERY].off);
        } else {
            asprintf(&client->config->path, "%.*s?%.*s", puri.field_data[UF_PATH].len, uri + puri.field_data[UF_PATH].off,
                     puri.field_data[UF_QUERY].len, uri + puri.field_data[UF_QUERY].off);
        }
        ESP_MEM_CHECK(TAG, client->config->path, {
            MQTT_API_UNLOCK(client);
            return ESP_ERR_NO_MEM;
        });
    }

    if (puri.field_data[UF_PORT].len) {
        client->config->port = strtol((const char *)(uri + puri.field_data[UF_PORT].off), NULL, 10);
    }

    char *user_info = create_string(uri + puri.field_data[UF_USERINFO].off, puri.field_data[UF_USERINFO].len);
    if (user_info) {
        char *pass = strchr(user_info, ':');
        if (pass) {
            pass[0] = 0; //terminal username
            pass ++;
            client->connect_info.password = strdup(pass);
        }
        client->connect_info.username = strdup(user_info);

        free(user_info);
    }

    MQTT_API_UNLOCK(client);
    return ESP_OK;
}

esp_err_t mqtt_write_data(esp_mqtt_client_handle_t client)
{
    if (esp_mqtt_write(client) != ESP_OK) {
        esp_mqtt_client_dispatch_transport_error(client);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t esp_mqtt_dispatch_event(esp_mqtt_client_handle_t client)
{
    client->event.user_context = client->config->user_context;
    client->event.client = client;

    if (client->config->event_handle) {
        return client->config->event_handle(&client->event);
    } else {
#ifdef MQTT_SUPPORTED_FEATURE_EVENT_LOOP
        esp_event_post_to(client->config->event_loop_handle, MQTT_EVENTS, client->event.event_id, &client->event, sizeof(client->event), portMAX_DELAY);
        return esp_event_loop_run(client->config->event_loop_handle, 0);
#else
        return ESP_FAIL;
#endif
    }
}

static outbox_item_handle_t mqtt_enqueue_oversized(esp_mqtt_client_handle_t client, uint8_t *remaining_data, int remaining_len)
{
    ESP_LOGD(TAG, "mqtt_enqueue_oversized id: %d, type=%d successful",
             client->mqtt_state.pending_msg_id, client->mqtt_state.pending_msg_type);
    //lock mutex
    outbox_message_t msg = { 0 };
    msg.data = client->mqtt_state.outbound_message->data;
    msg.len =  client->mqtt_state.outbound_message->length;
    msg.msg_id = client->mqtt_state.pending_msg_id;
    msg.msg_type = client->mqtt_state.pending_msg_type;
    msg.msg_qos = client->mqtt_state.pending_publish_qos;
    msg.remaining_data = remaining_data;
    msg.remaining_len = remaining_len;
    //Copy to queue buffer
    return outbox_enqueue(client->outbox, &msg, platform_tick_get_ms());
    //unlock
}

static outbox_item_handle_t mqtt_enqueue(esp_mqtt_client_handle_t client)
{
    ESP_LOGD(TAG, "mqtt_enqueue id: %d, type=%d successful",
             client->mqtt_state.pending_msg_id, client->mqtt_state.pending_msg_type);
    if (client->mqtt_state.pending_msg_count > 0) {
        outbox_message_t msg = { 0 };
        msg.data = client->mqtt_state.outbound_message->data;
        msg.len =  client->mqtt_state.outbound_message->length;
        msg.msg_id = client->mqtt_state.pending_msg_id;
        msg.msg_type = client->mqtt_state.pending_msg_type;
        msg.msg_qos = client->mqtt_state.pending_publish_qos;
        //Copy to queue buffer
        return outbox_enqueue(client->outbox, &msg, platform_tick_get_ms());
    }
    return NULL;
}


/*
 * Returns:
 *     -1 in case of failure
 *      0 if no message has been received
 *      1 if a message has been received and placed to client->mqtt_state:
 *           message length:  client->mqtt_state.message_length
 *           message content: client->mqtt_state.in_buffer
 */
static int mqtt_message_receive(esp_mqtt_client_handle_t client, int read_poll_timeout_ms)
{
    int read_len, total_len, fixed_header_len;
    uint8_t *buf = client->mqtt_state.in_buffer + client->mqtt_state.in_buffer_read_len;
    esp_transport_handle_t t = client->transport;

    client->mqtt_state.message_length = 0;
    if (client->mqtt_state.in_buffer_read_len == 0) {
        /*
         * Read first byte of the mqtt packet fixed header, it contains packet
         * type and flags.
         */
        read_len = esp_transport_read(t, (char *)buf, 1, read_poll_timeout_ms);
        if (read_len < 0) {
            ESP_LOGE(TAG, "%s: transport_read() error: errno=%d", __func__, errno);
            goto err;
        }
        if (read_len == 0) {
            ESP_LOGV(TAG, "%s: transport_read(): no data or EOF", __func__);
            return 0;
        }
        ESP_LOGD(TAG, "%s: first byte: 0x%x", __func__, *buf);
        /*
         * Verify the flags and act according to MQTT protocol: close connection
         * if the flags are set incorrectly.
         */
        if (!mqtt_has_valid_msg_hdr(buf, read_len)) {
            ESP_LOGE(TAG, "%s: received a message with an invalid header=0x%x", __func__, *buf);
            goto err;
        }
        buf++;
        client->mqtt_state.in_buffer_read_len++;
    }
    if ((client->mqtt_state.in_buffer_read_len == 1) ||
            ((client->mqtt_state.in_buffer_read_len < 6) && (*(buf - 1) & 0x80))) {
        do {
            /*
             * Read the "remaining length" part of mqtt packet fixed header.  It
             * starts at second byte and spans up to 4 bytes, but we accept here
             * only up to 2 bytes of remaining length, i.e. messages with
             * maximal remaining length value = 16383 (maximal total message
             * size of 16386 bytes).
             */
            read_len = esp_transport_read(t, (char *)buf, 1, read_poll_timeout_ms);
            if (read_len < 0) {
                ESP_LOGE(TAG, "%s: transport_read() error: errno=%d", __func__, errno);
                goto err;
            }
            if (read_len == 0) {
                ESP_LOGD(TAG, "%s: transport_read(): no data or EOF", __func__);
                return 0;
            }
            ESP_LOGD(TAG, "%s: read \"remaining length\" byte: 0x%x", __func__, *buf);
            buf++;
            client->mqtt_state.in_buffer_read_len++;
        } while ((client->mqtt_state.in_buffer_read_len < 6) && (*(buf - 1) & 0x80));
    }
    total_len = mqtt_get_total_length(client->mqtt_state.in_buffer, client->mqtt_state.in_buffer_read_len, &fixed_header_len);
    ESP_LOGD(TAG, "%s: total message length: %d (already read: %zu)", __func__, total_len, client->mqtt_state.in_buffer_read_len);
    client->mqtt_state.message_length = total_len;
    if (client->mqtt_state.in_buffer_length < total_len) {
        if (mqtt_get_type(client->mqtt_state.in_buffer) == MQTT_MSG_TYPE_PUBLISH) {
            /*
             * In case larger publish messages, we only need to read full topic, data can be split to multiple data event.
             * Evaluate and correct total_len to read only publish message header, so data can be read separately
             */
            if (client->mqtt_state.in_buffer_read_len < fixed_header_len + 2) {
                /* read next 2 bytes - topic length to get minimum portion of publish packet */
                read_len = esp_transport_read(t, (char *)buf, client->mqtt_state.in_buffer_read_len - fixed_header_len + 2, read_poll_timeout_ms);
                ESP_LOGD(TAG, "%s: read_len=%d", __func__, read_len);
                if (read_len < 0) {
                    ESP_LOGE(TAG, "%s: transport_read() error: errno=%d", __func__, errno);
                    goto err;
                } else if (read_len == 0) {
                    ESP_LOGD(TAG, "%s: transport_read(): no data or EOF", __func__);
                    return 0;
                }
                client->mqtt_state.in_buffer_read_len += read_len;
                buf += read_len;
                if (client->mqtt_state.in_buffer_read_len < fixed_header_len + 2) {
                    ESP_LOGD(TAG, "%s: transport_read(): message reading left in progress :: total message length: %d (already read: %zu)",
                             __func__, total_len, client->mqtt_state.in_buffer_read_len);
                    return 0;
                }
            }
            int topic_len = client->mqtt_state.in_buffer[fixed_header_len] << 8;
            topic_len |= client->mqtt_state.in_buffer[fixed_header_len + 1];
            // Compute len of the remaining part of the packet without data
            total_len = fixed_header_len + topic_len + (mqtt_get_qos(client->mqtt_state.in_buffer) > 0 ? 2 : 0);
            ESP_LOGD(TAG, "%s: total len modified to %d as message longer than input buffer", __func__, total_len);
            // Check if the packet, without data, fits in the fixed buffer
            if (client->mqtt_state.in_buffer_length < total_len) {
                ESP_LOGE(TAG, "%s: message is too big, insufficient buffer size", __func__);
                goto err;
            } else {
                // Set total len to the maximum fixed buffer size
                total_len = client->mqtt_state.in_buffer_length;
            }
            /* free to continue with reading */
        } else {
            ESP_LOGE(TAG, "%s: message is too big, insufficient buffer size", __func__);
            goto err;
        }
    }
    if (client->mqtt_state.in_buffer_read_len < total_len) {
        /* read the rest of the mqtt message, up to the fixed size */
        read_len = esp_transport_read(t, (char *)buf, total_len - client->mqtt_state.in_buffer_read_len, read_poll_timeout_ms);
        ESP_LOGD(TAG, "%s: read_len=%d", __func__, read_len);
        if (read_len < 0) {
            ESP_LOGE(TAG, "%s: transport_read() error: errno=%d", __func__, errno);
            goto err;
        }
        if (read_len == 0) {
            ESP_LOGD(TAG, "%s: transport_read(): no data or EOF", __func__);
            return 0;
        }
        client->mqtt_state.in_buffer_read_len += read_len;
        if (client->mqtt_state.in_buffer_read_len < total_len) {
            // Didn't manage to read all the message
            ESP_LOGD(TAG, "%s: transport_read(): message reading left in progress :: total message length: %d (already read: %zu)",
                     __func__, total_len, client->mqtt_state.in_buffer_read_len);
            return 0;
        }
    }
    ESP_LOGD(TAG, "%s: transport_read():%zu %zu", __func__, client->mqtt_state.in_buffer_read_len, client->mqtt_state.message_length);
    return 1;
err:
    esp_mqtt_client_dispatch_transport_error(client);
    return -1;
}

static esp_err_t mqtt_process_receive(esp_mqtt_client_handle_t client)
{
    uint8_t  msg_type;

    /* non-blocking receive in order not to block other tasks */
    int recv = mqtt_message_receive(client, 0);
    if (recv < 0) {
        ESP_LOGE(TAG, "%s: mqtt_message_receive() returned %d", __func__, recv);
        return ESP_FAIL;
    }
    if (recv == 0) {
        // No message
        return ESP_OK;
    }
    uint8_t * recv_buf = client->mqtt_state.in_buffer; 
    int recv_len = client->mqtt_state.message_length;
    esp_err_t err;

    // Clear the event struct
    memset(&client->event, 0, sizeof(esp_mqtt_event_t));

    // If the message was valid, get the type, quality of service and id of the message
    msg_type = mqtt_get_type(recv_buf);

    ESP_LOGD(TAG, "msg_type=%d", msg_type);

    switch (msg_type) {
    case MQTT_MSG_TYPE_SUBACK:
    {
        err = handle_suback(client, recv_buf, recv_len);
        break;
    }
    case MQTT_MSG_TYPE_UNSUBACK:
    {
        err = handle_unsuback(client, recv_buf, recv_len);
        break;
    }
    case MQTT_MSG_TYPE_PUBLISH:
    {
        err = handle_publish(client);
        break;
    }
    case MQTT_MSG_TYPE_PUBACK:
    {
        err = handle_puback(client, recv_buf, recv_len);
        break;
    }
    case MQTT_MSG_TYPE_PUBREC:
    {
        err = handle_pubrec(client, recv_buf, recv_len);
        break;
    }
    case MQTT_MSG_TYPE_PUBREL:
    {
        err = handle_pubrel(client, recv_buf, recv_len);
        break;
    }
    case MQTT_MSG_TYPE_PUBCOMP:
    {
        err = handle_pubcomp(client, recv_buf, recv_len);
        break;
    }
    case MQTT_MSG_TYPE_PINGRESP:
    {
        ESP_LOGD(TAG, "MQTT_MSG_TYPE_PINGRESP");
        client->wait_for_ping_resp = false;
        /* It is the responsibility of the Client to ensure that the interval between Control Packets
         * being sent does not exceed the Keep Alive value. In the absence of sending any other Control
         * Packets, the Client MUST send a PINGREQ Packet [MQTT-3.1.2-23].
         * [MQTT-3.1.2-23]
         */
        client->keepalive_tick = platform_tick_get_ms();
        err = ESP_OK;
        break;
    }
    default:
        ESP_LOGW(TAG, "Invalid MQTT type received <%d>", msg_type);
        err = ESP_OK; // Do not disconnect
        break;
    }

    client->mqtt_state.in_buffer_read_len = 0;
    return err;
}

static esp_err_t mqtt_resend_queued(esp_mqtt_client_handle_t client, outbox_item_handle_t item)
{
    // decode queued data
    client->mqtt_state.outbound_message->data = outbox_item_get_data(item, &client->mqtt_state.outbound_message->length, &client->mqtt_state.pending_msg_id,
            &client->mqtt_state.pending_msg_type, &client->mqtt_state.pending_publish_qos);
    // set duplicate flag for QoS-1 and QoS-2 messages
    if (client->mqtt_state.pending_msg_type == MQTT_MSG_TYPE_PUBLISH && client->mqtt_state.pending_publish_qos > 0 && (outbox_item_get_pending(item) == TRANSMITTED)) {
        mqtt_set_dup(client->mqtt_state.outbound_message->data);
    }

    // try to resend the data
    if (mqtt_write_data(client) != ESP_OK) {
        ESP_LOGE(TAG, "Error to resend data ");
        esp_mqtt_abort_connection(client);
        return ESP_FAIL;
    }

    // check if it was QoS-0 publish message
    if (client->mqtt_state.pending_msg_type == MQTT_MSG_TYPE_PUBLISH && client->mqtt_state.pending_publish_qos == 0) {
        // delete all qos0 publish messages once we process them
        if (outbox_delete_item(client->outbox, item) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to remove queued qos0 message from the outbox");
        }
    }
    return ESP_OK;
}

static void mqtt_delete_expired_messages(esp_mqtt_client_handle_t client)
{
    // Delete message after OUTBOX_EXPIRED_TIMEOUT_MS milliseconds
#if MQTT_REPORT_DELETED_MESSAGES
    // also report the deleted items as MQTT_EVENT_DELETED events if enabled
    int deleted_items = 0;
    int msg_id = 0;
    while ((msg_id = outbox_delete_single_expired(client->outbox, platform_tick_get_ms(), OUTBOX_EXPIRED_TIMEOUT_MS)) > 0) {
        client->event.event_id = MQTT_EVENT_DELETED;
        client->event.msg_id = msg_id;
        if (esp_mqtt_dispatch_event(client) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to post event on deleting message id=%d", msg_id);
        }
        deleted_items ++;
    }
#else
    int deleted_items = outbox_delete_expired(client->outbox, platform_tick_get_ms(), OUTBOX_EXPIRED_TIMEOUT_MS);
#endif
    client->mqtt_state.pending_msg_count -= deleted_items;

    if (client->mqtt_state.pending_msg_count < 0) {
        client->mqtt_state.pending_msg_count = 0;
    }
}

static void esp_mqtt_task(void *pv)
{
    esp_mqtt_client_handle_t client = (esp_mqtt_client_handle_t) pv;
    uint64_t last_retransmit = 0;
    outbox_tick_t msg_tick = 0;
    client->run = true;

    //get transport by scheme
    client->transport = esp_transport_list_get_transport(client->transport_list, client->config->scheme);

    if (client->transport == NULL) {
        ESP_LOGE(TAG, "There are no transports valid, stop mqtt client, config scheme = %s", client->config->scheme);
        client->run = false;
    }
    //default port
    if (client->config->port == 0) {
        client->config->port = esp_transport_get_default_port(client->transport);
    }

    client->state = MQTT_STATE_INIT;
    xEventGroupClearBits(client->status_bits, STOPPED_BIT);
    while (client->run) {
        MQTT_API_LOCK(client);
        switch (client->state) {
        case MQTT_STATE_DISCONNECTED:
            break;
        case MQTT_STATE_INIT:
            xEventGroupClearBits(client->status_bits, RECONNECT_BIT | DISCONNECT_BIT);
            client->event.event_id = MQTT_EVENT_BEFORE_CONNECT;
            esp_mqtt_dispatch_event(client);

            if (client->transport == NULL) {
                ESP_LOGE(TAG, "There is no transport");
                client->run = false;
            }
#if MQTT_ENABLE_SSL
            esp_mqtt_set_ssl_transport_properties(client->transport_list, client->config);
#endif

            if (esp_transport_connect(client->transport,
                                      client->config->host,
                                      client->config->port,
                                      client->config->network_timeout_ms) < 0) {
                ESP_LOGE(TAG, "Error transport connect");
                esp_mqtt_client_dispatch_transport_error(client);
                esp_mqtt_abort_connection(client);
                break;
            }
            ESP_LOGD(TAG, "Transport connected to %s://%s:%d", client->config->scheme, client->config->host, client->config->port);

            if (esp_mqtt_connect(client, client->config->network_timeout_ms) != ESP_OK) {
                ESP_LOGE(TAG, "MQTT connect failed");
                esp_mqtt_abort_connection(client);
                break;
            }

            break;
        case MQTT_STATE_CONNECTED:
            // check for disconnection request
            if (xEventGroupWaitBits(client->status_bits, DISCONNECT_BIT, true, true, 0) & DISCONNECT_BIT) {
#ifdef CONFIG_MQTT_PROTOCOL_50
                send_disconnect_msg(client, SUCCESS);
#else
                send_disconnect_msg(client);    // ignore error, if clean disconnect fails, just abort the connection
#endif
                esp_mqtt_abort_connection(client);
                break;
            }
            // receive and process data
            if (mqtt_process_receive(client) == ESP_FAIL) {
                esp_mqtt_abort_connection(client);
                break;
            }

            // delete long pending messages
            mqtt_delete_expired_messages(client);

            // resend all non-transmitted messages first
            outbox_item_handle_t item = outbox_dequeue(client->outbox, QUEUED, NULL);
            if (item) {
                if (mqtt_resend_queued(client, item) == ESP_OK) {
                    outbox_set_pending(client->outbox, client->mqtt_state.pending_msg_id, TRANSMITTED);
                }
                // resend other "transmitted" messages after 1s
            } else if (platform_tick_get_ms() - last_retransmit > client->config->message_retransmit_timeout) {
                last_retransmit = platform_tick_get_ms();
                item = outbox_dequeue(client->outbox, TRANSMITTED, &msg_tick);
                if (item && (last_retransmit - msg_tick > client->config->message_retransmit_timeout))  {
                    mqtt_resend_queued(client, item);
                }
            }

            if (process_keepalive(client) != ESP_OK) {
                break;
            }

            if (client->config->refresh_connection_after_ms &&
                    platform_tick_get_ms() - client->refresh_connection_tick > client->config->refresh_connection_after_ms) {
                ESP_LOGD(TAG, "Refreshing the connection...");
                esp_mqtt_abort_connection(client);
                client->state = MQTT_STATE_INIT;
            }

            break;
        case MQTT_STATE_WAIT_RECONNECT:

            if (!client->config->auto_reconnect && xEventGroupGetBits(client->status_bits)&RECONNECT_BIT) {
                xEventGroupClearBits(client->status_bits, RECONNECT_BIT);
                client->state = MQTT_STATE_INIT;
                client->wait_timeout_ms = MQTT_RECON_DEFAULT_MS;
                ESP_LOGD(TAG, "Reconnecting per user request...");
                break;
            } else if (client->config->auto_reconnect &&
                       platform_tick_get_ms() - client->reconnect_tick > client->wait_timeout_ms) {
                client->state = MQTT_STATE_INIT;
                client->reconnect_tick = platform_tick_get_ms();
                ESP_LOGD(TAG, "Reconnecting...");
                break;
            }
            MQTT_API_UNLOCK(client);
            xEventGroupWaitBits(client->status_bits, RECONNECT_BIT, false, true,
                                client->wait_timeout_ms / 2 / portTICK_PERIOD_MS);
            // continue the while loop instead of break, as the mutex is unlocked
            continue;
        default:
            ESP_LOGE(TAG, "MQTT client error, client is in an unrecoverable state.");
            break;
        }
        MQTT_API_UNLOCK(client);
        if (MQTT_STATE_CONNECTED == client->state) {
            if (esp_transport_poll_read(client->transport, MQTT_POLL_READ_TIMEOUT_MS) < 0) {
                ESP_LOGE(TAG, "Poll read error: %d, aborting connection", errno);
                esp_mqtt_abort_connection(client);
            }
        }

    }
    esp_transport_close(client->transport);
    outbox_delete_all_items(client->outbox);
    xEventGroupSetBits(client->status_bits, STOPPED_BIT);

    vTaskDelete(NULL);
}

esp_err_t esp_mqtt_client_start(esp_mqtt_client_handle_t client)
{
    if (!client) {
        ESP_LOGE(TAG, "Client was not initialized");
        return ESP_ERR_INVALID_ARG;
    }
    MQTT_API_LOCK(client);
    if (client->state != MQTT_STATE_INIT && client->state != MQTT_STATE_DISCONNECTED) {
        ESP_LOGE(TAG, "Client has started");
        MQTT_API_UNLOCK(client);
        return ESP_FAIL;
    }
    if (esp_mqtt_client_create_transport(client) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create transport list");
        MQTT_API_UNLOCK(client);
        return ESP_FAIL;
    }
    esp_err_t err = ESP_OK;
#if MQTT_CORE_SELECTION_ENABLED
    ESP_LOGD(TAG, "Core selection enabled on %u", MQTT_TASK_CORE);
    if (xTaskCreatePinnedToCore(esp_mqtt_task, "mqtt_task", client->config->task_stack, client, client->config->task_prio, &client->task_handle, MQTT_TASK_CORE) != pdTRUE) {
        ESP_LOGE(TAG, "Error create mqtt task");
        err = ESP_FAIL;
    }
#else
    ESP_LOGD(TAG, "Core selection disabled");
    if (xTaskCreate(esp_mqtt_task, "mqtt_task", client->config->task_stack, client, client->config->task_prio, &client->task_handle) != pdTRUE) {
        ESP_LOGE(TAG, "Error create mqtt task");
        err = ESP_FAIL;
    }
#endif
    MQTT_API_UNLOCK(client);
    return err;
}

esp_err_t esp_mqtt_client_disconnect(esp_mqtt_client_handle_t client)
{
    if (!client) {
        ESP_LOGE(TAG, "Client was not initialized");
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGI(TAG, "Client asked to disconnect");
    xEventGroupSetBits(client->status_bits, DISCONNECT_BIT);
    return ESP_OK;
}

esp_err_t esp_mqtt_client_reconnect(esp_mqtt_client_handle_t client)
{
    if (!client) {
        ESP_LOGE(TAG, "Client was not initialized");
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGI(TAG, "Client force reconnect requested");
    if (client->state != MQTT_STATE_WAIT_RECONNECT) {
        ESP_LOGD(TAG, "The client is not waiting for reconnection. Ignore the request");
        return ESP_FAIL;
    }
    client->wait_timeout_ms = 0;
    xEventGroupSetBits(client->status_bits, RECONNECT_BIT);
    return ESP_OK;
}

#ifdef CONFIG_MQTT_PROTOCOL_50
esp_err_t send_disconnect_msg(esp_mqtt_client_handle_t client, int reason)
#else
esp_err_t send_disconnect_msg(esp_mqtt_client_handle_t client)
#endif
{
    // Notify the broker we are disconnecting
#ifdef CONFIG_MQTT_PROTOCOL_50
    if (reason == SUCCESS && client->config->send_will_on_disconnect) {
        reason = DISCONNECT_WITH_WILL_MESSAGE;
    }
    // TODO : Fill properties if needed

    // Session expiry interval (client only)

    // Reason string

    // User props

    // Server reference. Sent only by server

    // TODO END
    client->mqtt_state.outbound_message = mqtt_msg_disconnect(&client->mqtt_state.mqtt_connection, reason, NULL);
#else
    client->mqtt_state.outbound_message = mqtt_msg_disconnect(&client->mqtt_state.mqtt_connection);
#endif
    if (client->mqtt_state.outbound_message->length == 0) {
        ESP_LOGE(TAG, "Disconnect message cannot be created");
        return ESP_FAIL;
    }
    if (mqtt_write_data(client) != ESP_OK) {
        ESP_LOGE(TAG, "Error sending disconnect message");
    }
    return ESP_OK;
}

esp_err_t esp_mqtt_client_stop(esp_mqtt_client_handle_t client)
{
    if (!client) {
        ESP_LOGE(TAG, "Client was not initialized");
        return ESP_ERR_INVALID_ARG;
    }
    MQTT_API_LOCK(client);
    if (client->run) {
        /* A running client cannot be stopped from the MQTT task/event handler */
        TaskHandle_t running_task = xTaskGetCurrentTaskHandle();
        if (running_task == client->task_handle) {
            MQTT_API_UNLOCK(client);
            ESP_LOGE(TAG, "Client cannot be stopped from MQTT task");
            return ESP_FAIL;
        }

        // Only send the disconnect message if the client is connected
        if (client->state == MQTT_STATE_CONNECTED) {
#ifdef CONFIG_MQTT_PROTOCOL_50
            if (send_disconnect_msg(client, SUCCESS) != ESP_OK) {
#else
            if (send_disconnect_msg(client) != ESP_OK) {
#endif
                MQTT_API_UNLOCK(client);
                return ESP_FAIL;
            }
        }

        client->run = false;
        client->state = MQTT_STATE_DISCONNECTED;
        MQTT_API_UNLOCK(client);
        xEventGroupWaitBits(client->status_bits, STOPPED_BIT, false, true, portMAX_DELAY);
        return ESP_OK;
    } else {
        ESP_LOGW(TAG, "Client asked to stop, but was not started");
        MQTT_API_UNLOCK(client);
        return ESP_FAIL;
    }
}

static esp_err_t esp_mqtt_client_ping(esp_mqtt_client_handle_t client)
{
    client->mqtt_state.outbound_message = mqtt_msg_pingreq(&client->mqtt_state.mqtt_connection);
    if (client->mqtt_state.outbound_message->length == 0) {
        ESP_LOGE(TAG, "Ping message cannot be created");
        return ESP_FAIL;
    }

    if (mqtt_write_data(client) != ESP_OK) {
        ESP_LOGE(TAG, "Error sending ping");
        return ESP_FAIL;
    }
    ESP_LOGD(TAG, "Sent PING successful");
    return ESP_OK;
}

int esp_mqtt_client_subscribe(esp_mqtt_client_handle_t client, mqtt_subscribe_info_t* info)
{
    if (!client) {
        ESP_LOGE(TAG, "Client was not initialized");
        return ESP_FAIL;
    }
    if (!info) {
        ESP_LOGE(TAG, "No subscribe info provided.");
        return ESP_FAIL;
    }
    MQTT_API_LOCK(client);
    if (client->state != MQTT_STATE_CONNECTED) {
        ESP_LOGE(TAG, "Client has not connected");
        MQTT_API_UNLOCK(client);
        return ESP_FAIL;
    }
#ifdef CONFIG_MQTT_PROTOCOL_50
    mqtt_properties_t props = { 0 };

    // Subscription identifier
    if(info->subscription_id != 0) {
        if(mqtt_property_put_number(&props, SUBSCRIPTION_IDENTIFIER, info->subscription_id) != ESP_OK) {
            return ESP_FAIL;
        }
    }

    // User props
    for(int i = 0; i < info->user_props_count; ++i) {
        if(mqtt_property_put_pair(&props, USER_PROPERTY, info->user_props[i].key, info->user_props[i].value) != ESP_OK) {
            return ESP_FAIL;
        }
    }

    client->mqtt_state.outbound_message = mqtt_msg_subscribe(&client->mqtt_state.mqtt_connection,
                                          &client->mqtt_state.pending_msg_id,
                                          &props,
                                          info->topics_count, 
                                          info->topics);
#else
    client->mqtt_state.outbound_message = mqtt_msg_subscribe(&client->mqtt_state.mqtt_connection,
                                          &client->mqtt_state.pending_msg_id,
                                          info->topics_count, 
                                          info->topics);
#endif
    if (client->mqtt_state.outbound_message->length == 0) {
        ESP_LOGE(TAG, "Subscribe message cannot be created");
        MQTT_API_UNLOCK(client);
        return ESP_FAIL;
    }

    client->mqtt_state.pending_msg_type = mqtt_get_type(client->mqtt_state.outbound_message->data);
    client->mqtt_state.pending_msg_count ++;
    //move pending msg to outbox (if have)
    if (!mqtt_enqueue(client)) {
        MQTT_API_UNLOCK(client);
        return ESP_FAIL;
    }
    outbox_set_pending(client->outbox, client->mqtt_state.pending_msg_id, TRANSMITTED);// handle error

    if (mqtt_write_data(client) != ESP_OK) {
        for(int i = 0; i < info->topics_count; ++i) {
            ESP_LOGE(TAG, "Error to subscribe topic=%s, qos=%d", info->topics[i].name, info->topics[i].qos);
        }
        MQTT_API_UNLOCK(client);
        return ESP_FAIL;
    }

    for(int i = 0; i < info->topics_count; ++i) {
        ESP_LOGD(TAG, "Sent subscribe topic=%s, id: %d, type=%d successful", info->topics[i].name, client->mqtt_state.pending_msg_id, client->mqtt_state.pending_msg_type);
    }
    MQTT_API_UNLOCK(client);
    return client->mqtt_state.pending_msg_id;
}

int esp_mqtt_client_unsubscribe(esp_mqtt_client_handle_t client, mqtt_unsubscribe_info_t* info)
{
    if (!client) {
        ESP_LOGE(TAG, "Client was not initialized");
        return ESP_FAIL;
    }
    if (!info) {
        ESP_LOGE(TAG, "No unsubscribe info provided.");
        return ESP_FAIL;
    }
    MQTT_API_LOCK(client);
    if (client->state != MQTT_STATE_CONNECTED) {
        MQTT_API_UNLOCK(client);
        ESP_LOGE(TAG, "Client has not connected");
        return ESP_FAIL;
    }
#ifdef CONFIG_MQTT_PROTOCOL_50
    mqtt_properties_t props = { 0 };
    // User props
    for(int i = 0; i < info->user_props_count; ++i) {
        if(mqtt_property_put_pair(&props, USER_PROPERTY, info->user_props[i].key, info->user_props[i].value) != ESP_OK) {
            return ESP_FAIL;
        }
    }

    client->mqtt_state.outbound_message = mqtt_msg_unsubscribe(&client->mqtt_state.mqtt_connection,
                                          &client->mqtt_state.pending_msg_id,
                                          &props,
                                          info->topics_count,
                                          info->topics
                                          );
#else
    client->mqtt_state.outbound_message = mqtt_msg_unsubscribe(&client->mqtt_state.mqtt_connection,
                                          &client->mqtt_state.pending_msg_id,
                                          info->topics_count,
                                          info->topics
                                          );
#endif
    if (client->mqtt_state.outbound_message->length == 0) {
        MQTT_API_UNLOCK(client);
        ESP_LOGE(TAG, "Unubscribe message cannot be created");
        return ESP_FAIL;
    }
    for(int i = 0; i < info->topics_count; ++i) {
        ESP_LOGD(TAG, "unsubscribe, topic\"%s\", id: %d", info->topics[i], client->mqtt_state.pending_msg_id);
    }

    client->mqtt_state.pending_msg_type = mqtt_get_type(client->mqtt_state.outbound_message->data);
    client->mqtt_state.pending_msg_count ++;
    if (!mqtt_enqueue(client)) {
        MQTT_API_UNLOCK(client);
        return ESP_FAIL;
    }
    outbox_set_pending(client->outbox, client->mqtt_state.pending_msg_id, TRANSMITTED); //handle error

    if (mqtt_write_data(client) != ESP_OK) {
        for(int i = 0; i < info->topics_count; ++i) {
            ESP_LOGE(TAG, "Error to unsubscribe topic=%s", info->topics[i]);
        }
        MQTT_API_UNLOCK(client);
        return ESP_FAIL;
    }

    for(int i = 0; i < info->topics_count; ++i) {
        ESP_LOGD(TAG, "Sent Unsubscribe topics=%s, id: %d, successful", info->topics[i], client->mqtt_state.pending_msg_id);
    }
    MQTT_API_UNLOCK(client);
    return client->mqtt_state.pending_msg_id;
}

static inline int mqtt_client_enqueue_priv(
    esp_mqtt_client_handle_t client, 
    mqtt_publish_info_t* info,
    bool store)
{
    uint16_t pending_msg_id = 0;
#ifdef CONFIG_MQTT_PROTOCOL_50
    // Build props
    mqtt_properties_t props = { 0 };

    if(info->payload_format_indicator) {
        if(mqtt_property_put_number(&props, PAYLOAD_FORMAT_INDICATOR, info->payload_format_indicator) != ESP_OK) {
            return ESP_FAIL;
        }
    }

    if(info->message_expiry_interval) {
        if(mqtt_property_put_number(&props, MESSAGE_EXPIRY_INTERVAL, info->message_expiry_interval) != ESP_OK) {
            return ESP_FAIL;
        }
    }

    if(info->topic_alias) {
        if(mqtt_property_put_number(&props, TOPIC_ALIAS, info->topic_alias) != ESP_OK) {
            return ESP_FAIL;
        }
    }

    if(info->response_topic) {
        if(mqtt_property_put_buffer(&props, RESPONSE_TOPIC, (uint8_t*)info->response_topic, strlen(info->response_topic)) != ESP_OK) {
            return ESP_FAIL;
        }
    }

    if(info->correlation_data && info->correlation_data_len) {
        if(mqtt_property_put_buffer(&props, CORRELATION_DATA, info->correlation_data, info->correlation_data_len) != ESP_OK) {
            return ESP_FAIL;
        }
    }

    // User props
    for(int i = 0; i < info->user_props_count; ++i) {
        if(mqtt_property_put_pair(&props, USER_PROPERTY, info->user_props[i].key, info->user_props[i].value) != ESP_OK) {
            return ESP_FAIL;
        }
    }

    // Subscription identifier not implemented in client

    if(info->content_type) {
        if(mqtt_property_put_buffer(&props, CONTENT_TYPE, (uint8_t*)info->content_type, strlen(info->content_type)) != ESP_OK) {
            return ESP_FAIL;
        }
    }

    mqtt_message_t *publish_msg = mqtt_msg_publish(&client->mqtt_state.mqtt_connection,
                                  info->topic,
                                  &props, 
                                  info->data, info->data_len,
                                  info->qos, info->retain,
                                  &pending_msg_id);
#else
    mqtt_message_t *publish_msg = mqtt_msg_publish(&client->mqtt_state.mqtt_connection,
                                  info->topic, info->data, info->data_len,
                                  info->qos, info->retain,
                                  &pending_msg_id);
#endif

    if (publish_msg->length == 0) {
        ESP_LOGE(TAG, "Publish message cannot be created");
        return -1;
    }
    /* We have to set as pending all the qos>0 messages */
    client->mqtt_state.outbound_message = publish_msg;
    if (info->qos > 0 || store) {
        client->mqtt_state.pending_msg_type = mqtt_get_type(client->mqtt_state.outbound_message->data);
        client->mqtt_state.pending_msg_id = pending_msg_id;
        client->mqtt_state.pending_publish_qos = info->qos;
        client->mqtt_state.pending_msg_count ++;
        // by default store as QUEUED (not transmitted yet) only for messages which would fit outbound buffer
        if (client->mqtt_state.mqtt_connection.message.fragmented_msg_total_length == 0) {
            if (!mqtt_enqueue(client)) {
                return -1;
            }
        } else {
            int first_fragment = client->mqtt_state.outbound_message->length - client->mqtt_state.outbound_message->fragmented_msg_data_offset;
            if (!mqtt_enqueue_oversized(client, ((uint8_t *)info->data) + first_fragment, info->data_len - first_fragment)) {
                return -1;
            }
            client->mqtt_state.outbound_message->fragmented_msg_total_length = 0;
        }
    }
    return pending_msg_id;
}

int esp_mqtt_client_publish(esp_mqtt_client_handle_t client, mqtt_publish_info_t* info)
{
    if (!client) {
        ESP_LOGE(TAG, "Client was not initialized");
        return -1;
    }
    if (!info) {
        ESP_LOGE(TAG, "No publish data provided");
        return -1;
    }
    MQTT_API_LOCK(client);
#if MQTT_SKIP_PUBLISH_IF_DISCONNECTED
    if (client->state != MQTT_STATE_CONNECTED) {
        ESP_LOGI(TAG, "Publishing skipped: client is not connected");
        MQTT_API_UNLOCK(client);
        return -1;
    }
#endif

    /* Acceptable publish messages:
        data == NULL, len == 0: publish null message
        data valid,   len == 0: publish all data, payload len is determined from string length
        data valid,   len >  0: publish data with defined length
     */
    if (info->data_len <= 0 && info->data != NULL) {
        info->data_len = strlen(info->data);
    }

    int pending_msg_id = mqtt_client_enqueue_priv(client, info, false);
    if (pending_msg_id < 0) {
        MQTT_API_UNLOCK(client);
        return -1;
    }
    int ret = 0;

    /* Skip sending if not connected (rely on resending) */
    if (client->state != MQTT_STATE_CONNECTED) {
        ESP_LOGD(TAG, "Publish: client is not connected");
        if (info->qos > 0) {
            ret = pending_msg_id;
        }

        // delete long pending messages
        mqtt_delete_expired_messages(client);

        goto cannot_publish;
    }

    /* Provide support for sending fragmented message if it doesn't fit buffer */
    int remaining_len = info->data_len;
    const char *current_data = info->data;
    bool sending = true;

    while (sending)  {

        if (mqtt_write_data(client) != ESP_OK) {
            esp_mqtt_abort_connection(client);
            ret = -1;
            goto cannot_publish;
        }

        int data_sent = client->mqtt_state.outbound_message->length - client->mqtt_state.outbound_message->fragmented_msg_data_offset;
        client->mqtt_state.outbound_message->fragmented_msg_data_offset = 0;
        client->mqtt_state.outbound_message->fragmented_msg_total_length = 0;
        remaining_len -= data_sent;
        current_data +=  data_sent;

        if (remaining_len > 0) {
            mqtt_connection_t *connection = &client->mqtt_state.mqtt_connection;
            ESP_LOGD(TAG, "Sending fragmented message, remains to send %d bytes of %d", remaining_len, info->data_len);
            if (remaining_len > connection->buffer_length) {
                // Continue with sending
                memcpy(connection->buffer, current_data, connection->buffer_length);
                connection->message.length = connection->buffer_length;
                sending = true;
            } else {
                memcpy(connection->buffer, current_data, remaining_len);
                connection->message.length = remaining_len;
                sending = true;
            }
            connection->message.data = connection->buffer;
            client->mqtt_state.outbound_message = &connection->message;
        } else {
            // Message was sent correctly
            sending = false;
        }
    }

    if (info->qos > 0) {
        //Tick is set after transmit to avoid retransmitting too early due slow network speed / big messages
        outbox_set_tick(client->outbox, pending_msg_id, platform_tick_get_ms());
        outbox_set_pending(client->outbox, pending_msg_id, TRANSMITTED);
    }
    MQTT_API_UNLOCK(client);
    return pending_msg_id;

cannot_publish:
    // clear out possible fragmented publish if failed or skipped
    client->mqtt_state.outbound_message->fragmented_msg_total_length = 0;
    if (info->qos == 0) {
        ESP_LOGW(TAG, "Publish: Losing qos0 data when client not connected");
    }
    MQTT_API_UNLOCK(client);

    return ret;
}

int esp_mqtt_client_enqueue(esp_mqtt_client_handle_t client, mqtt_publish_info_t* info, bool store)
{
    if (!client) {
        ESP_LOGE(TAG, "Client was not initialized");
        return -1;
    }
    if (!info) {
        ESP_LOGE(TAG, "No publish data provided");
        return -1;
    }

    /* Acceptable publish messages:
        data == NULL, len == 0: publish null message
        data valid,   len == 0: publish all data, payload len is determined from string length
        data valid,   len >  0: publish data with defined length
     */
    if (info->data_len <= 0 && info->data != NULL) {
        info->data_len = strlen(info->data);
    }

    MQTT_API_LOCK(client);
    int ret = mqtt_client_enqueue_priv(client, info, store);
    MQTT_API_UNLOCK(client);
    if (ret == 0 && store == false) {
        // messages with qos=0 are not enqueued if not overridden by store_in_outobx -> indicate as error
        return -1;
    }
    return ret;
}

esp_err_t esp_mqtt_client_register_event(esp_mqtt_client_handle_t client, esp_mqtt_event_id_t event, esp_event_handler_t event_handler, void *event_handler_arg)
{
    if (client == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
#ifdef MQTT_SUPPORTED_FEATURE_EVENT_LOOP
    if (client->config->event_handle) {
        ESP_LOGW(TAG, "Registering event loop while event callback is not null, clearing callback");
        client->config->event_handle = NULL;
    }

    return esp_event_handler_register_with(client->config->event_loop_handle, MQTT_EVENTS, event, event_handler, event_handler_arg);
#else
    ESP_LOGE(TAG, "Registering event handler while event loop not available in IDF version %s", IDF_VER);
    return ESP_FAIL;
#endif
}


static void esp_mqtt_client_dispatch_transport_error(esp_mqtt_client_handle_t client)
{
    client->event.event_id = MQTT_EVENT_ERROR;
    client->event.error_handle.error_type = MQTT_ERROR_TYPE_TCP_TRANSPORT;
#ifdef MQTT_SUPPORTED_FEATURE_TRANSPORT_ERR_REPORTING
    client->event.error_handle.esp_tls_last_esp_err = esp_tls_get_and_clear_last_error(esp_transport_get_error_handle(client->transport),
            &client->event.error_handle.esp_tls_stack_err,
            &client->event.error_handle.esp_tls_cert_verify_flags);
#ifdef MQTT_SUPPORTED_FEATURE_TRANSPORT_SOCK_ERRNO_REPORTING
    client->event.error_handle.esp_transport_sock_errno = esp_transport_get_errno(client->transport);
#endif
#endif
    esp_mqtt_dispatch_event(client);
}

int esp_mqtt_client_get_outbox_size(esp_mqtt_client_handle_t client)
{
    int outbox_size = 0;

    if (client == NULL) {
        return 0;
    }

    MQTT_API_LOCK(client);

    if (client->outbox) {
        outbox_size = outbox_get_size(client->outbox);
    }

    MQTT_API_UNLOCK(client);

    return outbox_size;
}
