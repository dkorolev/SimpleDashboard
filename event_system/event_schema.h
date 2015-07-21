#include "../schema.h"

struct CTFOBaseEvent : yoda::Padawan {
  uint64_t t;
  std::string device_id;
  std::string client_id;

  template <typename A>
  void serialize(A& ar) {
    Padawan::serialize(ar);
    ar(CEREAL_NVP(t), CEREAL_NVP(device_id), CEREAL_NVP(client_id));
  }
};

struct LaunchEvent : CTFOBaseEvent {
  bool first = false;
  LaunchEvent() = default;
  explicit LaunchEvent(bool first) : first(first) {}

  template <typename A>
  void serialize(A& ar) {
    CTFOBaseEvent::serialize(ar);
    ar(CEREAL_NVP(first));
  }
};

struct FocusEvent : CTFOBaseEvent {
  bool gained_focus = false;
  FocusEvent() = default;
  explicit FocusEvent(bool gained_focus) : gained_focus(gained_focus) {}

  template <typename A>
  void serialize(A& ar) {
    CTFOBaseEvent::serialize(ar);
    ar(CEREAL_NVP(gained_focus));
  }
};

struct DeviceInfoEvent : CTFOBaseEvent {
  std::string model;
  std::string name;

  template <typename A>
  void serialize(A& ar) {
    CTFOBaseEvent::serialize(ar);
    ar(CEREAL_NVP(model), CEREAL_NVP(name));
  }
};

struct CardActionEvent : CTFOBaseEvent {
  UID uid;
  CID cid;
  ANSWER action;

  template <typename A>
  void serialize(A& ar) {
    CTFOBaseEvent::serialize(ar);
    ar(CEREAL_NVP(uid), CEREAL_NVP(cid), CEREAL_NVP(action));
  }
};
