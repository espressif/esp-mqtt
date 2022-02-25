/*
 * This file is subject to the terms and conditions defined in
 * file 'LICENSE', which is part of this source code package.
 * Tuan PM <tuanpm at live dot com>
 */

#ifndef _MQTT_CLIENT_H_
#define _MQTT_CLIENT_H_

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "esp_err.h"
#include "esp_event.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef ESP_EVENT_DECLARE_BASE
// Define event loop types if macros not available
typedef void *esp_event_loop_handle_t;
typedef void *esp_event_handler_t;
#endif

typedef struct esp_mqtt_client *esp_mqtt_client_handle_t;

/**
 * @brief MQTT event types.
 *
 * User event handler receives context data in `esp_mqtt_event_t` structure with
 *  - `user_context` - user data from `esp_mqtt_client_config_t`
 *  - `client` - mqtt client handle
 *  - various other data depending on event type
 *
 */
typedef enum esp_mqtt_event_id_t {
    MQTT_EVENT_ANY = -1,
    MQTT_EVENT_ERROR = 0,          /*!< on error event, additional context: connection return code, error handle from esp_tls (if supported) */
    MQTT_EVENT_CONNECTED,          /*!< connected event, additional context: session_present flag */
    MQTT_EVENT_DISCONNECTED,       /*!< disconnected event */
    MQTT_EVENT_SUBSCRIBED,         /*!< subscribed event, additional context:
                                        - msg_id               message id
                                        - data                 pointer to the received data
                                        - data_len             length of the data for this event
                                        */
    MQTT_EVENT_UNSUBSCRIBED,       /*!< unsubscribed event */
    MQTT_EVENT_PUBLISHED,          /*!< published event, additional context:  msg_id */
    MQTT_EVENT_DATA,               /*!< data event, additional context:
                                        - msg_id               message id
                                        - topic                pointer to the received topic
                                        - topic_len            length of the topic
                                        - data                 pointer to the received data
                                        - data_len             length of the data for this event
                                        - current_data_offset  offset of the current data for this event
                                        - total_data_len       total length of the data received
                                        - retain               retain flag of the message
                                        - qos                  qos level of the message
                                        - dup                  dup flag of the message
                                        Note: Multiple MQTT_EVENT_DATA could be fired for one message, if it is
                                        longer than internal buffer. In that case only first event contains topic
                                        pointer and length, other contain data only with current data length
                                        and current data offset updating.
                                         */
    MQTT_EVENT_BEFORE_CONNECT,     /*!< The event occurs before connecting */
    MQTT_EVENT_DELETED,            /*!< Notification on delete of one message from the internal outbox,
                                        if the message couldn't have been sent and acknowledged before expiring
                                        defined in OUTBOX_EXPIRED_TIMEOUT_MS.
                                        (events are not posted upon deletion of successfully acknowledged messages)
                                        - This event id is posted only if MQTT_REPORT_DELETED_MESSAGES==1
                                        - Additional context: msg_id (id of the deleted message).
                                        */
} esp_mqtt_event_id_t;

/**
 * MQTT connection error codes propagated via ERROR event
 */
typedef enum esp_mqtt_connect_return_code_t {
    MQTT_CONNECTION_ACCEPTED = 0,                   /*!< Connection accepted  */
    MQTT_CONNECTION_REFUSE_PROTOCOL,                /*!< MQTT connection refused reason: Wrong protocol */
    MQTT_CONNECTION_REFUSE_ID_REJECTED,             /*!< MQTT connection refused reason: ID rejected */
    MQTT_CONNECTION_REFUSE_SERVER_UNAVAILABLE,      /*!< MQTT connection refused reason: Server unavailable */
    MQTT_CONNECTION_REFUSE_BAD_USERNAME,            /*!< MQTT connection refused reason: Wrong user */
    MQTT_CONNECTION_REFUSE_NOT_AUTHORIZED           /*!< MQTT connection refused reason: Wrong username or password */
} esp_mqtt_connect_return_code_t;

/**
 * MQTT connection error codes propagated via ERROR event
 */
typedef enum esp_mqtt_error_type_t {
    MQTT_ERROR_TYPE_NONE = 0,
    MQTT_ERROR_TYPE_TCP_TRANSPORT,
    MQTT_ERROR_TYPE_CONNECTION_REFUSED,
} esp_mqtt_error_type_t;

/**
 * MQTT_ERROR_TYPE_TCP_TRANSPORT error type hold all sorts of transport layer errors,
 * including ESP-TLS error, but in the past only the errors from MQTT_ERROR_TYPE_ESP_TLS layer
 * were reported, so the ESP-TLS error type is re-defined here for backward compatibility
 */
#define MQTT_ERROR_TYPE_ESP_TLS MQTT_ERROR_TYPE_TCP_TRANSPORT

typedef enum esp_mqtt_transport_t {
    MQTT_TRANSPORT_UNKNOWN = 0x0,
    MQTT_TRANSPORT_OVER_TCP,      /*!< MQTT over TCP, using scheme: ``mqtt`` */
    MQTT_TRANSPORT_OVER_SSL,      /*!< MQTT over SSL, using scheme: ``mqtts`` */
    MQTT_TRANSPORT_OVER_WS,       /*!< MQTT over Websocket, using scheme:: ``ws`` */
    MQTT_TRANSPORT_OVER_WSS       /*!< MQTT over Websocket Secure, using scheme: ``wss`` */
} esp_mqtt_transport_t;

/**
 *  MQTT protocol version used for connection
 */
typedef enum esp_mqtt_protocol_ver_t {
    MQTT_PROTOCOL_UNDEFINED = 0,
    MQTT_PROTOCOL_V_3_1,
    MQTT_PROTOCOL_V_3_1_1,
    MQTT_PROTOCOL_V_5,
} esp_mqtt_protocol_ver_t;

#ifdef CONFIG_MQTT_PROTOCOL_5
/**
 *  MQTT5 protocol error reason code, more details refer to MQTT5 protocol document section 2.4
 */
enum mqtt5_error_reason_code {
    MQTT5_UNSPECIFIED_ERROR                      = 0x80,
    MQTT5_MALFORMED_PACKET                       = 0x81,
    MQTT5_PROTOCOL_ERROR                         = 0x82,
    MQTT5_IMPLEMENT_SPECIFIC_ERROR               = 0x83,
    MQTT5_UNSUPPORTED_PROTOCOL_VER               = 0x84,
    MQTT5_INVAILD_CLIENT_ID                      = 0x85,
    MQTT5_BAD_USERNAME_OR_PWD                    = 0x86,
    MQTT5_NOT_AUTHORIZED                         = 0x87,
    MQTT5_SERVER_UNAVAILABLE                     = 0x88,
    MQTT5_SERVER_BUSY                            = 0x89,
    MQTT5_BANNED                                 = 0x8A,
    MQTT5_SERVER_SHUTTING_DOWN                   = 0x8B,
    MQTT5_BAD_AUTH_METHOD                        = 0x8C,
    MQTT5_KEEP_ALIVE_TIMEOUT                     = 0x8D,
    MQTT5_SESSION_TAKEN_OVER                     = 0x8E,
    MQTT5_TOPIC_FILTER_INVAILD                   = 0x8F,
    MQTT5_TOPIC_NAME_INVAILD                     = 0x90,
    MQTT5_PACKET_IDENTIFIER_IN_USE               = 0x91,
    MQTT5_PACKET_IDENTIFIER_NOT_FOUND            = 0x92,
    MQTT5_RECEIVE_MAXIMUM_EXCEEDED               = 0x93,
    MQTT5_TOPIC_ALIAS_INVAILD                    = 0x94,
    MQTT5_PACKET_TOO_LARGE                       = 0x95,
    MQTT5_MESSAGE_RATE_TOO_HIGH                  = 0x96,
    MQTT5_QUOTA_EXCEEDED                         = 0x97,
    MQTT5_ADMINISTRATIVE_ACTION                  = 0x98,
    MQTT5_PAYLOAD_FORMAT_INVAILD                 = 0x99,
    MQTT5_RETAIN_NOT_SUPPORT                     = 0x9A,
    MQTT5_QOS_NOT_SUPPORT                        = 0x9B,
    MQTT5_USE_ANOTHER_SERVER                     = 0x9C,
    MQTT5_SERVER_MOVED                           = 0x9D,
    MQTT5_SHARED_SUBSCR_NOT_SUPPORTED            = 0x9E,
    MQTT5_CONNECTION_RATE_EXCEEDED               = 0x9F,
    MQTT5_MAXIMUM_CONNECT_TIME                   = 0xA0,
    MQTT5_SUBSCRIBE_IDENTIFIER_NOT_SUPPORT       = 0xA1,
    MQTT5_WILDCARD_SUBSCRIBE_NOT_SUPPORT         = 0xA2,
};

/**
 *  MQTT5 user property handle
 */
typedef struct mqtt5_user_property_list_t *mqtt5_user_property_handle_t;

/**
 *  MQTT5 protocol connect properties and will properties configuration, more details refer to MQTT5 protocol document section 3.1.2.11 and 3.3.2.3
 */
typedef struct {
    uint32_t session_expiry_interval;            /*!< The interval time of session expiry */ 
    uint32_t maximum_packet_size;                /*!< The maximum packet size that we can receive */
    uint16_t receive_maximum;                    /*!< The maximum pakcket count that we process concurrently */
    uint16_t topic_alias_maximum;                /*!< The maximum topic alias that we support */
    bool request_resp_info;                      /*!< This value to request Server to return Response information */
    bool request_problem_info;                   /*!< This value to indicate whether the reason string or user properties are sent in case of failures */
    mqtt5_user_property_handle_t user_property;  /*!< The handle for user property, call function esp_mqtt5_client_set_user_property to set it */
    uint32_t will_delay_interval;                /*!< The time interval that server delays publishing will message  */
    uint32_t message_expiry_interval;            /*!< The time interval that message expiry */
    bool payload_format_indicator;               /*!< This value is to indicator will message payload format */
    const char *content_type;                    /*!< This value is to indicator will message content type, use a MIME content type string */
    const char *response_topic;                  /*!< Topic name for a response message */
    const char *correlation_data;                /*!< Binary data for receiver to match the response message */
    uint16_t correlation_data_len;               /*!< The length of correlation data */
    mqtt5_user_property_handle_t will_user_property;  /*!< The handle for will message user property, call function esp_mqtt5_client_set_user_property to set it */
} esp_mqtt5_connection_property_config_t;

/**
 *  MQTT5 protocol publish properties configuration, more details refer to MQTT5 protocol document section 3.3.2.3
 */
typedef struct {
    bool payload_format_indicator;               /*!< This value is to indicator publish message payload format */
    uint32_t message_expiry_interval;            /*!< The time interval that message expiry */
    uint16_t topic_alias;                        /*!< An interger value to identify the topic instead of using topic name string */
    const char *response_topic;                  /*!< Topic name for a response message */
    const char *correlation_data;                /*!< Binary data for receiver to match the response message */
    uint16_t correlation_data_len;               /*!< The length of correlation data */
    const char *content_type;                    /*!< This value is to indicator publish message content type, use a MIME content type string */
    mqtt5_user_property_handle_t user_property;  /*!< The handle for user property, call function esp_mqtt5_client_set_user_property to set it */
} esp_mqtt5_publish_property_config_t;

/**
 *  MQTT5 protocol subscribe properties configuration, more details refer to MQTT5 protocol document section 3.8.2.1
 */
typedef struct {
    uint16_t subscribe_id;                       /*!< A variable byte represents the identifier of the subscription */
    bool no_local_flag;                          /*!< Subscription Option to allow that server publish message that client sent */
    bool retain_as_published_flag;               /*!< Subscription Option to keep the retain flag as published option */
    uint8_t retain_handle;                       /*!< Subscription Option to handle retain option */
    bool is_share_subscribe;                     /*!< Whether subscribe is a shared subscription */
    const char *share_name;                      /*!< The name of shared subscription which is a part of $share/{share_name}/{topic} */
    mqtt5_user_property_handle_t user_property;  /*!< The handle for user property, call function esp_mqtt5_client_set_user_property to set it */
} esp_mqtt5_subscribe_property_config_t;

/**
 *  MQTT5 protocol unsubscribe properties configuration, more details refer to MQTT5 protocol document section 3.10.2.1
 */
typedef struct {
    bool is_share_subscribe;                     /*!< Whether subscribe is a shared subscription */
    const char *share_name;                      /*!< The name of shared subscription which is a part of $share/{share_name}/{topic} */
    mqtt5_user_property_handle_t user_property;  /*!< The handle for user property, call function esp_mqtt5_client_set_user_property to set it */
} esp_mqtt5_unsubscribe_property_config_t;

/**
 *  MQTT5 protocol disconnect properties configuration, more details refer to MQTT5 protocol document section 3.14.2.2
 */
typedef struct {
    uint32_t session_expiry_interval;            /*!< The interval time of session expiry */
    uint8_t disconnect_reason;                   /*!< The reason that connection disconnet, refer to mqtt5_error_reason_code */
    mqtt5_user_property_handle_t user_property;  /*!< The handle for user property, call function esp_mqtt5_client_set_user_property to set it */
} esp_mqtt5_disconnect_property_config_t;

/**
 *  MQTT5 protocol for event properties
 */
typedef struct {
    bool payload_format_indicator;      /*!< Payload format of the message */
    char *response_topic;               /*!< Response topic of the message */
    int response_topic_len;             /*!< Response topic length of the message */
    char *correlation_data;             /*!< Correlation data of the message */
    uint16_t correlation_data_len;      /*!< Correlation data length of the message */
    char *content_type;                 /*!< Content type of the message */
    int content_type_len;               /*!< Content type length of the message */
    mqtt5_user_property_handle_t user_property;  /*!< The handle for user property, call function esp_mqtt5_client_delete_user_property to free the memory */
} esp_mqtt5_event_property_t;

/**
 *  MQTT5 protocol for user property
 */
typedef struct {
    const char *key;                       /*!< Item key name */
    const char *value;                     /*!< Item value string */
} esp_mqtt5_user_property_item_t;
#endif

/**
 * @brief MQTT error code structure to be passed as a contextual information into ERROR event
 *
 * Important: This structure extends `esp_tls_last_error` error structure and is backward compatible with it
 * (so might be down-casted and treated as `esp_tls_last_error` error, but recommended to update applications if used this way previously)
 *
 * Use this structure directly checking error_type first and then appropriate error code depending on the source of the error:
 *
 * | error_type | related member variables | note |
 * | MQTT_ERROR_TYPE_TCP_TRANSPORT | esp_tls_last_esp_err, esp_tls_stack_err, esp_tls_cert_verify_flags, sock_errno | Error reported from tcp_transport/esp-tls |
 * | MQTT_ERROR_TYPE_CONNECTION_REFUSED | connect_return_code | Internal error reported from MQTT broker on connection |
 */
typedef struct esp_mqtt_error_codes {
    /* compatible portion of the struct corresponding to struct esp_tls_last_error */
    esp_err_t esp_tls_last_esp_err;              /*!< last esp_err code reported from esp-tls component */
    int       esp_tls_stack_err;                 /*!< tls specific error code reported from underlying tls stack */
    int       esp_tls_cert_verify_flags;         /*!< tls flags reported from underlying tls stack during certificate verification */
    /* esp-mqtt specific structure extension */
    esp_mqtt_error_type_t error_type;            /*!< error type referring to the source of the error */
    esp_mqtt_connect_return_code_t connect_return_code; /*!< connection refused error code reported from MQTT broker on connection */
    /* tcp_transport extension */
    int       esp_transport_sock_errno;         /*!< errno from the underlying socket */

} esp_mqtt_error_codes_t;

/**
 * MQTT event configuration structure
 */
typedef struct esp_mqtt_event_t {
    esp_mqtt_event_id_t event_id;       /*!< MQTT event type */
    esp_mqtt_client_handle_t client;    /*!< MQTT client handle for this event */
    void *user_context;                 /*!< User context passed from MQTT client config */
    char *data;                         /*!< Data associated with this event */
    int data_len;                       /*!< Length of the data for this event */
    int total_data_len;                 /*!< Total length of the data (longer data are supplied with multiple events) */
    int current_data_offset;            /*!< Actual offset for the data associated with this event */
    char *topic;                        /*!< Topic associated with this event */
    int topic_len;                      /*!< Length of the topic for this event associated with this event */
    int msg_id;                         /*!< MQTT messaged id of message */
    int session_present;                /*!< MQTT session_present flag for connection event */
    esp_mqtt_error_codes_t *error_handle; /*!< esp-mqtt error handle including esp-tls errors as well as internal mqtt errors */
    bool retain;                        /*!< Retained flag of the message associated with this event */
    int qos;                            /*!< qos of the messages associated with this event */
    bool dup;                           /*!< dup flag of the message associated with this event */
    esp_mqtt_protocol_ver_t protocol_ver;   /*!< MQTT protocol version used for connection, defaults to value from menuconfig*/
#ifdef CONFIG_MQTT_PROTOCOL_5
    esp_mqtt5_event_property_t *property; /*!< MQTT 5 property associated with this event */
#endif
} esp_mqtt_event_t;

typedef esp_mqtt_event_t *esp_mqtt_event_handle_t;

typedef esp_err_t (* mqtt_event_callback_t)(esp_mqtt_event_handle_t event);

/**
 * MQTT client configuration structure
 */
typedef struct esp_mqtt_client_config_t {
    const char *uri;                        /*!< Complete MQTT broker URI, have precedence over all broker address configuration */
    const char *host;                       /*!< MQTT broker domain, to set ipv4 pass it as string) */
    esp_mqtt_transport_t transport;         /*!< Selects transport, is overrided by URI transport */
    const char *path;                       /*!< Path in the URI, if seting the fields separately, instead of using uri field
                                              in a Websocket connection:
                                              host = "domain.name"
                                              transport = MQTT_TRANSPORT_OVER_WSS
                                              path = "/websocket_broker"
                                            */
    uint32_t port;                          /*!< MQTT broker port */
    bool          use_global_ca_store;      /*!< Use a global ca_store, look esp-tls documentation for details. */
    esp_err_t (*crt_bundle_attach)(void *conf); /*!< Pointer to ESP x509 Certificate Bundle attach function for the usage of certification bundles in mqtts */
    const char *cert_pem;                   /*!< Pointer to certificate data in PEM or DER format for server verify (with SSL), default is NULL, not required to verify the server. PEM-format must have a terminating NULL-character. DER-format requires the length to be passed in cert_len. */
    size_t cert_len;                        /*!< Length of the buffer pointed to by cert_pem. May be 0 for null-terminated pem */
    const struct psk_key_hint *psk_hint_key;     /*!< Pointer to PSK struct defined in esp_tls.h to enable PSK authentication (as alternative to certificate verification). If not NULL and server certificates are NULL, PSK is enabled */
    bool skip_cert_common_name_check;       /*!< Skip any validation of server certificate CN field, this reduces the security of TLS and makes the mqtt client susceptible to MITM attacks  */
    const char **alpn_protos;               /*!< NULL-terminated list of supported application protocols to be used for ALPN */
    const char *username;                   /*!< MQTT username */
    const char *password;                   /*!< MQTT password */
    const char *client_cert_pem;            /*!< Pointer to certificate data in PEM or DER format for SSL mutual authentication, default is NULL, not required if mutual authentication is not needed. If it is not NULL, also `client_key_pem` has to be provided. PEM-format must have a terminating NULL-character. DER-format requires the length to be passed in client_cert_len. */
    size_t client_cert_len;                 /*!< Length of the buffer pointed to by client_cert_pem. May be 0 for null-terminated pem */
    const char *client_key_pem;             /*!< Pointer to private key data in PEM or DER format for SSL mutual authentication, default is NULL, not required if mutual authentication is not needed. If it is not NULL, also `client_cert_pem` has to be provided. PEM-format must have a terminating NULL-character. DER-format requires the length to be passed in client_key_len */
    size_t client_key_len;                  /*!< Length of the buffer pointed to by client_key_pem. May be 0 for null-terminated pem */
    const char *clientkey_password;         /*!< Client key decryption password string */
    int clientkey_password_len;             /*!< String length of the password pointed to by clientkey_password */
    bool use_secure_element;                /*!< enable secure element for enabling SSL connection */
    void *ds_data;                          /*!< carrier of handle for digital signature parameters */
    bool set_null_client_id;                /*!< Selects a NULL client id */
    const char *client_id;                  /*!< Set client id.
                                            Ignored if set_null_client_id == true
                                            If NULL set the default client id.
                                            Default client id is ``ESP32_%CHIPID%`` where %CHIPID% are last 3 bytes of MAC address in hex format */
    mqtt_event_callback_t event_handle;     /*!< handle for MQTT events as a callback in legacy mode */
    esp_event_loop_handle_t event_loop_handle; /*!< handle for MQTT event loop library */
    int task_prio;                          /*!< MQTT task priority, default is 5, can be changed in ``make menuconfig`` */
    int task_stack;                         /*!< MQTT task stack size, default is 6144 bytes, can be changed in ``make menuconfig`` */
    const char *lwt_topic;                  /*!< LWT (Last Will and Testament) message topic (NULL by default) */
    const char *lwt_msg;                    /*!< LWT message (NULL by default) */
    int lwt_qos;                            /*!< LWT message qos */
    int lwt_retain;                         /*!< LWT retained message flag */
    int lwt_msg_len;                        /*!< LWT message length */
    int disable_clean_session;              /*!< mqtt clean session, default clean_session is true */
    int keepalive;                          /*!< mqtt keepalive, default is 120 seconds */
    bool disable_keepalive;                 /*!< Set disable_keepalive=true to turn off keep-alive mechanism, false by default (keepalive is active by default). Note: setting the config value `keepalive` to `0` doesn't disable keepalive feature, but uses a default keepalive period */
    esp_mqtt_protocol_ver_t protocol_ver;   /*!< MQTT protocol version used for connection, defaults to value from menuconfig*/
    int message_retransmit_timeout;         /*!< timeout for retansmit of failded packet */
    int reconnect_timeout_ms;               /*!< Reconnect to the broker after this value in miliseconds if auto reconnect is not disabled (defaults to 10s) */
    int network_timeout_ms;                 /*!< Abort network operation if it is not completed after this value, in milliseconds (defaults to 10s) */
    int refresh_connection_after_ms;        /*!< Refresh connection after this value (in milliseconds) */
    bool disable_auto_reconnect;            /*!< this mqtt client will reconnect to server (when errors/disconnect). Set disable_auto_reconnect=true to disable */
    void *user_context;                     /*!< pass user context to this option, then can receive that context in ``event->user_context`` */
    int buffer_size;                        /*!< size of MQTT send/receive buffer, default is 1024 (only receive buffer size if ``out_buffer_size`` defined) */
    int out_buffer_size;                    /*!< size of MQTT output buffer. If not defined, both output and input buffers have the same size defined as ``buffer_size`` */
} esp_mqtt_client_config_t;

/**
 * @brief Creates mqtt client handle based on the configuration
 *
 * @param config    mqtt configuration structure
 *
 * @return mqtt_client_handle if successfully created, NULL on error
 */
esp_mqtt_client_handle_t esp_mqtt_client_init(const esp_mqtt_client_config_t *config);

/**
 * @brief Sets mqtt connection URI. This API is usually used to overrides the URI
 * configured in esp_mqtt_client_init
 *
 * @param client    mqtt client handle
 * @param uri
 *
 * @return ESP_FAIL if URI parse error, ESP_OK on success
 */
esp_err_t esp_mqtt_client_set_uri(esp_mqtt_client_handle_t client, const char *uri);

/**
 * @brief Starts mqtt client with already created client handle
 *
 * @param client    mqtt client handle
 *
 * @return ESP_OK on success
 *         ESP_ERR_INVALID_ARG on wrong initialization
 *         ESP_FAIL on other error
 */
esp_err_t esp_mqtt_client_start(esp_mqtt_client_handle_t client);

/**
 * @brief This api is typically used to force reconnection upon a specific event
 *
 * @param client    mqtt client handle
 *
 * @return ESP_OK on success
 *         ESP_ERR_INVALID_ARG on wrong initialization
 *         ESP_FAIL if client is in invalid state
 */
esp_err_t esp_mqtt_client_reconnect(esp_mqtt_client_handle_t client);

/**
 * @brief This api is typically used to force disconnection from the broker
 *
 * @param client    mqtt client handle
 *
 * @return ESP_OK on success
 *         ESP_ERR_INVALID_ARG on wrong initialization
 */
esp_err_t esp_mqtt_client_disconnect(esp_mqtt_client_handle_t client);

/**
 * @brief Stops mqtt client tasks
 *
 *  * Notes:
 *  - Cannot be called from the mqtt event handler
 *
 * @param client    mqtt client handle
 *
 * @return ESP_OK on success
 *         ESP_ERR_INVALID_ARG on wrong initialization
 *         ESP_FAIL if client is in invalid state
 */
esp_err_t esp_mqtt_client_stop(esp_mqtt_client_handle_t client);

/**
 * @brief Subscribe the client to defined topic with defined qos
 *
 * Notes:
 * - Client must be connected to send subscribe message
 * - This API is could be executed from a user task or
 * from a mqtt event callback i.e. internal mqtt task
 * (API is protected by internal mutex, so it might block
 * if a longer data receive operation is in progress.
 *
 * @param client    mqtt client handle
 * @param topic
 * @param qos
 *
 * @return message_id of the subscribe message on success
 *         -1 on failure
 */
int esp_mqtt_client_subscribe(esp_mqtt_client_handle_t client, const char *topic, int qos);

/**
 * @brief Unsubscribe the client from defined topic
 *
 * Notes:
 * - Client must be connected to send unsubscribe message
 * - It is thread safe, please refer to `esp_mqtt_client_subscribe` for details
 *
 * @param client    mqtt client handle
 * @param topic
 *
 * @return message_id of the subscribe message on success
 *         -1 on failure
 */
int esp_mqtt_client_unsubscribe(esp_mqtt_client_handle_t client, const char *topic);

/**
 * @brief Client to send a publish message to the broker
 *
 * Notes:
 * - This API might block for several seconds, either due to network timeout (10s)
 *   or if publishing payloads longer than internal buffer (due to message
 *   fragmentation)
 * - Client doesn't have to be connected for this API to work, enqueueing the messages
 *   with qos>1 (returning -1 for all the qos=0 messages if disconnected).
 *   If MQTT_SKIP_PUBLISH_IF_DISCONNECTED is enabled, this API will not attempt to publish
 *   when the client is not connected and will always return -1.
 * - It is thread safe, please refer to `esp_mqtt_client_subscribe` for details
 *
 * @param client    mqtt client handle
 * @param topic     topic string
 * @param data      payload string (set to NULL, sending empty payload message)
 * @param len       data length, if set to 0, length is calculated from payload string
 * @param qos       qos of publish message
 * @param retain    retain flag
 *
 * @return message_id of the publish message (for QoS 0 message_id will always be zero) on success.
 *         -1 on failure.
 */
int esp_mqtt_client_publish(esp_mqtt_client_handle_t client, const char *topic, const char *data, int len, int qos, int retain);

/**
 * @brief Enqueue a message to the outbox, to be sent later. Typically used for messages with qos>0, but could
 * be also used for qos=0 messages if store=true.
 *
 * This API generates and stores the publish message into the internal outbox and the actual sending
 * to the network is performed in the mqtt-task context (in contrast to the esp_mqtt_client_publish()
 * which sends the publish message immediately in the user task's context).
 * Thus, it could be used as a non blocking version of esp_mqtt_client_publish().
 *
 * @param client    mqtt client handle
 * @param topic     topic string
 * @param data      payload string (set to NULL, sending empty payload message)
 * @param len       data length, if set to 0, length is calculated from payload string
 * @param qos       qos of publish message
 * @param retain    retain flag
 * @param store     if true, all messages are enqueued; otherwise only qos1 and qos 2 are enqueued
 *
 * @return message_id if queued successfully, -1 otherwise
 */
int esp_mqtt_client_enqueue(esp_mqtt_client_handle_t client, const char *topic, const char *data, int len, int qos, int retain, bool store);

/**
 * @brief Destroys the client handle
 *
 * Notes:
 *  - Cannot be called from the mqtt event handler
 *
 * @param client    mqtt client handle
 *
 * @return ESP_OK
 *         ESP_ERR_INVALID_ARG on wrong initialization
 */
esp_err_t esp_mqtt_client_destroy(esp_mqtt_client_handle_t client);

/**
 * @brief Set configuration structure, typically used when updating the config (i.e. on "before_connect" event
 *
 * @param client    mqtt client handle
 *
 * @param config    mqtt configuration structure
 *
 * @return ESP_ERR_NO_MEM if failed to allocate
 *         ESP_ERR_INVALID_ARG if conflicts on transport configuration.
 *         ESP_OK on success
 */
esp_err_t esp_mqtt_set_config(esp_mqtt_client_handle_t client, const esp_mqtt_client_config_t *config);

/**
 * @brief Registers mqtt event
 *
 * @param client            mqtt client handle
 * @param event             event type
 * @param event_handler     handler callback
 * @param event_handler_arg handlers context
 *
 * @return ESP_ERR_NO_MEM if failed to allocate
 *         ESP_ERR_INVALID_ARG on wrong initialization
 *         ESP_OK on success
 */
esp_err_t esp_mqtt_client_register_event(esp_mqtt_client_handle_t client, esp_mqtt_event_id_t event, esp_event_handler_t event_handler, void *event_handler_arg);

/**
 * @brief Get outbox size
 *
 * @param client            mqtt client handle
 * @return outbox size
 *         0 on wrong initialization
 */
int esp_mqtt_client_get_outbox_size(esp_mqtt_client_handle_t client);

#ifdef CONFIG_MQTT_PROTOCOL_5
/**
 * @brief Set MQTT5 client connect property configuration
 *
 * @param client            mqtt client handle
 * @param connect_property  connect property
 *
 * @return ESP_ERR_NO_MEM if failed to allocate
 *         ESP_ERR_INVALID_ARG on wrong initialization
 *         ESP_FAIL on fail
 *         ESP_OK on success
 */
esp_err_t esp_mqtt5_client_set_connect_property(esp_mqtt_client_handle_t client, const esp_mqtt5_connection_property_config_t *connect_property);

/**
 * @brief Set MQTT5 client publish property configuration
 *
 * This API will not store the publish property, it is one-time configuration.
 * Before call `esp_mqtt_client_publish` to publish data, call this API to set publish property if have
 *
 * @param client            mqtt client handle
 * @param property          publish property
 *
 * @return ESP_ERR_INVALID_ARG on wrong initialization
 *         ESP_FAIL on fail
 *         ESP_OK on success
 */
esp_err_t esp_mqtt5_client_set_publish_property(esp_mqtt_client_handle_t client, const esp_mqtt5_publish_property_config_t *property);

/**
 * @brief Set MQTT5 client subscribe property configuration
 *
 * This API will not store the subscribe property, it is one-time configuration.
 * Before call `esp_mqtt_client_subscribe` to subscribe topic, call this API to set subscribe property if have
 *
 * @param client            mqtt client handle
 * @param property          subscribe property
 *
 * @return ESP_ERR_INVALID_ARG on wrong initialization
 *         ESP_FAIL on fail
 *         ESP_OK on success
 */
esp_err_t esp_mqtt5_client_set_subscribe_property(esp_mqtt_client_handle_t client, const esp_mqtt5_subscribe_property_config_t *property);

/**
 * @brief Set MQTT5 client unsubscribe property configuration
 *
 * This API will not store the unsubscribe property, it is one-time configuration.
 * Before call `esp_mqtt_client_unsubscribe` to unsubscribe topic, call this API to set unsubscribe property if have
 *
 * @param client            mqtt client handle
 * @param property          unsubscribe property
 *
 * @return ESP_ERR_INVALID_ARG on wrong initialization
 *         ESP_FAIL on fail
 *         ESP_OK on success
 */
esp_err_t esp_mqtt5_client_set_unsubscribe_property(esp_mqtt_client_handle_t client, const esp_mqtt5_unsubscribe_property_config_t *property);

/**
 * @brief Set MQTT5 client disconnect property configuration
 *
 * This API will not store the disconnect property, it is one-time configuration.
 * Before call `esp_mqtt_client_disconnect` to disconnect connection, call this API to set disconnect property if have
 *
 * @param client            mqtt client handle
 * @param property          disconnect property
 *
 * @return ESP_ERR_NO_MEM if failed to allocate
 *         ESP_ERR_INVALID_ARG on wrong initialization
 *         ESP_FAIL on fail
 *         ESP_OK on success
 */
esp_err_t esp_mqtt5_client_set_disconnect_property(esp_mqtt_client_handle_t client, const esp_mqtt5_disconnect_property_config_t *property);

/**
 * @brief Set MQTT5 client user property configuration
 *
 * This API will allocate memory for user_property, please DO NOT forget `call esp_mqtt5_client_delete_user_property`
 * after you use it.
 * Before publish data, subscribe topic, unsubscribe, etc, call this API to set user property if have
 *
 * @param user_property            user_property handle
 * @param item                     array of user property data (eg. {{"var","val"},{"other","2"}})
 * @param item_num                 number of items in user property data
 *
 * @return ESP_ERR_NO_MEM if failed to allocate
 *         ESP_FAIL on fail
 *         ESP_OK on success
 */
esp_err_t esp_mqtt5_client_set_user_property(mqtt5_user_property_handle_t *user_property, esp_mqtt5_user_property_item_t item[], uint8_t item_num);

/**
 * @brief Get MQTT5 client user property
 *
 * @param user_property            user_property handle
 * @param item                     point that store user property data
 * @param item_num                 number of items in user property data
 *
 * This API can use with `esp_mqtt5_client_get_user_property_count` to get list count of user property.
 * And malloc number of count item array memory to store the user property data.
 * Please DO NOT forget the item memory, key and value point in item memory when get user property data successfully.
 *
 * @return ESP_ERR_NO_MEM if failed to allocate
 *         ESP_FAIL on fail
 *         ESP_OK on success
 */
esp_err_t esp_mqtt5_client_get_user_property(mqtt5_user_property_handle_t user_property, esp_mqtt5_user_property_item_t *item, uint8_t *item_num);

/**
 * @brief Get MQTT5 client user property list count
 *
 * @param user_property            user_property handle
 * @return user property list count
 */
uint8_t esp_mqtt5_client_get_user_property_count(mqtt5_user_property_handle_t user_property);

/**
 * @brief Free the user property list
 *
 * @param user_property            user_property handle
 * 
 * This API will free the memory in user property list and free user_property itself
 */
void esp_mqtt5_client_delete_user_property(mqtt5_user_property_handle_t user_property);
#endif
#ifdef __cplusplus
}
#endif //__cplusplus

#endif
