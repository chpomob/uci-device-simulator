#ifndef UCI_SIM_DEVICE_H
#define UCI_SIM_DEVICE_H

#include "uci_sim_packet.h"
#include "uci_sim_profile.h"
#include "uci_sim_scenario.h"

#define UCI_SIM_MAX_PENDING_NOTIFICATIONS 8U
#define UCI_SIM_MAX_SCENARIO_EVENTS 8U
#define UCI_SIM_MAX_DEVICE_CONFIGS 8U
#define UCI_SIM_MAX_SESSION_CONFIGS 80U
#define UCI_SIM_MAX_CONFIG_VALUE 64U

typedef enum {
    UCI_SIM_EVENT_NONE = 0,
    UCI_SIM_EVENT_RANGE_DATA = 1,
} uci_sim_event_type_t;

typedef struct uci_sim_scheduled_event {
    uci_sim_event_type_t type;
    uint32_t session_id;
    uint32_t delay_ms;
} uci_sim_scheduled_event_t;

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

typedef struct uci_sim_multicast_entry {
    uint16_t short_address;
    uint32_t subsession_id;
    uint8_t key_len;
    uint8_t key[32];
    int in_use;
} uci_sim_multicast_entry_t;

typedef struct uci_sim_logical_link {
    uint8_t link_id;
    uint8_t mode;
    uint8_t credit;
    int in_use;
} uci_sim_logical_link_t;

typedef struct uci_sim_session {
    uint32_t session_id;
    uint8_t session_type;
    uint8_t state;
    uint32_t ranging_count;
    uint16_t max_data_size;
    uint8_t ranging_stream_remaining;
    uint8_t dt_anchor_round_indexes[UCI_SIM_MAX_DT_ROUNDS];
    uint8_t dt_anchor_round_count;
    uint8_t dt_tag_round_indexes[UCI_SIM_MAX_DT_ROUNDS];
    uint8_t dt_tag_round_count;
    uint8_t dtp_repetition;
    uint8_t dtp_control;
    uint8_t dtp_size;
    uint8_t dtp_payload_len;
    uint8_t dtp_payload[64];
    uint32_t hus_controller_primary_session_id;
    uint8_t hus_controller_role;
    uint8_t hus_controller_reserved;
    uint16_t hus_controller_config_length;
    uint8_t hus_controller_config_data[250];
    uint32_t hus_controlee_primary_session_id;
    uint8_t hus_controlee_role;
    uint8_t hus_controlee_reserved;
    uint16_t hus_controlee_config_length;
    uint8_t hus_controlee_config_data[250];
    uint16_t last_data_sequence;
    uint16_t last_data_length;
    uint8_t has_last_data_message;
    uint8_t has_last_proximity_state;
    uint8_t last_in_proximity_range;
    int allocated;
    uci_sim_session_config_t configs[UCI_SIM_MAX_SESSION_CONFIGS];
    uci_sim_multicast_entry_t multicast_entries[UCI_SIM_MAX_MULTICAST_ENTRIES];
    uci_sim_logical_link_t logical_links[UCI_SIM_MAX_LOGICAL_LINKS];
    uint8_t logical_link_count;
} uci_sim_session_t;

typedef struct uci_sim_device {
    uint16_t uci_version;
    uint16_t mac_version;
    uint16_t phy_version;
    uint16_t test_version;
    uint8_t device_state;
    uint64_t next_uwbs_timestamp;
    const uci_sim_profile_t* profile;
    uci_sim_device_config_t device_configs[UCI_SIM_MAX_DEVICE_CONFIGS];
    uci_sim_scenario_kind_t scenario;
    uint32_t next_ranging_sequence;
    uci_sim_session_t sessions[UCI_SIM_MAX_SESSIONS];
    uci_sim_scheduled_event_t scheduled_events[UCI_SIM_MAX_SCENARIO_EVENTS];
    size_t scheduled_event_count;
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
void uci_sim_device_init_with_profile(uci_sim_device_t* device,
                                      const uci_sim_profile_t* profile,
                                      uci_sim_scenario_kind_t scenario);
void uci_sim_device_reset_runtime_state(uci_sim_device_t* device);
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
uint8_t uci_sim_session_get_range_data_ntf_config(const uci_sim_session_t* session);
uint16_t uci_sim_session_get_range_data_ntf_proximity_near(const uci_sim_session_t* session);
uint16_t uci_sim_session_get_range_data_ntf_proximity_far(const uci_sim_session_t* session);
uint8_t uci_sim_session_get_aoa_result_req(const uci_sim_session_t* session);
uint8_t uci_sim_session_get_result_report_config(const uci_sim_session_t* session);
int uci_sim_device_store_config(uci_sim_device_t* device,
                                uint8_t config_id,
                                const uint8_t* value,
                                uint8_t value_len);
int uci_sim_device_get_config(const uci_sim_device_t* device,
                              uint8_t config_id,
                              uint8_t* value,
                              uint8_t* value_len);
int uci_sim_device_get_session(uci_sim_device_t* device,
                               uint32_t session_id,
                               uci_sim_session_t** out_session);
int uci_sim_session_add_multicast_entry(uci_sim_session_t* session,
                                        uint16_t short_address,
                                        uint32_t subsession_id,
                                        const uint8_t* key,
                                        uint8_t key_len);
int uci_sim_session_remove_multicast_entry(uci_sim_session_t* session,
                                           uint16_t short_address,
                                           uint32_t subsession_id);
uci_sim_logical_link_t* uci_sim_session_find_logical_link(uci_sim_session_t* session,
                                                          uint8_t link_id);
uci_sim_logical_link_t* uci_sim_session_allocate_logical_link(uci_sim_session_t* session,
                                                              uint8_t requested_id,
                                                              uint8_t* assigned_id);
int uci_sim_session_remove_logical_link(uci_sim_session_t* session, uint8_t link_id);
int uci_sim_device_schedule_event(uci_sim_device_t* device,
                                  uci_sim_event_type_t type,
                                  uint32_t session_id,
                                  uint32_t delay_ms);
void uci_sim_device_cancel_session_events(uci_sim_device_t* device, uint32_t session_id);
void uci_sim_device_tick_events(uci_sim_device_t* device, uint32_t elapsed_ms);
int uci_sim_device_dequeue_ready_event(uci_sim_device_t* device, uci_sim_scheduled_event_t* event);
int uci_sim_device_emit_ranging_stream(uci_sim_device_t* device,
                                       uci_sim_session_t* session,
                                       uci_sim_result_t* result);
int uci_sim_device_handle_packet(uci_sim_device_t* device,
                                 const uci_sim_packet_t* request,
                                 uci_sim_result_t* result);

#endif
