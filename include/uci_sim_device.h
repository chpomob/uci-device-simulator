#ifndef UCI_SIM_DEVICE_H
#define UCI_SIM_DEVICE_H

#include "uci_sim_packet.h"
#include "uci_sim_scenario.h"

#define UCI_SIM_MAX_PENDING_NOTIFICATIONS 8U
#define UCI_SIM_MAX_DEVICE_CONFIGS 8U
#define UCI_SIM_MAX_SESSION_CONFIGS 16U
#define UCI_SIM_MAX_CONFIG_VALUE 64U

typedef struct uci_sim_session_config {
    uint8_t config_id;
    uint8_t value_len;
    uint8_t value[UCI_SIM_MAX_CONFIG_VALUE];
    int in_use;
} uci_sim_session_config_t;

typedef struct uci_sim_device_config {
    uint8_t config_id;
    uint8_t value_len;
    uint8_t value[UCI_SIM_MAX_CONFIG_VALUE];
    int in_use;
} uci_sim_device_config_t;

typedef struct uci_sim_session {
    uint32_t session_id;
    uint8_t session_type;
    uint8_t state;
    uint32_t ranging_count;
    uint16_t max_data_size;
    uint8_t ranging_stream_remaining;
    int allocated;
    uci_sim_session_config_t configs[UCI_SIM_MAX_SESSION_CONFIGS];
} uci_sim_session_t;

typedef struct uci_sim_device {
    uint16_t uci_version;
    uint16_t mac_version;
    uint16_t phy_version;
    uint16_t test_version;
    uint8_t device_state;
    uci_sim_device_config_t device_configs[UCI_SIM_MAX_DEVICE_CONFIGS];
    uci_sim_scenario_kind_t scenario;
    uint32_t next_ranging_sequence;
    uci_sim_session_t sessions[UCI_SIM_MAX_SESSIONS];
    uci_sim_packet_t pending_notifications[UCI_SIM_MAX_PENDING_NOTIFICATIONS];
    size_t pending_notification_count;
} uci_sim_device_t;

typedef struct uci_sim_result {
    uci_sim_packet_t response;
    int has_response;
    uci_sim_packet_t notification;
    int has_notification;
} uci_sim_result_t;

void uci_sim_device_init(uci_sim_device_t* device);
void uci_sim_device_init_with_scenario(uci_sim_device_t* device, uci_sim_scenario_kind_t scenario);
void uci_sim_device_set_scenario(uci_sim_device_t* device, uci_sim_scenario_kind_t scenario);
int uci_sim_device_queue_notification(uci_sim_device_t* device, const uci_sim_packet_t* notification);
int uci_sim_device_dequeue_notification(uci_sim_device_t* device, uci_sim_packet_t* notification);
int uci_sim_device_deliver_notification(uci_sim_device_t* device,
                                        const uci_sim_packet_t* notification,
                                        uci_sim_result_t* result);
void uci_sim_device_finalize_result(uci_sim_device_t* device,
                                  uci_sim_result_t* result,
                                  size_t pending_count_before);
int uci_sim_session_store_config(uci_sim_session_t* session,
                                 uint8_t config_id,
                                 const uint8_t* value,
                                 uint8_t value_len);
int uci_sim_session_get_config(const uci_sim_session_t* session,
                               uint8_t config_id,
                               uint8_t* value,
                               uint8_t* value_len);
int uci_sim_device_store_config(uci_sim_device_t* device,
                                uint8_t config_id,
                                const uint8_t* value,
                                uint8_t value_len);
int uci_sim_device_get_config(const uci_sim_device_t* device,
                              uint8_t config_id,
                              uint8_t* value,
                              uint8_t* value_len);
int uci_sim_device_emit_ranging_stream(uci_sim_device_t* device,
                                       uci_sim_session_t* session,
                                       uci_sim_result_t* result);
int uci_sim_device_handle_packet(uci_sim_device_t* device,
                                 const uci_sim_packet_t* request,
                                 uci_sim_result_t* result);

#endif
