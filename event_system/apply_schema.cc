#include "preprocessor.h"

CEREAL_REGISTER_TYPE(CTFOBaseEvent);
CEREAL_REGISTER_TYPE(LaunchEvent);
CEREAL_REGISTER_TYPE(FocusEvent);
CEREAL_REGISTER_TYPE(DeviceInfoEvent);
CEREAL_REGISTER_TYPE(CardActionEvent);

int main() {
  EventPreprocessor<CTFOBaseEvent> preprocessor;
  std::string raw_entry;
  LogEntry log_entry;
  while(std::getline(std::cin, raw_entry)) {
    try {
      ParseJSON(raw_entry, log_entry);
      std::cout << JSON(preprocessor.ProcessLogEntry(log_entry));
    } catch (const bricks::ParseJSONException&) {
      std::cerr << "Unable to parse raw log entry." << std::endl;
    }      
  }
}
