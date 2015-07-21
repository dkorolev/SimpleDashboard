#include "event_schema.h"

#include "../../Current/Bricks/template/metaprogramming.h"
#include "../../Current/EventCollector/event_collector.h"
#include "../../Current/Midichlorians/Dev/Beta/MidichloriansDataDictionary.h"

template <typename BASE_EVENT>
struct MidichloriansEventPreprocessor {
  typedef BASE_EVENT T_BASE_EVENT;
  typedef std::unique_ptr<BASE_EVENT> T_RETURN_TYPE;
  typedef std::tuple<iOSDeviceInfo,
          iOSAppLaunchEvent,
          iOSFirstLaunchEvent,
          iOSFocusEvent,
          iOSGenericEvent> T_SUPPORTED_TYPES;

  void operator()(const iOSDeviceInfo& source_event, T_RETURN_TYPE& result) {
    DeviceInfoEvent* device_info_event = new DeviceInfoEvent;
    ExtractBaseEventFields(source_event, *device_info_event);
    device_info_event->model = source_event.info.at("deviceModel");
    device_info_event->name = source_event.info.at("deviceName");
    result.reset(device_info_event);
  }
  void operator()(const iOSAppLaunchEvent& source_event, T_RETURN_TYPE& result) {}
  void operator()(const iOSFirstLaunchEvent& source_event, T_RETURN_TYPE& result) {}
  void operator()(const iOSFocusEvent& source_event, T_RETURN_TYPE& result) {}
  void operator()(const iOSGenericEvent& source_event, T_RETURN_TYPE& result) {}

 private:
  void ExtractBaseEventFields(const MidichloriansEvent& source_event, BASE_EVENT& dest_event) {
    dest_event.device_id = source_event.device_id;
    dest_event.client_id = source_event.client_id;
  }
};

template <typename BASE_EVENT>
struct EventPreprocessor {
  typedef BASE_EVENT T_BASE_EVENT;

  std::unique_ptr<BASE_EVENT> ProcessLogEntry(LogEntry& log_entry) {
    std::unique_ptr<BASE_EVENT> result;
    if (log_entry.m != "TICK") {
      std::unique_ptr<MidichloriansEvent> midichlorians_event;
      try {
        ParseJSON(log_entry.b, midichlorians_event);
      } catch (const bricks::ParseJSONException&) {
        std::cerr << "Unable to parse LogEvent body." << std::endl;
      }
      bricks::metaprogramming::RTTIDynamicCall<typename MidichloriansEventPreprocessor<BASE_EVENT>::T_SUPPORTED_TYPES>(
         midichlorians_event, midichlorians_preprocessor, result);
      result->t = log_entry.t;
      return result;
    }
  }
 private:
  MidichloriansEventPreprocessor<BASE_EVENT> midichlorians_preprocessor;
};
