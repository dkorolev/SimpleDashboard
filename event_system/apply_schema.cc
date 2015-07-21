#include "preprocessor.h"

CEREAL_REGISTER_TYPE(CTFOBaseEvent);
CEREAL_REGISTER_TYPE(LaunchEvent);
CEREAL_REGISTER_TYPE(FocusEvent);
CEREAL_REGISTER_TYPE(DeviceInfoEvent);
CEREAL_REGISTER_TYPE(CardActionEvent);

struct SimpleDumpJSONToStandardError {
  template<typename T>
  void operator()(const T& entry) {
    std::cerr << JSON(entry) << std::endl;
  }
};

int main() {
  EventPreprocessor<CTFOBaseEvent> preprocessor;
  std::string raw_entry;
  LogEntry log_entry;
  SimpleDumpJSONToStandardError processor;
  while(std::getline(std::cin, raw_entry)) {
    try {
      ParseJSON(raw_entry, log_entry);
      preprocessor.DispatchLogEntry(log_entry, processor);
    } catch (const bricks::ParseJSONException&) {
      std::cerr << "Unable to parse raw log entry." << std::endl;
    }      
  }
}
