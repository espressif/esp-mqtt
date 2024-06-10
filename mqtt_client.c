#include <stdint.h>
#include <stdlib.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_transport.h"
#include "mqtt_client.h"
#include "mqtt_client_priv.h"
#include "mqtt_msg.h"
#include "mqtt_outbox.h"

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

static esp_err_t esp_mqtt_dispatch_event(esp_mqtt_client_handle_t client);
static esp_err_t esp_mqtt_dispatch_event_with_msgid(esp_mqtt_client_handle_t client);
static esp_err_t esp_mqtt_connect(esp_mqtt_client_handle_t client, int timeout_ms);
static void esp_mqtt_abort_connection(esp_mqtt_client_handle_t client);
static esp_err_t esp_mqtt_client_ping(esp_mqtt_client_handle_t client);
static char *create_string(const char *ptr, int len);
static int mqtt_message_receive(esp_mqtt_client_handle_t client, int read_poll_timeout_ms);
static void esp_mqtt_client_dispatch_transport_error(esp_mqtt_client_handle_t client);
static esp_err_t send_disconnect_msg(esp_mqtt_client_handle_t client);

/**
 * @brief Processes error reported from transport layer (considering the message read status)
 *
 * @param err: Error reported from TCP transport
 * @param client: MQTT client handle
 * @param mid_message: True if the error occured when reading incomplete message
 *
 * @return  - 0 on Timeout
 *          - -1 on Timeout with incomplete message
 *          - -2 on Error or EOF
 */
static int esp_mqtt_handle_transport_read_error(int err, esp_mqtt_client_handle_t client, bool mid_message)
{
    if (err == ERR_TCP_TRANSPORT_CONNECTION_TIMEOUT) {
        if (mid_message) {
            // No error message, because we could've read with timeout 0 (caller will decide)
            return -1;
        }
        // Not an error, continue
        ESP_LOGV(TAG, "%s: transport_read(): call timed out before data was ready!", __func__);
        return 0;
    }

    if (err == ERR_TCP_TRANSPORT_CONNECTION_CLOSED_BY_FIN) {
        ESP_LOGE(TAG, "%s: transport_read(): EOF", __func__);
    }

    ESP_LOGE(TAG, "%s: transport_read() error: errno=%d", __func__, errno);
    esp_mqtt_client_dispatch_transport_error(client);
    return -2;
}

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

    if (cfg->common_name) {
#if defined(MQTT_SUPPORTED_FEATURE_CRT_CMN_NAME) && MQTT_ENABLE_SSL
        esp_transport_ssl_set_common_name(ssl, cfg->common_name);
#else
        ESP_LOGE(TAG, "Setting expected certificate common name is not available in IDF version %s", IDF_VER);
        goto esp_mqtt_set_transport_failed;
#endif
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


    return ESP_OK;

esp_mqtt_set_transport_failed:
    return ESP_FAIL;
}
#endif // MQTT_ENABLE_SSL

/* Checks if the user supplied config values are internally consistent */
static esp_err_t esp_mqtt_check_cfg_conflict(const mqtt_config_storage_t *cfg, const esp_mqtt_client_config_t *user_cfg)
{
    if (cfg == NULL || user_cfg == NULL) {
        ESP_LOGE(TAG, "Invalid configuration");
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t ret = ESP_OK;

    bool ssl_cfg_enabled = cfg->use_global_ca_store || cfg->cacert_buf || cfg->clientcert_buf || cfg->psk_hint_key || cfg->alpn_protos;
    bool is_ssl_scheme = false;
    if (cfg->scheme) {
        is_ssl_scheme = (strncasecmp(cfg->scheme, MQTT_OVER_SSL_SCHEME, sizeof(MQTT_OVER_SSL_SCHEME)) == 0) || (strncasecmp(cfg->scheme, MQTT_OVER_WSS_SCHEME, sizeof(MQTT_OVER_WSS_SCHEME)) == 0);
    }

    if (!is_ssl_scheme && ssl_cfg_enabled) {
        if (cfg->uri) {
            ESP_LOGW(TAG, "SSL related configs set, but the URI scheme specifies a non-SSL scheme, scheme = %s", cfg->scheme);
        } else {
            ESP_LOGW(TAG, "SSL related configs set, but the transport protocol is a non-SSL scheme, transport = %d", user_cfg->broker.address.transport);
        }
        ret = ESP_ERR_INVALID_ARG;
    }

    if (cfg->uri && user_cfg->broker.address.transport) {
        ESP_LOGW(TAG, "Transport config set, but overridden by scheme from URI: transport = %d, uri scheme = %s", user_cfg->broker.address.transport, cfg->scheme);
        ret = ESP_ERR_INVALID_ARG;
    }

    return ret;
}

bool esp_mqtt_set_if_config(char const *const new_config, char **old_config)
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

        if ((strncasecmp(client->config->scheme, MQTT_OVER_TCP_SCHEME, sizeof(MQTT_OVER_TCP_SCHEME)) == 0) || (strncasecmp(client->config->scheme, MQTT_OVER_WS_SCHEME, sizeof(MQTT_OVER_WS_SCHEME)) == 0)) {
            esp_transport_handle_t tcp = esp_transport_tcp_init();
            ESP_MEM_CHECK(TAG, tcp, return ESP_ERR_NO_MEM);
            esp_transport_set_default_port(tcp, MQTT_TCP_DEFAULT_PORT);
            if (client->config->if_name) {
                esp_transport_tcp_set_interface_name(tcp, client->config->if_name);
            }
            esp_transport_list_add(client->transport_list, tcp, MQTT_OVER_TCP_SCHEME);
            if (strncasecmp(client->config->scheme, MQTT_OVER_WS_SCHEME, sizeof(MQTT_OVER_WS_SCHEME)) == 0) {
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
        } else if ((strncasecmp(client->config->scheme, MQTT_OVER_SSL_SCHEME, sizeof(MQTT_OVER_SSL_SCHEME)) == 0) || (strncasecmp(client->config->scheme, MQTT_OVER_WSS_SCHEME, sizeof(MQTT_OVER_WSS_SCHEME)) == 0)) {
#if MQTT_ENABLE_SSL
            esp_transport_handle_t ssl = esp_transport_ssl_init();
            ESP_MEM_CHECK(TAG, ssl, return ESP_ERR_NO_MEM);
            esp_transport_set_default_port(ssl, MQTT_SSL_DEFAULT_PORT);
            if (client->config->if_name) {
                esp_transport_ssl_set_interface_name(ssl, client->config->if_name);
            }
            esp_transport_list_add(client->transport_list, ssl, MQTT_OVER_SSL_SCHEME);
            if (strncasecmp(client->config->scheme, MQTT_OVER_WSS_SCHEME, sizeof(MQTT_OVER_WSS_SCHEME)) == 0) {
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

    mqtt_msg_buffer_destroy(&client->mqtt_state.connection);
    int buffer_size = config->buffer.size;
    if (buffer_size <= 0) {
        buffer_size = MQTT_BUFFER_SIZE_BYTE;
    }

    // use separate value for output buffer size if configured
    int out_buffer_size = config->buffer.out_size > 0 ? config->buffer.out_size : buffer_size;
    if (mqtt_msg_buffer_init(&client->mqtt_state.connection, out_buffer_size) != ESP_OK) {
        goto _mqtt_set_config_failed;
    }

    free(client->mqtt_state.in_buffer);
    client->mqtt_state.in_buffer = (uint8_t *)malloc(buffer_size);
    ESP_MEM_CHECK(TAG, client->mqtt_state.in_buffer, goto _mqtt_set_config_failed);
    client->mqtt_state.in_buffer_length = buffer_size;

    client->config->message_retransmit_timeout = config->session.message_retransmit_timeout;
    if (config->session.message_retransmit_timeout <= 0) {
        client->config->message_retransmit_timeout = MQTT_DEFAULT_RETRANSMIT_TIMEOUT_MS;
    }

    client->config->task_prio = config->task.priority;
    if (client->config->task_prio <= 0) {
        client->config->task_prio = MQTT_TASK_PRIORITY;
    }

    client->config->task_stack = config->task.stack_size;
    if (client->config->task_stack <= 0) {
        client->config->task_stack = MQTT_TASK_STACK;
    }

    if (config->broker.address.port) {
        client->config->port = config->broker.address.port;
    }

    err = ESP_ERR_NO_MEM;
    ESP_MEM_CHECK(TAG, esp_mqtt_set_if_config(config->broker.address.hostname, &client->config->host), goto _mqtt_set_config_failed);
    ESP_MEM_CHECK(TAG, esp_mqtt_set_if_config(config->broker.address.path, &client->config->path), goto _mqtt_set_config_failed);
    ESP_MEM_CHECK(TAG, esp_mqtt_set_if_config(config->credentials.username, &client->mqtt_state.connection.information.username), goto _mqtt_set_config_failed);
    ESP_MEM_CHECK(TAG, esp_mqtt_set_if_config(config->credentials.authentication.password, &client->mqtt_state.connection.information.password), goto _mqtt_set_config_failed);

    if (!config->credentials.set_null_client_id) {
        if (config->credentials.client_id) {
            ESP_MEM_CHECK(TAG, esp_mqtt_set_if_config(config->credentials.client_id, &client->mqtt_state.connection.information.client_id), goto _mqtt_set_config_failed);
        } else if (client->mqtt_state.connection.information.client_id == NULL) {
            client->mqtt_state.connection.information.client_id = platform_create_id_string();
        }
        ESP_MEM_CHECK(TAG, client->mqtt_state.connection.information.client_id, goto _mqtt_set_config_failed);
        ESP_LOGD(TAG, "MQTT client_id=%s", client->mqtt_state.connection.information.client_id);
    }

    ESP_MEM_CHECK(TAG, esp_mqtt_set_if_config(config->broker.address.uri, &client->config->uri), goto _mqtt_set_config_failed);
    ESP_MEM_CHECK(TAG, esp_mqtt_set_if_config(config->session.last_will.topic, &client->mqtt_state.connection.information.will_topic), goto _mqtt_set_config_failed);

    if (config->session.last_will.msg_len && config->session.last_will.msg) {
        free(client->mqtt_state.connection.information.will_message);
        client->mqtt_state.connection.information.will_message = malloc(config->session.last_will.msg_len);
        ESP_MEM_CHECK(TAG, client->mqtt_state.connection.information.will_message, goto _mqtt_set_config_failed);
        memcpy(client->mqtt_state.connection.information.will_message, config->session.last_will.msg, config->session.last_will.msg_len);
        client->mqtt_state.connection.information.will_length = config->session.last_will.msg_len;
    } else if (config->session.last_will.msg) {
        free(client->mqtt_state.connection.information.will_message);
        client->mqtt_state.connection.information.will_message = strdup(config->session.last_will.msg);
        ESP_MEM_CHECK(TAG, client->mqtt_state.connection.information.will_message, goto _mqtt_set_config_failed);
        client->mqtt_state.connection.information.will_length = strlen(config->session.last_will.msg);
    }
    if (config->session.last_will.qos) {
        client->mqtt_state.connection.information.will_qos = config->session.last_will.qos;
    }
    if (config->session.last_will.retain) {
        client->mqtt_state.connection.information.will_retain = config->session.last_will.retain;
    }

    if (config->session.disable_clean_session == client->mqtt_state.connection.information.clean_session) {
        client->mqtt_state.connection.information.clean_session = !config->session.disable_clean_session;
        if (!client->mqtt_state.connection.information.clean_session && config->credentials.set_null_client_id) {
            ESP_LOGE(TAG, "Clean Session flag must be true if client has a null id");
        }
    }
    if (config->session.keepalive) {
        client->mqtt_state.connection.information.keepalive = config->session.keepalive;
    }
    if (client->mqtt_state.connection.information.keepalive == 0) {
        client->mqtt_state.connection.information.keepalive = MQTT_KEEPALIVE_TICK;
    }
    if (config->session.disable_keepalive) {
        // internal `keepalive` value (in connect_info) is in line with 3.1.2.10 Keep Alive from mqtt spec:
        //      * keepalive=0: Keep alive mechanism disabled (server not to disconnect the client on its inactivity)
        //      * period in seconds to send a Control packet if inactive
        client->mqtt_state.connection.information.keepalive = 0;
    }

    if (config->session.protocol_ver) {
        client->mqtt_state.connection.information.protocol_ver = config->session.protocol_ver;
    }
    if (client->mqtt_state.connection.information.protocol_ver == MQTT_PROTOCOL_UNDEFINED) {
#ifdef MQTT_PROTOCOL_311
        client->mqtt_state.connection.information.protocol_ver = MQTT_PROTOCOL_V_3_1_1;
#else
        client->mqtt_state.connection.information.protocol_ver = MQTT_PROTOCOL_V_3_1;
#endif
    } else if (client->mqtt_state.connection.information.protocol_ver == MQTT_PROTOCOL_V_5) {
#ifndef MQTT_PROTOCOL_5
        ESP_LOGE(TAG, "Please first enable MQTT_PROTOCOL_5 feature in menuconfig");
        goto _mqtt_set_config_failed;
#endif
    }

    client->config->network_timeout_ms = config->network.timeout_ms;
    if (client->config->network_timeout_ms <= 0) {
        client->config->network_timeout_ms = MQTT_NETWORK_TIMEOUT_MS;
    }

    if (config->network.refresh_connection_after_ms) {
        client->config->refresh_connection_after_ms = config->network.refresh_connection_after_ms;
    }

    client->config->auto_reconnect = true;
    if (config->network.disable_auto_reconnect == client->config->auto_reconnect) {
        client->config->auto_reconnect = !config->network.disable_auto_reconnect;
    }

    if (config->network.reconnect_timeout_ms) {
        client->config->reconnect_timeout_ms = config->network.reconnect_timeout_ms;
    } else {
        client->config->reconnect_timeout_ms = MQTT_RECON_DEFAULT_MS;
    }
    if (config->network.transport) {
        client->config->transport = config->network.transport;
    }

    if (config->network.if_name) {
        client->config->if_name = calloc(1, sizeof(struct ifreq) + 1);
        ESP_MEM_CHECK(TAG, client->config->if_name, goto _mqtt_set_config_failed);
        memcpy(client->config->if_name, config->network.if_name, sizeof(struct ifreq));
    }

    if (config->broker.verification.alpn_protos) {
        for (int i = 0; i < client->config->num_alpn_protos; i++) {
            free(client->config->alpn_protos[i]);
        }
        free(client->config->alpn_protos);
        client->config->num_alpn_protos = 0;

        const char **p;

        for (p = config->broker.verification.alpn_protos; *p != NULL; p++ ) {
            client->config->num_alpn_protos++;
        }
        // mbedTLS expects the list to be null-terminated
        client->config->alpn_protos = calloc(client->config->num_alpn_protos + 1, sizeof(*config->broker.verification.alpn_protos));
        ESP_MEM_CHECK(TAG, client->config->alpn_protos, goto _mqtt_set_config_failed);

        for (int i = 0; i < client->config->num_alpn_protos; i++) {
            client->config->alpn_protos[i] = strdup(config->broker.verification.alpn_protos[i]);
            ESP_MEM_CHECK(TAG, client->config->alpn_protos[i], goto _mqtt_set_config_failed);
        }
    }

    // configure ssl related parameters
    client->config->use_global_ca_store = config->broker.verification.use_global_ca_store;
    client->config->cacert_buf = config->broker.verification.certificate;
    client->config->cacert_bytes = config->broker.verification.certificate_len;
    client->config->psk_hint_key = config->broker.verification.psk_hint_key;
    client->config->crt_bundle_attach = config->broker.verification.crt_bundle_attach;
    client->config->clientcert_buf = config->credentials.authentication.certificate;
    client->config->clientcert_bytes = config->credentials.authentication.certificate_len;
    client->config->clientkey_buf = config->credentials.authentication.key;
    client->config->clientkey_bytes = config->credentials.authentication.key_len;
    client->config->skip_cert_common_name_check = config->broker.verification.skip_cert_common_name_check;
    client->config->common_name = config->broker.verification.common_name;
    client->config->use_secure_element = config->credentials.authentication.use_secure_element;
    client->config->ds_data = config->credentials.authentication.ds_data;

    if (config->credentials.authentication.key_password && config->credentials.authentication.key_password_len) {
        client->config->clientkey_password_len = config->credentials.authentication.key_password_len;
        client->config->clientkey_password = malloc(client->config->clientkey_password_len);
        ESP_MEM_CHECK(TAG, client->config->clientkey_password, goto _mqtt_set_config_failed);
        memcpy(client->config->clientkey_password, config->credentials.authentication.key_password, client->config->clientkey_password_len);
    }

    if (config->broker.address.transport) {
        free(client->config->scheme);
        client->config->scheme = NULL;
        if (config->broker.address.transport == MQTT_TRANSPORT_OVER_TCP) {
            client->config->scheme = create_string(MQTT_OVER_TCP_SCHEME, strlen(MQTT_OVER_TCP_SCHEME));
            ESP_MEM_CHECK(TAG, client->config->scheme, goto _mqtt_set_config_failed);
        }
#if MQTT_ENABLE_WS
        else if (config->broker.address.transport == MQTT_TRANSPORT_OVER_WS) {
            client->config->scheme = create_string(MQTT_OVER_WS_SCHEME, strlen(MQTT_OVER_WS_SCHEME));
            ESP_MEM_CHECK(TAG, client->config->scheme, goto _mqtt_set_config_failed);
        }
#endif
#if MQTT_ENABLE_SSL
        else if (config->broker.address.transport == MQTT_TRANSPORT_OVER_SSL) {
            client->config->scheme = create_string(MQTT_OVER_SSL_SCHEME, strlen(MQTT_OVER_SSL_SCHEME));
            ESP_MEM_CHECK(TAG, client->config->scheme, goto _mqtt_set_config_failed);
        }
#endif
#if MQTT_ENABLE_WSS
        else if (config->broker.address.transport == MQTT_TRANSPORT_OVER_WSS) {
            client->config->scheme = create_string(MQTT_OVER_WSS_SCHEME, strlen(MQTT_OVER_WSS_SCHEME));
            ESP_MEM_CHECK(TAG, client->config->scheme, goto _mqtt_set_config_failed);
        }
#endif
    }

    // Set uri at the end of config to override separately configured uri elements
    if (config->broker.address.uri) {
        if (esp_mqtt_client_set_uri(client, client->config->uri) != ESP_OK) {
            err = ESP_FAIL;
            goto _mqtt_set_config_failed;
        }
    }
    client->config->outbox_limit = config->outbox.limit;
    esp_err_t config_has_conflict = esp_mqtt_check_cfg_conflict(client->config, config);

    MQTT_API_UNLOCK(client);

    return config_has_conflict;
_mqtt_set_config_failed:
    esp_mqtt_destroy_config(client);
    MQTT_API_UNLOCK(client);
    return err;
}

void esp_mqtt_destroy_config(esp_mqtt_client_handle_t client)
{
    if (client->config == NULL) {
        return;
    }
    free(client->mqtt_state.in_buffer);
    mqtt_msg_buffer_destroy(&client->mqtt_state.connection);
    free(client->config->host);
    free(client->config->uri);
    free(client->config->path);
    free(client->config->scheme);
    for (int i = 0; i < client->config->num_alpn_protos; i++) {
        free(client->config->alpn_protos[i]);
    }
    free(client->config->alpn_protos);
    free(client->config->clientkey_password);
    free(client->config->if_name);
    free(client->mqtt_state.connection.information.will_topic);
    free(client->mqtt_state.connection.information.will_message);
    free(client->mqtt_state.connection.information.client_id);
    free(client->mqtt_state.connection.information.username);
    free(client->mqtt_state.connection.information.password);
#ifdef MQTT_PROTOCOL_5
    esp_mqtt5_client_destory(client);
#endif
    memset(&client->mqtt_state.connection.information, 0, sizeof(mqtt_connect_info_t));
#ifdef MQTT_SUPPORTED_FEATURE_EVENT_LOOP
    if (client->config->event_loop_handle) {
        esp_event_loop_delete(client->config->event_loop_handle);
    }
#endif
    esp_transport_destroy(client->config->transport);
    memset(client->config, 0, sizeof(mqtt_config_storage_t));
    free(client->config);
    client->config = NULL;
}

static inline bool has_timed_out(uint64_t last_tick, uint64_t timeout)
{
    uint64_t next = last_tick + timeout;
    return (int64_t)(next - platform_tick_get_ms()) <= 0;
}

static esp_err_t process_keepalive(esp_mqtt_client_handle_t client)
{
    if (client->mqtt_state.connection.information.keepalive > 0) {
        const uint64_t keepalive_ms = client->mqtt_state.connection.information.keepalive * 1000;

        if (client->wait_for_ping_resp == true ) {
            if (has_timed_out(client->keepalive_tick, keepalive_ms)) {
                ESP_LOGE(TAG, "No PING_RESP, disconnected");
                esp_mqtt_abort_connection(client);
                client->wait_for_ping_resp = false;
                return ESP_FAIL;
            }
            return ESP_OK;
        }

        if (has_timed_out(client->keepalive_tick, keepalive_ms / 2)) {
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
    int wlen = 0, widx = 0, len = client->mqtt_state.connection.outbound_message.length;
    while (len > 0) {
        wlen = esp_transport_write(client->transport,
                                   (char *)client->mqtt_state.connection.outbound_message.data + widx,
                                   len,
                                   client->config->network_timeout_ms);
        if (wlen < 0) {
            ESP_LOGE(TAG, "Writing failed: errno=%d", errno);
            esp_mqtt_client_dispatch_transport_error(client);
            return ESP_FAIL;
        }

        if (wlen == 0) {
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
    int read_len, connect_rsp_code = 0;
    client->wait_for_ping_resp = false;
    if (client->mqtt_state.connection.information.protocol_ver == MQTT_PROTOCOL_V_5) {
#ifdef MQTT_PROTOCOL_5
        mqtt5_msg_connect(&client->mqtt_state.connection,
                          &client->mqtt_state.connection.information, &client->mqtt5_config->connect_property_info, &client->mqtt5_config->will_property_info);
#endif
    } else {
        mqtt_msg_connect(&client->mqtt_state.connection,
                         &client->mqtt_state.connection.information);
    }
    if (client->mqtt_state.connection.outbound_message.length == 0) {
        ESP_LOGE(TAG, "Connect message cannot be created");
        return ESP_FAIL;
    }

    client->mqtt_state.pending_msg_type = mqtt_get_type(client->mqtt_state.connection.outbound_message.data);
    if (client->mqtt_state.connection.information.protocol_ver == MQTT_PROTOCOL_V_5) {
#ifdef MQTT_PROTOCOL_5
        client->mqtt_state.pending_msg_id = mqtt5_get_id(client->mqtt_state.connection.outbound_message.data,
                                            client->mqtt_state.connection.outbound_message.length);
#endif
    } else {
        client->mqtt_state.pending_msg_id = mqtt_get_id(client->mqtt_state.connection.outbound_message.data,
                                            client->mqtt_state.connection.outbound_message.length);
    }
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

    if (mqtt_get_type(client->mqtt_state.in_buffer) != MQTT_MSG_TYPE_CONNACK) {
        ESP_LOGE(TAG, "Invalid MSG_TYPE response: %d, read_len: %d", mqtt_get_type(client->mqtt_state.in_buffer), read_len);
        return ESP_FAIL;
    }
    if (client->mqtt_state.connection.information.protocol_ver == MQTT_PROTOCOL_V_5) {
#ifdef MQTT_PROTOCOL_5
        if (esp_mqtt5_parse_connack(client, &connect_rsp_code) == ESP_OK) {
            client->send_publish_packet_count = 0;
            return ESP_OK;
        }
#endif
    } else {
        client->mqtt_state.in_buffer_read_len = 0;
        connect_rsp_code = mqtt_get_connect_return_code(client->mqtt_state.in_buffer);
        if (connect_rsp_code == MQTT_CONNECTION_ACCEPTED) {
            ESP_LOGD(TAG, "Connected");
            return ESP_OK;
        }
        switch (connect_rsp_code) {
        case MQTT_CONNECTION_REFUSE_PROTOCOL:
            ESP_LOGW(TAG, "Connection refused, bad protocol");
            break;
        case MQTT_CONNECTION_REFUSE_SERVER_UNAVAILABLE:
            ESP_LOGW(TAG, "Connection refused, server unavailable");
            break;
        case MQTT_CONNECTION_REFUSE_BAD_USERNAME:
            ESP_LOGW(TAG, "Connection refused, bad username or password");
            break;
        case MQTT_CONNECTION_REFUSE_NOT_AUTHORIZED:
            ESP_LOGW(TAG, "Connection refused, not authorized");
            break;
        default:
            ESP_LOGW(TAG, "Connection refused, Unknow reason");
            break;
        }
    }
    /* propagate event with connection refused error */
    client->event.event_id = MQTT_EVENT_ERROR;
    client->event.error_handle->error_type = MQTT_ERROR_TYPE_CONNECTION_REFUSED;
    client->event.error_handle->connect_return_code = connect_rsp_code;
    client->event.error_handle->esp_tls_stack_err = 0;
    client->event.error_handle->esp_tls_last_esp_err = 0;
    client->event.error_handle->esp_tls_cert_verify_flags = 0;
    esp_mqtt_dispatch_event_with_msgid(client);

    return ESP_FAIL;
}

static void esp_mqtt_abort_connection(esp_mqtt_client_handle_t client)
{
    MQTT_API_LOCK(client);
    esp_transport_close(client->transport);
    client->wait_timeout_ms = client->config->reconnect_timeout_ms;
    client->reconnect_tick = platform_tick_get_ms();
    client->state = MQTT_STATE_WAIT_RECONNECT;
    ESP_LOGD(TAG, "Reconnect after %d ms", client->wait_timeout_ms);
    client->event.event_id = MQTT_EVENT_DISCONNECTED;
    client->wait_for_ping_resp = false;
    esp_mqtt_dispatch_event_with_msgid(client);
    MQTT_API_UNLOCK(client);
}

static bool create_client_data(esp_mqtt_client_handle_t client)
{
    client->event.error_handle = calloc(1, sizeof(esp_mqtt_error_codes_t));
    ESP_MEM_CHECK(TAG, client->event.error_handle, return false)

    client->api_lock = xSemaphoreCreateRecursiveMutex();
    ESP_MEM_CHECK(TAG, client->api_lock, return false);

    client->outbox = outbox_init();
    ESP_MEM_CHECK(TAG, client->outbox, return false);
    client->status_bits = xEventGroupCreate();
    ESP_MEM_CHECK(TAG, client->status_bits, return false);

    return true;
}

esp_mqtt_client_handle_t esp_mqtt_client_init(const esp_mqtt_client_config_t *config)
{
    esp_mqtt_client_handle_t client = heap_caps_calloc(1, sizeof(struct esp_mqtt_client),
#if MQTT_EVENT_QUEUE_SIZE > 1
                                      // if supporting multiple queued events, we keep track of them
                                      // using atomic variable, so need to make sure it won't get allocated in PSRAM
                                      MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#else
                                      MALLOC_CAP_DEFAULT);
#endif
    ESP_MEM_CHECK(TAG, client, return NULL);
    if (!create_client_data(client)) {
        goto _mqtt_init_failed;
    }

    if (esp_mqtt_set_config(client, config) != ESP_OK) {
        goto _mqtt_init_failed;
    }
#ifdef MQTT_SUPPORTED_FEATURE_EVENT_LOOP
    esp_event_loop_args_t no_task_loop = {
        .queue_size = MQTT_EVENT_QUEUE_SIZE,
        .task_name = NULL,
    };
    esp_event_loop_create(&no_task_loop, &client->config->event_loop_handle);
#if MQTT_EVENT_QUEUE_SIZE > 1
    atomic_init(&client->queued_events, 0);
#endif
#endif

    client->keepalive_tick = platform_tick_get_ms();
    client->reconnect_tick = platform_tick_get_ms();
    client->refresh_connection_tick = platform_tick_get_ms();
    client->wait_for_ping_resp = false;
#ifdef MQTT_PROTOCOL_5
    if (esp_mqtt5_create_default_config(client) != ESP_OK) {
        goto _mqtt_init_failed;
    }
#endif
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
    if (client->run) {
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
    if (client->api_lock) {
        vSemaphoreDelete(client->api_lock);
    }
    free(client->event.error_handle);
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
// False-positive leak detection. TODO: GCC-366
#pragma GCC diagnostic push  // TODO: IDF-10105
#if __clang__
#pragma clang diagnostic ignored "-Wunknown-warning-option"
#else
#pragma GCC diagnostic ignored "-Wpragmas"
#endif
#pragma GCC diagnostic ignored "-Wanalyzer-malloc-leak"
    free(client->config->scheme);
    free(client->config->host);
    free(client->config->path);

    client->config->scheme = create_string(uri + puri.field_data[UF_SCHEMA].off, puri.field_data[UF_SCHEMA].len);
    client->config->host = create_string(uri + puri.field_data[UF_HOST].off, puri.field_data[UF_HOST].len);
    client->config->path = NULL;
#pragma GCC diagnostic pop

    if (puri.field_data[UF_PATH].len || puri.field_data[UF_QUERY].len) {
        int asprintf_ret_value;
        if (puri.field_data[UF_QUERY].len == 0) {
            asprintf_ret_value = asprintf(&client->config->path,
                    "%.*s",
                    puri.field_data[UF_PATH].len, uri + puri.field_data[UF_PATH].off);
        } else if (puri.field_data[UF_PATH].len == 0)  {
            asprintf_ret_value = asprintf(&client->config->path,
                    "/?%.*s",
                    puri.field_data[UF_QUERY].len, uri + puri.field_data[UF_QUERY].off);
        } else {
            asprintf_ret_value = asprintf(&client->config->path,
                    "%.*s?%.*s",
                    puri.field_data[UF_PATH].len, uri + puri.field_data[UF_PATH].off,
                    puri.field_data[UF_QUERY].len, uri + puri.field_data[UF_QUERY].off);
        }

        if (asprintf_ret_value == -1) {
            ESP_LOGE(TAG,"%s(%d): %s",  __FUNCTION__, __LINE__, "Memory exhausted");
            MQTT_API_UNLOCK(client);
            return ESP_ERR_NO_MEM;
        }
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
            client->mqtt_state.connection.information.password = strdup(pass);
        }
        client->mqtt_state.connection.information.username = strdup(user_info);

        free(user_info);
    }

    MQTT_API_UNLOCK(client);
    return ESP_OK;
}


static esp_err_t esp_mqtt_dispatch_event_with_msgid(esp_mqtt_client_handle_t client)
{
    if (client->mqtt_state.connection.information.protocol_ver == MQTT_PROTOCOL_V_5) {
#ifdef MQTT_PROTOCOL_5
        client->event.msg_id = mqtt5_get_id(client->mqtt_state.in_buffer, client->mqtt_state.in_buffer_length);
#endif
    } else {
        client->event.msg_id = mqtt_get_id(client->mqtt_state.in_buffer, client->mqtt_state.in_buffer_length);
    }
    return esp_mqtt_dispatch_event(client);
}

esp_err_t esp_mqtt_dispatch_custom_event(esp_mqtt_client_handle_t client, esp_mqtt_event_t *event)
{
    esp_err_t ret = esp_event_post_to(client->config->event_loop_handle, MQTT_EVENTS, MQTT_USER_EVENT, event, sizeof(*event), 0);
#if MQTT_EVENT_QUEUE_SIZE > 1
    if (ret == ESP_OK) {
        atomic_fetch_add(&client->queued_events, 1);
    }
#endif
    return ret;
}

static esp_err_t esp_mqtt_dispatch_event(esp_mqtt_client_handle_t client)
{
    client->event.client = client;
    client->event.protocol_ver = client->mqtt_state.connection.information.protocol_ver;
    esp_err_t ret = ESP_FAIL;

#ifdef MQTT_SUPPORTED_FEATURE_EVENT_LOOP
    esp_event_post_to(client->config->event_loop_handle, MQTT_EVENTS, client->event.event_id, &client->event, sizeof(client->event), portMAX_DELAY);
    ret = esp_event_loop_run(client->config->event_loop_handle, 0);
#else
    return ESP_FAIL;
#endif
    if (client->mqtt_state.connection.information.protocol_ver == MQTT_PROTOCOL_V_5) {
#ifdef MQTT_PROTOCOL_5
        esp_mqtt5_client_delete_user_property(client->event.property->user_property);
        client->event.property->user_property = NULL;
#endif
    }
    return ret;
}

static esp_err_t deliver_publish(esp_mqtt_client_handle_t client)
{
    uint8_t *msg_buf = client->mqtt_state.in_buffer;
    size_t msg_read_len = client->mqtt_state.in_buffer_read_len;
    size_t msg_total_len = client->mqtt_state.message_length;
    size_t msg_topic_len = msg_read_len, msg_data_len = msg_read_len;
    size_t msg_data_offset = 0;
    char *msg_topic = NULL, *msg_data = NULL;

    if (client->mqtt_state.connection.information.protocol_ver == MQTT_PROTOCOL_V_5) {
#ifdef MQTT_PROTOCOL_5
        if (esp_mqtt5_get_publish_data(client, msg_buf, msg_read_len, &msg_topic, &msg_topic_len, &msg_data, &msg_data_len) != ESP_OK) {
            ESP_LOGE(TAG, "%s: esp_mqtt5_get_publish_data() failed", __func__);
            return ESP_FAIL;
        }
#endif
    } else {
        // get topic
        msg_topic = mqtt_get_publish_topic(msg_buf, &msg_topic_len);
        if (msg_topic == NULL) {
            ESP_LOGE(TAG, "%s: mqtt_get_publish_topic() failed", __func__);
            return ESP_FAIL;
        }
        ESP_LOGD(TAG, "%s: msg_topic_len=%"NEWLIB_NANO_COMPAT_FORMAT, __func__, NEWLIB_NANO_COMPAT_CAST(msg_topic_len));

        // get payload
        msg_data = mqtt_get_publish_data(msg_buf, &msg_data_len);
        if (msg_data_len > 0 && msg_data == NULL) {
            ESP_LOGE(TAG, "%s: mqtt_get_publish_data() failed", __func__);
            return ESP_FAIL;
        }
    }
    // post data event
    client->event.retain = mqtt_get_retain(msg_buf);
    if (client->mqtt_state.connection.information.protocol_ver == MQTT_PROTOCOL_V_5) {
#ifdef MQTT_PROTOCOL_5
        client->event.msg_id = mqtt5_get_id(msg_buf, msg_read_len);
#endif
    } else {
        client->event.msg_id = mqtt_get_id(msg_buf, msg_read_len);
    }
    client->event.qos = mqtt_get_qos(msg_buf);
    client->event.dup = mqtt_get_dup(msg_buf);
    client->event.total_data_len = msg_data_len + msg_total_len - msg_read_len;
post_data_event:
    ESP_LOGD(TAG, "Get data len= %"NEWLIB_NANO_COMPAT_FORMAT", topic len=%"NEWLIB_NANO_COMPAT_FORMAT", total_data: %d offset: %"NEWLIB_NANO_COMPAT_FORMAT,
             NEWLIB_NANO_COMPAT_CAST(msg_data_len), NEWLIB_NANO_COMPAT_CAST(msg_topic_len),
             client->event.total_data_len, NEWLIB_NANO_COMPAT_CAST(msg_data_offset));
    client->event.event_id = MQTT_EVENT_DATA;
    client->event.data = msg_data_len > 0 ? msg_data : NULL;
    client->event.data_len = msg_data_len;
    client->event.current_data_offset = msg_data_offset;
    client->event.topic = msg_topic;
    client->event.topic_len = msg_topic_len;
    esp_mqtt_dispatch_event(client);

    if (msg_read_len < msg_total_len) {
        size_t buf_len = client->mqtt_state.in_buffer_length;

        msg_data = (char *)client->mqtt_state.in_buffer;
        msg_topic = NULL;
        msg_topic_len = 0;
        msg_data_offset += msg_data_len;
        int ret = esp_transport_read(client->transport, (char *)client->mqtt_state.in_buffer,
                                     msg_total_len - msg_read_len > buf_len ? buf_len : msg_total_len - msg_read_len,
                                     client->config->network_timeout_ms);
        if (ret <= 0) {
            return esp_mqtt_handle_transport_read_error(ret, client, false) == 0 ? ESP_OK : ESP_FAIL;
        }

        msg_data_len = ret;
        msg_read_len += msg_data_len;
        goto post_data_event;
    }
    return ESP_OK;
}

static esp_err_t deliver_suback(esp_mqtt_client_handle_t client)
{
    uint8_t *msg_buf = client->mqtt_state.in_buffer;
    size_t msg_data_len = client->mqtt_state.in_buffer_read_len;
    char *msg_data = NULL;

    if (client->mqtt_state.connection.information.protocol_ver == MQTT_PROTOCOL_V_5) {
#ifdef MQTT_PROTOCOL_5
        msg_data = mqtt5_get_suback_data(msg_buf, &msg_data_len, &client->event.property->user_property);
#else
        // SUBACK Using MQTT5 received but MQTT5 is disabled, This is unlikely to happen.
        return ESP_FAIL;
#endif
    } else {
        msg_data = mqtt_get_suback_data(msg_buf, &msg_data_len);
    }
    if (msg_data_len <= 0) {
        ESP_LOGE(TAG, "Failed to acquire suback data");
        return ESP_FAIL;
    }
    client->event.error_handle->esp_tls_stack_err = 0;
    client->event.error_handle->esp_tls_last_esp_err = 0;
    client->event.error_handle->esp_tls_cert_verify_flags = 0;
    client->event.error_handle->error_type = MQTT_ERROR_TYPE_NONE;
    client->event.error_handle->connect_return_code = MQTT_CONNECTION_ACCEPTED;
    // post data event
    for (int topic = 0; topic < msg_data_len; ++topic) {
        if ((uint8_t)msg_data[topic] >= 0x80) {
            client->event.error_handle->error_type = MQTT_ERROR_TYPE_SUBSCRIBE_FAILED;
            break;
        }
    }
    client->event.data_len = msg_data_len;
    client->event.total_data_len = msg_data_len;
    client->event.event_id = MQTT_EVENT_SUBSCRIBED;
    client->event.data = msg_data;
    client->event.current_data_offset = 0;
    esp_mqtt_dispatch_event_with_msgid(client);

    return ESP_OK;
}

// Deletes the initial message in MQTT communication protocol
// Return false when message is not found, making the received counterpart invalid.
static bool remove_initiator_message(esp_mqtt_client_handle_t client, int msg_type, int msg_id)
{
    if (outbox_delete(client->outbox, msg_id, msg_type) == ESP_OK) {
        ESP_LOGD(TAG, "Removed pending_id=%d", client->mqtt_state.pending_msg_id);
        return true;
    }

    ESP_LOGD(TAG, "Failed to remove pending_id=%d", client->mqtt_state.pending_msg_id);
    return false;
}

static outbox_item_handle_t mqtt_enqueue(esp_mqtt_client_handle_t client, uint8_t *remaining_data, int remaining_len)
{
    ESP_LOGD(TAG, "mqtt_enqueue id: %d, type=%d successful",
             client->mqtt_state.pending_msg_id, client->mqtt_state.pending_msg_type);
    outbox_message_t msg = { 0 };
    msg.data = client->mqtt_state.connection.outbound_message.data;
    msg.len =  client->mqtt_state.connection.outbound_message.length;
    msg.msg_id = client->mqtt_state.pending_msg_id;
    msg.msg_type = client->mqtt_state.pending_msg_type;
    msg.msg_qos = client->mqtt_state.pending_publish_qos;
    msg.remaining_data = remaining_data;
    msg.remaining_len = remaining_len;
    //Copy to queue buffer
    return outbox_enqueue(client->outbox, &msg, platform_tick_get_ms());
}


/*
 * Returns:
 *     -2 in case of failure or EOF (clean connection closure)
 *     -1 timeout while in-the-middle of the messge
 *      0 if no message has been received
 *      1 if a message has been received and placed to client->mqtt_state:
 *           message length:  client->mqtt_state.message_length
 *           message content: client->mqtt_state.in_buffer
 *
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
        if (read_len <= 0) {
            return esp_mqtt_handle_transport_read_error(read_len, client, false);
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
            if (read_len <= 0) {
                return esp_mqtt_handle_transport_read_error(read_len, client, true);
            }
            ESP_LOGD(TAG, "%s: read \"remaining length\" byte: 0x%x", __func__, *buf);
            buf++;
            client->mqtt_state.in_buffer_read_len++;
        } while ((client->mqtt_state.in_buffer_read_len < 6) && (*(buf - 1) & 0x80));
    }
    total_len = mqtt_get_total_length(client->mqtt_state.in_buffer, client->mqtt_state.in_buffer_read_len, &fixed_header_len);
    ESP_LOGD(TAG, "%s: total message length: %d (already read: %"NEWLIB_NANO_COMPAT_FORMAT")", __func__, total_len, NEWLIB_NANO_COMPAT_CAST(client->mqtt_state.in_buffer_read_len));
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
                if (read_len <= 0) {
                    return esp_mqtt_handle_transport_read_error(read_len, client, true);
                }
                client->mqtt_state.in_buffer_read_len += read_len;
                buf += read_len;
                if (client->mqtt_state.in_buffer_read_len < fixed_header_len + 2) {
                    ESP_LOGD(TAG, "%s: transport_read(): message reading left in progress :: total message length: %d (already read: %"NEWLIB_NANO_COMPAT_FORMAT")",
                             __func__, total_len, NEWLIB_NANO_COMPAT_CAST(client->mqtt_state.in_buffer_read_len));
                    return 0;
                }
            }
            int topic_len = client->mqtt_state.in_buffer[fixed_header_len] << 8;
            topic_len |= client->mqtt_state.in_buffer[fixed_header_len + 1];
            total_len = fixed_header_len + topic_len + (mqtt_get_qos(client->mqtt_state.in_buffer) > 0 ? 2 : 0);
            ESP_LOGD(TAG, "%s: total len modified to %d as message longer than input buffer", __func__, total_len);
            if (client->mqtt_state.in_buffer_length < total_len) {
                ESP_LOGE(TAG, "%s: message is too big, insufficient buffer size", __func__);
                goto err;
            } else {
                total_len = client->mqtt_state.in_buffer_length;
            }
            /* free to continue with reading */
        } else {
            ESP_LOGE(TAG, "%s: message is too big, insufficient buffer size", __func__);
            goto err;
        }
    }
    if (client->mqtt_state.in_buffer_read_len < total_len) {
        /* read the rest of the mqtt message */
        read_len = esp_transport_read(t, (char *)buf, total_len - client->mqtt_state.in_buffer_read_len, read_poll_timeout_ms);
        ESP_LOGD(TAG, "%s: read_len=%d", __func__, read_len);
        if (read_len <= 0) {
            return esp_mqtt_handle_transport_read_error(read_len, client, true);
        }
        client->mqtt_state.in_buffer_read_len += read_len;
        if (client->mqtt_state.in_buffer_read_len < total_len) {
            ESP_LOGD(TAG, "%s: transport_read(): message reading left in progress :: total message length: %d (already read: %"NEWLIB_NANO_COMPAT_FORMAT")",
                     __func__, total_len, NEWLIB_NANO_COMPAT_CAST(client->mqtt_state.in_buffer_read_len));
            return 0;
        }
    }
    ESP_LOGV(TAG, "%s: transport_read():%"NEWLIB_NANO_COMPAT_FORMAT" %"NEWLIB_NANO_COMPAT_FORMAT, __func__,
             NEWLIB_NANO_COMPAT_CAST(client->mqtt_state.in_buffer_read_len), NEWLIB_NANO_COMPAT_CAST(client->mqtt_state.message_length));
    return 1;
err:
    esp_mqtt_client_dispatch_transport_error(client);
    return -2;
}

static esp_err_t mqtt_process_receive(esp_mqtt_client_handle_t client)
{
    uint8_t msg_type = 0, msg_qos = 0;
    uint16_t msg_id = 0;
    size_t previous_in_buffer_read_len = client->mqtt_state.in_buffer_read_len;

    /* non-blocking receive in order not to block other tasks */
    int recv = mqtt_message_receive(client, 0);
    if (recv == 0) {    // Timeout
        return ESP_OK;
    }
    if (recv == -1) {    // Mid-message timeout
        if (previous_in_buffer_read_len == client->mqtt_state.in_buffer_read_len) {
            // Report error only if didn't receive anything since previous iteration
            ESP_LOGE(TAG, "%s: Network timeout while reading MQTT message", __func__);
            return ESP_FAIL;
        }
        return ESP_OK;  // Treat as standard timeout (keep reading the message)
    }
    if (recv < 0) {     // Other error
        ESP_LOGE(TAG, "%s: mqtt_message_receive() returned %d", __func__, recv);
        return ESP_FAIL;
    }
    int read_len = client->mqtt_state.message_length;

    // If the message was valid, get the type, quality of service and id of the message
    msg_type = mqtt_get_type(client->mqtt_state.in_buffer);
    msg_qos = mqtt_get_qos(client->mqtt_state.in_buffer);
    if (client->mqtt_state.connection.information.protocol_ver == MQTT_PROTOCOL_V_5) {
#ifdef MQTT_PROTOCOL_5
        msg_id = mqtt5_get_id(client->mqtt_state.in_buffer, read_len);
#endif
    } else {
        msg_id = mqtt_get_id(client->mqtt_state.in_buffer, read_len);
    }

    ESP_LOGD(TAG, "msg_type=%d, msg_id=%d", msg_type, msg_id);

    switch (msg_type) {
    case MQTT_MSG_TYPE_SUBACK:
        if (remove_initiator_message(client, MQTT_MSG_TYPE_SUBSCRIBE, msg_id)) {
#ifdef MQTT_PROTOCOL_5
            esp_mqtt5_parse_suback(client);
#endif
            ESP_LOGD(TAG, "deliver_suback, message_length_read=%"NEWLIB_NANO_COMPAT_FORMAT", message_length=%"NEWLIB_NANO_COMPAT_FORMAT,
                     NEWLIB_NANO_COMPAT_CAST(client->mqtt_state.in_buffer_read_len), NEWLIB_NANO_COMPAT_CAST(client->mqtt_state.message_length));
            if (deliver_suback(client) != ESP_OK) {
                ESP_LOGE(TAG, "Failed to deliver suback message id=%d", msg_id);
                return ESP_FAIL;
            }
        }
        break;
    case MQTT_MSG_TYPE_UNSUBACK:
        if (remove_initiator_message(client, MQTT_MSG_TYPE_UNSUBSCRIBE, msg_id)) {
#ifdef MQTT_PROTOCOL_5
            esp_mqtt5_parse_unsuback(client);
#endif
            ESP_LOGD(TAG, "UnSubscribe successful");
            client->event.event_id = MQTT_EVENT_UNSUBSCRIBED;
            esp_mqtt_dispatch_event_with_msgid(client);
        }
        break;
    case MQTT_MSG_TYPE_PUBLISH:
        ESP_LOGD(TAG, "deliver_publish, message_length_read=%"NEWLIB_NANO_COMPAT_FORMAT", message_length=%"NEWLIB_NANO_COMPAT_FORMAT, NEWLIB_NANO_COMPAT_CAST(client->mqtt_state.in_buffer_read_len), NEWLIB_NANO_COMPAT_CAST(client->mqtt_state.message_length));
        if (deliver_publish(client) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to deliver publish message id=%d", msg_id);
            return ESP_FAIL;
        }
        if (msg_qos == 1 || msg_qos == 2) {
            if (msg_qos == 1) {
                if (client->mqtt_state.connection.information.protocol_ver == MQTT_PROTOCOL_V_5) {
#ifdef MQTT_PROTOCOL_5
                    mqtt5_msg_puback(&client->mqtt_state.connection, msg_id);
#endif
                } else {
                    mqtt_msg_puback(&client->mqtt_state.connection, msg_id);
                }
            } else if (msg_qos == 2) {
                if (client->mqtt_state.connection.information.protocol_ver == MQTT_PROTOCOL_V_5) {
#ifdef MQTT_PROTOCOL_5
                    mqtt5_msg_pubrec(&client->mqtt_state.connection, msg_id);
#endif
                } else {
                    mqtt_msg_pubrec(&client->mqtt_state.connection, msg_id);
                }
            }
            if (client->mqtt_state.connection.outbound_message.length == 0) {
                ESP_LOGE(TAG, "Publish response message PUBACK or PUBREC cannot be created");
                return ESP_FAIL;
            }

            ESP_LOGD(TAG, "Queue response QoS: %d", msg_qos);

            if (esp_mqtt_write(client) != ESP_OK) {
                ESP_LOGE(TAG, "Error write qos msg repsonse, qos = %d", msg_qos);
                return ESP_FAIL;
            }
        }
        break;
    case MQTT_MSG_TYPE_PUBACK:
#ifdef MQTT_PROTOCOL_5
        if (client->mqtt_state.connection.information.protocol_ver == MQTT_PROTOCOL_V_5) {
            esp_mqtt5_decrement_packet_counter(client);
        }
#endif
        if (remove_initiator_message(client, MQTT_MSG_TYPE_PUBLISH, msg_id)) {
            ESP_LOGD(TAG, "received MQTT_MSG_TYPE_PUBACK, finish QoS1 publish");
#ifdef MQTT_PROTOCOL_5
            esp_mqtt5_parse_puback(client);
#endif
            client->event.event_id = MQTT_EVENT_PUBLISHED;
            esp_mqtt_dispatch_event_with_msgid(client);
        }
        break;
    case MQTT_MSG_TYPE_PUBREC:
        ESP_LOGD(TAG, "received MQTT_MSG_TYPE_PUBREC");
        if (client->mqtt_state.connection.information.protocol_ver == MQTT_PROTOCOL_V_5) {
#ifdef MQTT_PROTOCOL_5
            ESP_LOGI(TAG, "MQTT_MSG_TYPE_PUBREC return code is %d", mqtt5_msg_get_reason_code(client->mqtt_state.in_buffer, client->mqtt_state.in_buffer_read_len));
            mqtt5_msg_pubrel(&client->mqtt_state.connection, msg_id);
#endif
        } else {
            mqtt_msg_pubrel(&client->mqtt_state.connection, msg_id);
        }
        if (client->mqtt_state.connection.outbound_message.length == 0) {
            ESP_LOGE(TAG, "Publish response message PUBREL cannot be created");
            return ESP_FAIL;
        }

        outbox_set_pending(client->outbox, msg_id, ACKNOWLEDGED);
        esp_mqtt_write(client);
        break;
    case MQTT_MSG_TYPE_PUBREL:
        ESP_LOGD(TAG, "received MQTT_MSG_TYPE_PUBREL");
        if (client->mqtt_state.connection.information.protocol_ver == MQTT_PROTOCOL_V_5) {
#ifdef MQTT_PROTOCOL_5
            ESP_LOGI(TAG, "MQTT_MSG_TYPE_PUBREL return code is %d", mqtt5_msg_get_reason_code(client->mqtt_state.in_buffer, client->mqtt_state.in_buffer_read_len));
            mqtt5_msg_pubcomp(&client->mqtt_state.connection, msg_id);
#endif
        } else {
            mqtt_msg_pubcomp(&client->mqtt_state.connection, msg_id);
        }
        if (client->mqtt_state.connection.outbound_message.length == 0) {
            ESP_LOGE(TAG, "Publish response message PUBCOMP cannot be created");
            return ESP_FAIL;
        }

        esp_mqtt_write(client);
        break;
    case MQTT_MSG_TYPE_PUBCOMP:
        ESP_LOGD(TAG, "received MQTT_MSG_TYPE_PUBCOMP");
#ifdef MQTT_PROTOCOL_5
        if (client->mqtt_state.connection.information.protocol_ver == MQTT_PROTOCOL_V_5) {
            esp_mqtt5_decrement_packet_counter(client);
        }
#endif
        if (remove_initiator_message(client, MQTT_MSG_TYPE_PUBLISH, msg_id)) {
            ESP_LOGD(TAG, "Receive MQTT_MSG_TYPE_PUBCOMP, finish QoS2 publish");
#ifdef MQTT_PROTOCOL_5
            esp_mqtt5_parse_pubcomp(client);
#endif
            client->event.event_id = MQTT_EVENT_PUBLISHED;
            esp_mqtt_dispatch_event_with_msgid(client);
        }
        break;
    case MQTT_MSG_TYPE_PINGRESP:
        ESP_LOGD(TAG, "MQTT_MSG_TYPE_PINGRESP");
        client->wait_for_ping_resp = false;
        /* It is the responsibility of the Client to ensure that the interval between Control Packets
         * being sent does not exceed the Keep Alive value. In the absence of sending any other Control
         * Packets, the Client MUST send a PINGREQ Packet [MQTT-3.1.2-23].
         * [MQTT-3.1.2-23]
         */
        client->keepalive_tick = platform_tick_get_ms();
        break;
    }

    client->mqtt_state.in_buffer_read_len = 0;
    return ESP_OK;
}

static esp_err_t mqtt_resend_queued(esp_mqtt_client_handle_t client, outbox_item_handle_t item)
{
    // decode queued data
    client->mqtt_state.connection.outbound_message.data = outbox_item_get_data(item, &client->mqtt_state.connection.outbound_message.length, &client->mqtt_state.pending_msg_id,
            &client->mqtt_state.pending_msg_type, &client->mqtt_state.pending_publish_qos);
    // set duplicate flag for QoS-1 and QoS-2 messages
    if (client->mqtt_state.pending_msg_type == MQTT_MSG_TYPE_PUBLISH && client->mqtt_state.pending_publish_qos > 0 && (outbox_item_get_pending(item) == TRANSMITTED)) {
        mqtt_set_dup(client->mqtt_state.connection.outbound_message.data);
        ESP_LOGD(TAG, "Sending Duplicated QoS%d message with id=%d", client->mqtt_state.pending_publish_qos, client->mqtt_state.pending_msg_id);
    }

    // try to resend the data
    if (esp_mqtt_write(client) != ESP_OK) {
        ESP_LOGE(TAG, "Error to resend data ");
        esp_mqtt_abort_connection(client);
        return ESP_FAIL;
    }

    return ESP_OK;
}

static void mqtt_delete_expired_messages(esp_mqtt_client_handle_t client)
{
    // Delete message after OUTBOX_EXPIRED_TIMEOUT_MS milliseconds
#if MQTT_REPORT_DELETED_MESSAGES
    // also report the deleted items as MQTT_EVENT_DELETED events if enabled
    int msg_id = 0;
    while ((msg_id = outbox_delete_single_expired(client->outbox, platform_tick_get_ms(), OUTBOX_EXPIRED_TIMEOUT_MS)) > 0) {
        client->event.event_id = MQTT_EVENT_DELETED;
        client->event.msg_id = msg_id;
        if (esp_mqtt_dispatch_event(client) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to post event on deleting message id=%d", msg_id);
        }
    }
#else
    outbox_delete_expired(client->outbox, platform_tick_get_ms(), OUTBOX_EXPIRED_TIMEOUT_MS);
#endif
}

/**
 * @brief When using multiple queued item, we'd like to reduce the poll timeout to proceed with event loop exacution
 */
static inline int max_poll_timeout(esp_mqtt_client_handle_t client, int max_timeout)
{
    return
#if MQTT_EVENT_QUEUE_SIZE > 1
        atomic_load(&client->queued_events) > 0 ? 10 : max_timeout;
#else
        max_timeout;
#endif
}

static inline void run_event_loop(esp_mqtt_client_handle_t client)
{
#if MQTT_EVENT_QUEUE_SIZE > 1
    if (atomic_load(&client->queued_events) > 0) {
        atomic_fetch_sub(&client->queued_events, 1);
#else
    {
#endif
        esp_err_t ret = esp_event_loop_run(client->config->event_loop_handle, 0);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Error in running event_loop %d", ret);
        }
    }
}

static void esp_mqtt_task(void *pv)
{
    esp_mqtt_client_handle_t client = (esp_mqtt_client_handle_t) pv;
    uint64_t last_retransmit = 0;
    outbox_tick_t msg_tick = 0;
    client->run = true;

    client->state = MQTT_STATE_INIT;
    xEventGroupClearBits(client->status_bits, STOPPED_BIT);
    while (client->run) {
        MQTT_API_LOCK(client);
        run_event_loop(client);
        // delete long pending messages
        mqtt_delete_expired_messages(client);
        mqtt_client_state_t state = client->state;
        switch (state) {
        case MQTT_STATE_DISCONNECTED:
            break;
        case MQTT_STATE_INIT:
            xEventGroupClearBits(client->status_bits, RECONNECT_BIT | DISCONNECT_BIT);
            client->event.event_id = MQTT_EVENT_BEFORE_CONNECT;
            esp_mqtt_dispatch_event_with_msgid(client);


            client->transport = client->config->transport;
            if (!client->transport) {

                if (esp_mqtt_client_create_transport(client) != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to create transport list");
                    client->run = false;
                    break;
                }
                //get transport by scheme
                client->transport = esp_transport_list_get_transport(client->transport_list, client->config->scheme);

                if (client->transport == NULL) {
                    ESP_LOGE(TAG, "There are no transports valid, stop mqtt client, config scheme = %s", client->config->scheme);
                    client->run = false;
                    break;
                }
            }
            //default port
            if (client->config->port == 0) {
                client->config->port = esp_transport_get_default_port(client->transport);
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
            client->event.event_id = MQTT_EVENT_CONNECTED;
            if (client->mqtt_state.connection.information.protocol_ver != MQTT_PROTOCOL_V_5) {
                client->event.session_present = mqtt_get_connect_session_present(client->mqtt_state.in_buffer);
            }
            client->state = MQTT_STATE_CONNECTED;
            esp_mqtt_dispatch_event_with_msgid(client);
            client->refresh_connection_tick = platform_tick_get_ms();
            client->keepalive_tick = platform_tick_get_ms();

            break;
        case MQTT_STATE_CONNECTED:
            // check for disconnection request
            if (xEventGroupWaitBits(client->status_bits, DISCONNECT_BIT, true, true, 0) & DISCONNECT_BIT) {
                send_disconnect_msg(client);    // ignore error, if clean disconnect fails, just abort the connection
                esp_mqtt_abort_connection(client);
                break;
            }
            // receive and process data
            if (mqtt_process_receive(client) == ESP_FAIL) {
                esp_mqtt_abort_connection(client);
                break;
            }

            if (last_retransmit == 0) {
                // connected for first time, set last_retransmit to now, avoid retransmit
                last_retransmit = platform_tick_get_ms();
            }


            // resend all non-transmitted messages first
            outbox_item_handle_t item = outbox_dequeue(client->outbox, QUEUED, NULL);
            if (item) {
                if (mqtt_resend_queued(client, item) == ESP_OK) {
                    if (client->mqtt_state.pending_msg_type == MQTT_MSG_TYPE_PUBLISH && client->mqtt_state.pending_publish_qos == 0) {
                        // delete all qos0 publish messages once we process them
                        if (outbox_delete_item(client->outbox, item) != ESP_OK) {
                            ESP_LOGE(TAG, "Failed to remove queued qos0 message from the outbox");
                        }
                    }
                    if (client->mqtt_state.pending_publish_qos > 0) {
                        outbox_set_pending(client->outbox, client->mqtt_state.pending_msg_id, TRANSMITTED);
#ifdef MQTT_PROTOCOL_5
                        if (client->mqtt_state.connection.information.protocol_ver == MQTT_PROTOCOL_V_5) {
                            esp_mqtt5_increment_packet_counter(client);
                        }
#endif
                    }
                }
                // resend other "transmitted" messages after 1s
            } else if (has_timed_out(last_retransmit, client->config->message_retransmit_timeout)) {
                last_retransmit = platform_tick_get_ms();
                item = outbox_dequeue(client->outbox, TRANSMITTED, &msg_tick);
                if (item && (last_retransmit - msg_tick > client->config->message_retransmit_timeout))  {
                    if (mqtt_resend_queued(client, item) == ESP_OK) {
#ifdef MQTT_PROTOCOL_5
                        if (client->mqtt_state.connection.information.protocol_ver == MQTT_PROTOCOL_V_5) {
                            esp_mqtt5_increment_packet_counter(client);
                        }
#endif
                    }
                }
            }

            if (process_keepalive(client) != ESP_OK) {
                break;
            }

            if (client->config->refresh_connection_after_ms &&
                    has_timed_out(client->refresh_connection_tick, client->config->refresh_connection_after_ms)) {
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
                                max_poll_timeout(client, client->wait_timeout_ms / 2 / portTICK_PERIOD_MS));
            // continue the while loop instead of break, as the mutex is unlocked
            continue;
        default:
            ESP_LOGE(TAG, "MQTT client error, client is in an unrecoverable state.");
            break;
        }
        MQTT_API_UNLOCK(client);
        if (MQTT_STATE_CONNECTED == client->state) {
            if (esp_transport_poll_read(client->transport, max_poll_timeout(client, MQTT_POLL_READ_TIMEOUT_MS)) < 0) {
                ESP_LOGE(TAG, "Poll read error: %d, aborting connection", errno);
                esp_mqtt_abort_connection(client);
            }
        }

    }
    esp_transport_close(client->transport);
    outbox_delete_all_items(client->outbox);
    xEventGroupSetBits(client->status_bits, STOPPED_BIT);
    client->state = MQTT_STATE_DISCONNECTED;

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

static esp_err_t send_disconnect_msg(esp_mqtt_client_handle_t client)
{
    // Notify the broker we are disconnecting
    if (client->mqtt_state.connection.information.protocol_ver == MQTT_PROTOCOL_V_5) {
#ifdef MQTT_PROTOCOL_5
        mqtt5_msg_disconnect(&client->mqtt_state.connection, &client->mqtt5_config->disconnect_property_info);
        if (client->mqtt_state.connection.outbound_message.length) {
            esp_mqtt5_client_delete_user_property(client->mqtt5_config->disconnect_property_info.user_property);
            client->mqtt5_config->disconnect_property_info.user_property = NULL;
            memset(&client->mqtt5_config->disconnect_property_info, 0, sizeof(esp_mqtt5_disconnect_property_config_t));
        }
#endif
    } else {
        mqtt_msg_disconnect(&client->mqtt_state.connection);
    }
    if (client->mqtt_state.connection.outbound_message.length == 0) {
        ESP_LOGE(TAG, "Disconnect message cannot be created");
        return ESP_FAIL;
    }
    if (esp_mqtt_write(client) != ESP_OK) {
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
            if (send_disconnect_msg(client) != ESP_OK) {
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
    mqtt_msg_pingreq(&client->mqtt_state.connection);
    if (client->mqtt_state.connection.outbound_message.length == 0) {
        ESP_LOGE(TAG, "Ping message cannot be created");
        return ESP_FAIL;
    }

    if (esp_mqtt_write(client) != ESP_OK) {
        ESP_LOGE(TAG, "Error sending ping");
        return ESP_FAIL;
    }
    ESP_LOGD(TAG, "Sent PING successful");
    return ESP_OK;
}

int esp_mqtt_client_subscribe_multiple(esp_mqtt_client_handle_t client,
                                       const esp_mqtt_topic_t *topic_list, int size)
{
    if (!client) {
        ESP_LOGE(TAG, "Client was not initialized");
        return -1;
    }

    if (client->config->outbox_limit > 0 && outbox_get_size(client->outbox) > client->config->outbox_limit) {
        return -2;
    }

    if (client->state != MQTT_STATE_CONNECTED) {
        ESP_LOGE(TAG, "Client has not connected");
        return -1;
    }

    MQTT_API_LOCK(client);
    if (client->mqtt_state.connection.information.protocol_ver == MQTT_PROTOCOL_V_5) {
#ifdef MQTT_PROTOCOL_5
        int max_qos = topic_list[0].qos;
        for (int topic_number = 0; topic_number < size; ++topic_number) {
            if (topic_list[topic_number].qos > max_qos) {
                max_qos = topic_list[topic_number].qos;
            }
        }
        if (esp_mqtt5_client_subscribe_check(client, max_qos) != ESP_OK) {
            ESP_LOGI(TAG, "MQTT5 subscribe check fail: QoS %d not accepted by broker ", max_qos);
            MQTT_API_UNLOCK(client);
            return -1;
        }
        mqtt5_msg_subscribe(&client->mqtt_state.connection,
                            topic_list, size,
                            &client->mqtt_state.pending_msg_id, client->mqtt5_config->subscribe_property_info);
        if (client->mqtt_state.connection.outbound_message.length) {
            client->mqtt5_config->subscribe_property_info = NULL;
        }
#endif
    } else {
        mqtt_msg_subscribe(&client->mqtt_state.connection,
                           topic_list, size,
                           &client->mqtt_state.pending_msg_id);
    }
    if (client->mqtt_state.connection.outbound_message.length == 0) {
        ESP_LOGE(TAG, "Subscribe message cannot be created");
        MQTT_API_UNLOCK(client);
        return -1;
    }

    client->mqtt_state.pending_msg_type = mqtt_get_type(client->mqtt_state.connection.outbound_message.data);
    //move pending msg to outbox (if have)
    if (!mqtt_enqueue(client, NULL, 0)) {
        MQTT_API_UNLOCK(client);
        return -1;
    }
    outbox_set_pending(client->outbox, client->mqtt_state.pending_msg_id, TRANSMITTED);// handle error

    if (esp_mqtt_write(client) != ESP_OK) {
        ESP_LOGE(TAG, "Error to send subscribe message, first topic: %s, qos: %d", topic_list[0].filter, topic_list[0].qos);
        MQTT_API_UNLOCK(client);
        return -1;
    }

    ESP_LOGD(TAG, "Sent subscribe, first topic=%s, id: %d", topic_list[0].filter, client->mqtt_state.pending_msg_id);
    MQTT_API_UNLOCK(client);
    return client->mqtt_state.pending_msg_id;

}
int esp_mqtt_client_subscribe_single(esp_mqtt_client_handle_t client, const char *topic, int qos)
{
    esp_mqtt_topic_t user_topic = {.filter = topic, .qos = qos};
    return esp_mqtt_client_subscribe_multiple(client, &user_topic, 1);
}

int esp_mqtt_client_unsubscribe(esp_mqtt_client_handle_t client, const char *topic)
{
    if (!client) {
        ESP_LOGE(TAG, "Client was not initialized");
        return -1;
    }
    if (client->state != MQTT_STATE_CONNECTED) {
        ESP_LOGE(TAG, "Client has not connected");
        return -1;
    }
    MQTT_API_LOCK(client);
    if (client->mqtt_state.connection.information.protocol_ver == MQTT_PROTOCOL_V_5) {
#ifdef MQTT_PROTOCOL_5
        mqtt5_msg_unsubscribe(&client->mqtt_state.connection,
                              topic,
                              &client->mqtt_state.pending_msg_id, client->mqtt5_config->unsubscribe_property_info);
        if (client->mqtt_state.connection.outbound_message.length) {
            client->mqtt5_config->unsubscribe_property_info = NULL;
        }
#endif
    } else {
        mqtt_msg_unsubscribe(&client->mqtt_state.connection,
                             topic,
                             &client->mqtt_state.pending_msg_id);
    }
    if (client->mqtt_state.connection.outbound_message.length == 0) {
        MQTT_API_UNLOCK(client);
        ESP_LOGE(TAG, "Unubscribe message cannot be created");
        return -1;
    }
    ESP_LOGD(TAG, "unsubscribe, topic\"%s\", id: %d", topic, client->mqtt_state.pending_msg_id);

    client->mqtt_state.pending_msg_type = mqtt_get_type(client->mqtt_state.connection.outbound_message.data);
    if (!mqtt_enqueue(client, NULL, 0)) {
        MQTT_API_UNLOCK(client);
        return -1;
    }
    outbox_set_pending(client->outbox, client->mqtt_state.pending_msg_id, TRANSMITTED); //handle error

    if (esp_mqtt_write(client) != ESP_OK) {
        ESP_LOGE(TAG, "Error to unsubscribe topic=%s", topic);
        MQTT_API_UNLOCK(client);
        return -1;
    }

    ESP_LOGD(TAG, "Sent Unsubscribe topic=%s, id: %d, successful", topic, client->mqtt_state.pending_msg_id);
    MQTT_API_UNLOCK(client);
    return client->mqtt_state.pending_msg_id;
}

static int make_publish(esp_mqtt_client_handle_t client, const char *topic, const char *data,
                        int len, int qos, int retain)
{
    uint16_t pending_msg_id = 0;
    if (client->mqtt_state.connection.information.protocol_ver == MQTT_PROTOCOL_V_5) {
#ifdef MQTT_PROTOCOL_5
        mqtt5_msg_publish(&client->mqtt_state.connection,
                          topic, data, len,
                          qos, retain,
                          &pending_msg_id, client->mqtt5_config->publish_property_info, client->mqtt5_config->server_resp_property_info.response_info);
        if (client->mqtt_state.connection.outbound_message.length) {
            client->mqtt5_config->publish_property_info = NULL;
        }
#endif
    } else {
        mqtt_msg_publish(&client->mqtt_state.connection,
                         topic, data, len,
                         qos, retain,
                         &pending_msg_id);
    }

    if (client->mqtt_state.connection.outbound_message.length == 0) {
        ESP_LOGE(TAG, "Publish message cannot be created");
        return -1;
    }
    return pending_msg_id;
}
static inline int mqtt_client_enqueue_publish(esp_mqtt_client_handle_t client, const char *topic, const char *data,
        int len, int qos, int retain, bool store)
{
    int pending_msg_id = make_publish(client, topic, data, len, qos, retain);
    if (pending_msg_id < 0) {
        return -1;
    }
    /* We have to set as pending all the qos>0 messages */
    //TODO: client->mqtt_state.outbound_message = publish_msg;
    if (qos > 0 || store) {
        client->mqtt_state.pending_msg_type = mqtt_get_type(client->mqtt_state.connection.outbound_message.data);
        client->mqtt_state.pending_msg_id = pending_msg_id;
        client->mqtt_state.pending_publish_qos = qos;
        // by default store as QUEUED (not transmitted yet) only for messages which would fit outbound buffer
        if (client->mqtt_state.connection.outbound_message.fragmented_msg_total_length == 0) {
            if (!mqtt_enqueue(client, NULL, 0)) {
                return -1;
            }
        } else {
            int first_fragment = client->mqtt_state.connection.outbound_message.length - client->mqtt_state.connection.outbound_message.fragmented_msg_data_offset;
            if (!mqtt_enqueue(client, ((uint8_t *)data) + first_fragment, len - first_fragment)) {
                return -1;
            }
            client->mqtt_state.connection.outbound_message.fragmented_msg_total_length = 0;
        }
    }
    return pending_msg_id;
}

int esp_mqtt_client_publish(esp_mqtt_client_handle_t client, const char *topic, const char *data, int len, int qos, int retain)
{
    if (!client) {
        ESP_LOGE(TAG, "Client was not initialized");
        return -1;
    }
#if MQTT_SKIP_PUBLISH_IF_DISCONNECTED
    if (client->state != MQTT_STATE_CONNECTED) {
        ESP_LOGI(TAG, "Publishing skipped: client is not connected");
        return -1;
    }
#endif

MQTT_API_LOCK(client);
#ifdef MQTT_PROTOCOL_5
    if (client->mqtt_state.connection.information.protocol_ver == MQTT_PROTOCOL_V_5) {
        if (esp_mqtt5_client_publish_check(client, qos, retain) != ESP_OK) {
            ESP_LOGI(TAG, "MQTT5 publish check fail");
            MQTT_API_UNLOCK(client);
            return -1;
        }
    }
#endif

    /* Acceptable publish messages:
        data == NULL, len == 0: publish null message
        data valid,   len == 0: publish all data, payload len is determined from string length
        data valid,   len >  0: publish data with defined length
     */
    if (len <= 0 && data != NULL) {
        len = strlen(data);
    }

    if (client->config->outbox_limit > 0 && qos > 0) {
        if (len + outbox_get_size(client->outbox) > client->config->outbox_limit) {
            MQTT_API_UNLOCK(client);
            return -2;
        }
    }

    int pending_msg_id = mqtt_client_enqueue_publish(client, topic, data, len, qos, retain, false);
    if (pending_msg_id < 0) {
        MQTT_API_UNLOCK(client);
        return -1;
    }
    int ret = 0;

    /* Skip sending if not connected (rely on resending) */
    if (client->state != MQTT_STATE_CONNECTED) {
        ESP_LOGD(TAG, "Publish: client is not connected");
        if (qos > 0) {
            ret = pending_msg_id;
        } else {
            ret = -1;
        }

        // delete long pending messages
        mqtt_delete_expired_messages(client);

        goto cannot_publish;
    }

    /* Provide support for sending fragmented message if it doesn't fit buffer */
    int remaining_len = len;
    const char *current_data = data;
    bool sending = true;

    while (sending)  {

        if (esp_mqtt_write(client) != ESP_OK) {
            esp_mqtt_abort_connection(client);
            ret = -1;
            goto cannot_publish;
        }

        int data_sent = client->mqtt_state.connection.outbound_message.length - client->mqtt_state.connection.outbound_message.fragmented_msg_data_offset;
        client->mqtt_state.connection.outbound_message.fragmented_msg_data_offset = 0;
        client->mqtt_state.connection.outbound_message.fragmented_msg_total_length = 0;
        remaining_len -= data_sent;
        current_data +=  data_sent;

        if (remaining_len > 0) {
            mqtt_connection_t *connection = &client->mqtt_state.connection;
            ESP_LOGD(TAG, "Sending fragmented message, remains to send %d bytes of %d", remaining_len, len);
            int write_len = remaining_len > connection->buffer_length ? connection->buffer_length : remaining_len;
            memcpy(connection->buffer, current_data, write_len);
            connection->outbound_message.data = connection->buffer;
            connection->outbound_message.length = write_len;
            sending = true;
        } else {
            // Message was sent correctly
            sending = false;
        }
    }

    if (qos > 0) {
#ifdef MQTT_PROTOCOL_5
        if (client->mqtt_state.connection.information.protocol_ver == MQTT_PROTOCOL_V_5) {
            esp_mqtt5_increment_packet_counter(client);
        }
#endif
        //Tick is set after transmit to avoid retransmitting too early due slow network speed / big messages
        outbox_set_tick(client->outbox, pending_msg_id, platform_tick_get_ms());
        outbox_set_pending(client->outbox, pending_msg_id, TRANSMITTED);
    }
    MQTT_API_UNLOCK(client);
    return pending_msg_id;

cannot_publish:
    // clear out possible fragmented publish if failed or skipped
    client->mqtt_state.connection.outbound_message.fragmented_msg_total_length = 0;
    if (qos == 0) {
        ESP_LOGW(TAG, "Publish: Losing qos0 data when client not connected");
    }
    MQTT_API_UNLOCK(client);

    return ret;
}

int esp_mqtt_client_enqueue(esp_mqtt_client_handle_t client, const char *topic, const char *data, int len, int qos, int retain, bool store)
{
    if (!client) {
        ESP_LOGE(TAG, "Client was not initialized");
        return -1;
    }

    /* Acceptable publish messages:
        data == NULL, len == 0: publish null message
        data valid,   len == 0: publish all data, payload len is determined from string length
        data valid,   len >  0: publish data with defined length
     */
    if (len <= 0 && data != NULL) {
        len = strlen(data);
    }

    if (client->config->outbox_limit > 0) {
        if (len + outbox_get_size(client->outbox) > client->config->outbox_limit) {
            return -2;
        }

    }

    MQTT_API_LOCK(client);
#ifdef MQTT_PROTOCOL_5
    if (client->mqtt_state.connection.information.protocol_ver == MQTT_PROTOCOL_V_5) {
        if (esp_mqtt5_client_publish_check(client, qos, retain) != ESP_OK) {
            ESP_LOGI(TAG, "esp_mqtt_client_enqueue check fail");
            MQTT_API_UNLOCK(client);
            return -1;
        }
    }
#endif
    int ret = mqtt_client_enqueue_publish(client, topic, data, len, qos, retain, store);
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

    return esp_event_handler_register_with(client->config->event_loop_handle, MQTT_EVENTS, event, event_handler, event_handler_arg);
#else
    ESP_LOGE(TAG, "Registering event handler while event loop not available in IDF version %s", IDF_VER);
    return ESP_FAIL;
#endif
}

esp_err_t esp_mqtt_client_unregister_event(esp_mqtt_client_handle_t client, esp_mqtt_event_id_t event, esp_event_handler_t event_handler)
{
    if (client == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
#ifdef MQTT_SUPPORTED_FEATURE_EVENT_LOOP

    return esp_event_handler_unregister_with(client->config->event_loop_handle, MQTT_EVENTS, event, event_handler);
#else
    ESP_LOGE(TAG, "Unregistering event handler while event loop not available in IDF version %s", IDF_VER);
    return ESP_FAIL;
#endif
}

static void esp_mqtt_client_dispatch_transport_error(esp_mqtt_client_handle_t client)
{
    client->event.event_id = MQTT_EVENT_ERROR;
    client->event.error_handle->error_type = MQTT_ERROR_TYPE_TCP_TRANSPORT;
    client->event.error_handle->connect_return_code = 0;
#ifdef MQTT_SUPPORTED_FEATURE_TRANSPORT_ERR_REPORTING
    client->event.error_handle->esp_tls_last_esp_err = esp_tls_get_and_clear_last_error(esp_transport_get_error_handle(client->transport),
            &client->event.error_handle->esp_tls_stack_err,
            &client->event.error_handle->esp_tls_cert_verify_flags);
#ifdef MQTT_SUPPORTED_FEATURE_TRANSPORT_SOCK_ERRNO_REPORTING
    client->event.error_handle->esp_transport_sock_errno = esp_transport_get_errno(client->transport);
#endif
#endif
    esp_mqtt_dispatch_event_with_msgid(client);
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
