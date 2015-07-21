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

  template<typename F>
  void operator()(const iOSDeviceInfo& source_event, uint64_t t, F&& f) {
    DeviceInfoEvent device_info_event;
    device_info_event.t = t;
    ExtractBaseEventFields(source_event, device_info_event);
    device_info_event.model = source_event.info.at("deviceModel");
    device_info_event.name = source_event.info.at("deviceName");
    f(std::move(device_info_event));
  }
  template<typename F>
  void operator()(const iOSAppLaunchEvent&, uint64_t, F&&) {}
  template<typename F>
  void operator()(const iOSFirstLaunchEvent&, uint64_t, F&&) {}
  template<typename F>
  void operator()(const iOSFocusEvent&, uint64_t, F&&) {}
  template<typename F>
  void operator()(const iOSGenericEvent&, uint64_t, F&&) {}

 private:
  void ExtractBaseEventFields(const MidichloriansEvent& source_event, BASE_EVENT& dest_event) {
    dest_event.device_id = source_event.device_id;
    dest_event.client_id = source_event.client_id;
  }
};

template <typename BASE_EVENT>
struct EventPreprocessor {
  typedef BASE_EVENT T_BASE_EVENT;

  template<typename F>
  void DispatchLogEntry(const LogEntry& log_entry, F&& f) {
    if (log_entry.m != "TICK") {
      try {
        std::unique_ptr<MidichloriansEvent> midichlorians_event;
        ParseJSON(log_entry.b, midichlorians_event);
        bricks::metaprogramming::RTTIDynamicCall<typename MidichloriansEventPreprocessor<BASE_EVENT>::T_SUPPORTED_TYPES>(
           midichlorians_event, midichlorians_preprocessor, log_entry.t, std::forward<F>(f));
      } catch (const bricks::ParseJSONException&) {
        std::cerr << "Unable to parse LogEvent body." << std::endl;
      }
    }
  }
 private:
  MidichloriansEventPreprocessor<BASE_EVENT> midichlorians_preprocessor;
};
