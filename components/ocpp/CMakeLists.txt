idf_component_register(SRCS
  "ocpp_auth.c"
  "ocpp_call_with_cb.c"
  "ocpp_listener.c"
  "ocpp_reservation.c"
  "ocpp_smart_charging.c"
  "ocpp_task.c"
  "ocpp_transaction.c"
  "types/ocpp_authorization_data.c"
  "types/ocpp_authorization_status.c"
  "types/ocpp_charge_point_status.c"
  "types/ocpp_charging_profile.c"
  "types/ocpp_ci_string_type.c"
  "types/ocpp_csl.c"
  "types/ocpp_date_time.c"
  "types/ocpp_diagnostics_status.c"
  "types/ocpp_enum.c"
  "types/ocpp_id_tag_info.c"
  "types/ocpp_key_value.c"
  "types/ocpp_meter_value.c"
  "types/ocpp_phase_rotation.c"
  "types/ocpp_reason.c"
  "types/ocpp_registration_status.c"
  "types/ocpp_update_status.c"
  "messages/call_messages/autorize.c"
  "messages/call_messages/boot_notification.c"
  "messages/call_messages/data_transfer.c"
  "messages/call_messages/diagnostics_status_notification.c"
  "messages/call_messages/firmware_status_notification.c"
  "messages/call_messages/heartbeat.c"
  "messages/call_messages/meter_values.c"
  "messages/call_messages/ocpp_create_call.c"
  "messages/call_messages/start_transaction.c"
  "messages/call_messages/status_notification.c"
  "messages/call_messages/stop_transaction.c"
  "messages/error_messages/ocpp_create_error.c"
  "messages/result_messages/change_availability.c"
  "messages/result_messages/change_configuration.c"
  "messages/result_messages/clear_cache.c"
  "messages/result_messages/clear_charging_profile.c"
  "messages/result_messages/data_transfer.c"
  "messages/result_messages/get_composite_schedule.c"
  "messages/result_messages/get_configuration.c"
  "messages/result_messages/get_diagnostics.c"
  "messages/result_messages/get_local_list_version.c"
  "messages/result_messages/remote_start_stop_transaction.c"
  "messages/result_messages/reset.c"
  "messages/result_messages/send_local_list.c"
  "messages/result_messages/set_charging_profile.c"
  "messages/result_messages/trigger_message.c"
  "messages/result_messages/unlock_connector.c"
  "messages/result_messages/update_firmware.c"
  "messages/result_messages/cancel_reservation.c"
  "messages/result_messages/reserve_now.c"
  "messages/result_messages/ocpp_create_result.c"
  "ocpp_json/ocppj_message_structure.c"
  "ocpp_json/ocppj_validation.c"
  INCLUDE_DIRS "./include"
  REQUIRES driver esp_wifi esp_event esp_websocket_client json fatfs zaptec_cloud utz
  )

find_package(Doxygen
  OPTIONAL_COMPONENTS dot mscgen dia
  )

if(DOXYGEN_FOUND)
  message("Doxygen found. Adding documentation generation target for ocpp component")

  doxygen_add_docs(ocpp_doc
    "${CMAKE_CURRENT_SOURCE_DIR}/include/"
    )
else (DOXYGEN_FOUND)
  message("Consider installing doxygen for generating ocpp documentation")
endif (DOXYGEN_FOUND)
