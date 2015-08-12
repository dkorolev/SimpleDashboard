/*******************************************************************************
The MIT License (MIT)

Copyright (c) 2015 Dmitry "Dima" Korolev <dmitry.korolev@gmail.com>
Copyright (c) 2015 Maxim Zhurovich <zhurovich@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*******************************************************************************/

#include <algorithm>
#include <cctype>

#include "stdin_parse.h"
#include "insights.h"
#include "cubes.h"

#include "../Current/Profiler/profiler.h"

#include "../Current/Bricks/dflags/dflags.h"
#include "../Current/Bricks/strings/util.h"
#include "../Current/Bricks/template/metaprogramming.h"
#include "../Current/Bricks/waitable_atomic/waitable_atomic.h"

#include "../Current/Sherlock/sherlock.h"
#include "../Current/Yoda/yoda.h"

// Structured iOS events structure to follow.
#include "../Current/Midichlorians/Dev/Beta/MidichloriansDataDictionary.h"

DEFINE_int32(port, 3000, "Port to spawn the dashboard on.");
DEFINE_string(route, "/", "The route to serve the dashboard on.");
DEFINE_string(output_uri_prefix, "http://localhost", "The prefix for the URI-s output by the server.");
DEFINE_bool(enable_graceful_shutdown,
            false,
            "Set to true if the binary is only spawned to generate cube/insights data.");

#ifdef PROFILER_ENABLED
DEFINE_string(profiler_route, "/profile", "The route to expose the performance profile on.");
#endif

using bricks::strings::Printf;
using bricks::strings::ToLower;
using bricks::strings::Split;
using bricks::strings::ToString;
using bricks::strings::FromString;
using bricks::time::Now;
using bricks::Singleton;
using bricks::WaitableAtomic;
using bricks::metaprogramming::RTTIDynamicCall;

struct SearchIndex {
  std::map<std::string, std::set<std::string>> terms;

  void AddToIndex(const std::string& key, const std::string& value) {
    for (const auto& term : Split(ToLower(key), ::isalnum)) {
      terms[term].insert(value);
    }
  }

  template <typename A>
  void serialize(A& ar) {
    ar(CEREAL_NVP(terms));
  }

  struct Populator {
    typedef std::tuple<iOSIdentifyEvent,
                       iOSDeviceInfo,
                       iOSAppLaunchEvent,
                       iOSFirstLaunchEvent,
                       iOSFocusEvent,
                       iOSGenericEvent,
                       iOSBaseEvent> T_TYPES;
    SearchIndex& index;
    const std::string& rhs;
    Populator(SearchIndex& index, const std::string& rhs) : index(index), rhs(rhs) {}
    void operator()(iOSIdentifyEvent) {}
    void operator()(const iOSDeviceInfo& e) {
      for (const auto cit : e.info) {
        index.AddToIndex(cit.first, rhs);
        index.AddToIndex(cit.second, rhs);
      }
    }
    void operator()(const iOSAppLaunchEvent& e) { index.AddToIndex(e.binary_version, rhs); }
    void operator()(iOSFirstLaunchEvent) {}
    void operator()(iOSFocusEvent) {}
    void operator()(const iOSGenericEvent& e) {
      index.AddToIndex(e.event, rhs);
      index.AddToIndex(e.source, rhs);
    }
    void operator()(const iOSBaseEvent& e) { index.AddToIndex(e.description, rhs); }
  };
};

typedef EventWithTimestamp<MidichloriansEvent> MidichloriansEventWithTimestamp;
CEREAL_REGISTER_TYPE(MidichloriansEventWithTimestamp);

// Events grouped by session group key.
// Currently: `client_id`.
// TODO(dkorolev): Add more.
struct EventsByGID : yoda::Padawan {
  std::string row;  // GID, Group ID.
  uint64_t col;     // EID, Event ID.
  EventsByGID(std::string row = "", uint64_t col = 0) : row(row), col(col) {}
  template <typename A>
  void serialize(A& ar) {
    Padawan::serialize(ar);
    ar(CEREAL_NVP(row), CEREAL_NVP(col));
  }
};
CEREAL_REGISTER_TYPE(EventsByGID);

// TODO(dkorolev): Order of events.
// TODO(dkorolev): "Past N prior to X" events.
// TODO(dkorolev): "A within T from B" events.
struct AggregatedSessionInfo : yoda::Padawan {
  // Unique keys. Both can be used for traversal / retrieval.
  std::string sid;  // SID, the aggregated session ID.
  std::string gid;  // GID, the identifier of the group this session comes from.

  struct Row {
    std::string sid;
    bool operator<(const Row& rhs) const { return sid < rhs.sid; }
  };
  struct Col {
    std::string gid;
    bool operator<(const Col& rhs) const { return gid < rhs.gid; }
  };

  Row row() const { return Row{sid}; }
  Col col() const { return Col{gid}; }

  // Unique identifier.
  std::string uri;

  // Aggregated numbers.
  size_t number_of_events;
  size_t number_of_seconds;

  // Simple aggregation.
  std::map<std::string, size_t> counters;

  // Timestamps, first and last.
  uint64_t ms_first;
  uint64_t ms_last;

  // Events, because meh. -- D.K.
  std::vector<uint64_t> events;

  void Finalize() {
    number_of_events = events.size();
    number_of_seconds = (ms_last - ms_first + 1000 - 1) / 1000;
  }

  template <typename A>
  void serialize(A& ar) {
    Padawan::serialize(ar);
    ar(CEREAL_NVP(uri),
       CEREAL_NVP(sid),
       CEREAL_NVP(gid),
       CEREAL_NVP(number_of_events),
       CEREAL_NVP(number_of_seconds),
       CEREAL_NVP(counters),
       CEREAL_NVP(ms_first),
       CEREAL_NVP(ms_last),
       CEREAL_NVP(events));
  }
};
CEREAL_REGISTER_TYPE(AggregatedSessionInfo);

namespace DashboardAPIType {

using yoda::MemoryOnlyAPI;
using yoda::Dictionary;
using yoda::Matrix;
typedef MemoryOnlyAPI<Dictionary<MidichloriansEventWithTimestamp>,
                      Matrix<EventsByGID>,
                      Matrix<AggregatedSessionInfo>> DB;

}  // namespace DashboardAPIType

using DashboardAPIType::DB;

// Key extraction logic.
// One of N. -- D.K.
struct Splitter {
  DB& db;

  struct SessionsListPayload {
    std::vector<std::string> sessions;
    template <typename A>
    void serialize(A& ar) {
      ar(CEREAL_NVP(sessions));
    }
  };

  struct SessionDetailsPayload {
    struct Event {
      uint64_t eid_as_uint64;
      std::string uri;
      std::string time_ago;
      std::string time_since_previous_event;
      std::string text;
      Event(const uint64_t eid_as_uint64, const std::string& uri) : eid_as_uint64(eid_as_uint64), uri(uri) {}
      template <typename A>
      void serialize(A& ar) {
        ar(CEREAL_NVP(uri), CEREAL_NVP(time_ago), CEREAL_NVP(time_since_previous_event), CEREAL_NVP(text));
      }
      bool operator<(const Event& rhs) const { return eid_as_uint64 > rhs.eid_as_uint64; }
    };
    std::string error;
    std::string up;
    std::vector<Event> event;
    template <typename A>
    void serialize(A& ar) {
      ar(CEREAL_NVP(error), CEREAL_NVP(up), CEREAL_NVP(event));
    }
  };

  struct CurrentSessions {
    std::map<std::string, AggregatedSessionInfo> map;
    void EndTimedOutSessions(const uint64_t ms, typename DB::T_DATA& data) {
      PROFILER_SCOPE("CurrentSessions::EndTimedOutSessions()");
      std::vector<std::string> sessions_to_end;
      for (const auto cit : map) {
        if (ms - cit.second.ms_last > 10 * 60 * 1000) {
          sessions_to_end.push_back(cit.first);
        }
      }
      for (const auto key : sessions_to_end) {
        AggregatedSessionInfo& session = map[key];
        session.Finalize();
        data.Add(session);
        map.erase(key);
      }
    }
    template <typename A>
    void serialize(A& ar) {
      ar(CEREAL_NVP(map));
    }
  };

  struct SessionsPayload {
    CurrentSessions current;
    std::map<std::string, std::map<std::string, AggregatedSessionInfo>> finalized;
    template <typename A>
    void serialize(A& ar) {
      ar(CEREAL_NVP(current), CEREAL_NVP(finalized));
    }
  };

  WaitableAtomic<CurrentSessions> current_sessions;

  explicit Splitter(DB& db) : db(db) {
    // Grouped logs browser.
    HTTP(FLAGS_port).Register(FLAGS_route + "g", [this, &db](Request r) {
      const std::string& key = r.url.query["gid"];
      if (key.empty()) {
        db.Transaction([](typename DB::T_DATA data) {
                         SessionsListPayload payload;
                         for (const auto cit : yoda::Matrix<EventsByGID>::Accessor(data).Rows()) {
                           payload.sessions.push_back(FLAGS_output_uri_prefix + "/g?gid=" + cit.key());
                         }
                         std::sort(std::begin(payload.sessions), std::end(payload.sessions));
                         return payload;
                       },
                       std::move(r));
      } else {
        const auto now_as_uint64 = static_cast<uint64_t>(Now());
        db.Transaction(
            [key, now_as_uint64](typename DB::T_DATA data) {
              SessionDetailsPayload payload;
              try {
                payload.up = FLAGS_output_uri_prefix + "/g";
                for (const auto cit : yoda::Matrix<EventsByGID>::Accessor(data)[key]) {
                  const auto eid_as_uint64 = static_cast<uint64_t>(cit.col);
                  payload.event.push_back(SessionDetailsPayload::Event(
                      eid_as_uint64, Printf("%s/e?eid=%llu", FLAGS_output_uri_prefix.c_str(), eid_as_uint64)));
                }
                if (!payload.event.empty()) {
                  std::sort(std::begin(payload.event), std::end(payload.event));
                  for (auto& e : payload.event) {
                    const auto ev = data[static_cast<EID>(e.eid_as_uint64)];
                    e.time_ago = MillisecondIntervalAsString(now_as_uint64 - ev.ms);
                    e.text = ev.Description();
                  }
                  for (size_t i = 0; i + 1 < payload.event.size(); ++i) {
                    payload.event[i].time_since_previous_event = MillisecondIntervalAsString(
                        ((payload.event[i].eid_as_uint64 - payload.event[i + 1].eid_as_uint64) / 1000),
                        "same second as the event below",
                        "the event below + ");
                  }
                  payload.event.back().time_since_previous_event = "a long time ago in a galaxy far far away";
                }
              } catch (const yoda::SubscriptException<EventsByGID>&) {
                payload.error = "NOT FOUND";
              }
              return payload;
            },
            std::move(r));
      }
    });

    // Sessions browser.
    // TODO(dkorolev): Browser, not just visualizer.
    HTTP(FLAGS_port).Register(FLAGS_route + "s", [this, &db](Request r) {
      db.Transaction([this, &db](typename DB::T_DATA data) {
                       SessionsPayload payload;
                       // Current sessions.
                       current_sessions.ImmutableUse(
                           [this, &payload](const CurrentSessions& current) { payload.current = current; });
                       // Finalized sessions.
                       const auto& accessor = yoda::Matrix<AggregatedSessionInfo>::Accessor(data);
                       for (const auto& sessions_per_group : accessor.Cols()) {
                         auto& results_per_group = payload.finalized[sessions_per_group.key().gid];
                         for (const auto& individual_session : sessions_per_group) {
                           results_per_group[individual_session.sid] = individual_session;
                         }
                       }
                       return payload;
                     },
                     std::move(r));
    });

    // Export data for insight generation.
    HTTP(FLAGS_port).Register(FLAGS_route + "i", [this, &db](Request r) {
      db.Transaction([this](typename DB::T_DATA data) {
                       const std::vector<int> second_marks({5, 10, 15, 30, 60, 120, 300});
                       // Generate input data for insights.
                       InsightsInput payload;
                       // Create one and only realm so far.
                       payload.realm.resize(payload.realm.size() + 1);
                       InsightsInput::Realm& realm = payload.realm.front();
                       // TODO(dkorolev): Bracketing, grouping, time windows.
                       realm.description = "One and only realm.";
                       // Explain time features.
                       realm.tag["T"].name = "Session length";
                       const auto& accessor = yoda::Matrix<AggregatedSessionInfo>::Accessor(data);
                       for (const auto seconds : second_marks) {
                         auto& feature = realm.feature[Printf(">=%ds", seconds)];
                         feature.tag = "T";
                         feature.yes = Printf("%d seconds or longer", seconds);
                         feature.no = Printf("under %d seconds", seconds);
                       }
                       // Analyze individual sessions and export aggregated info about them.
                       for (const auto& sessions_per_group : accessor.Cols()) {
                         for (const auto& individual_session : sessions_per_group) {
                           // Emit the information about this session, in a way that makes it
                           // comparable with other sessions within the same realm.
                           realm.session.resize(realm.session.size() + 1);
                           InsightsInput::Session& output_session = realm.session.back();
                           output_session.key = individual_session.sid;
                           const int seconds = static_cast<int>(individual_session.number_of_seconds);
                           for (const auto t : second_marks) {
                             if (seconds >= t) {
                               output_session.feature.emplace_back(Printf(">=%ds", t));
                             }
                           }
                           for (const auto& counters : individual_session.counters) {
                             const std::string& feature = counters.first;
                             realm.tag[feature].name = feature;
                             realm.feature[feature].tag = feature;
                             realm.feature[feature].yes = "'" + feature + "'";
                             output_session.feature.emplace_back(feature);
                             for (size_t c = 2; c <= std::min(counters.second, static_cast<size_t>(10)); ++c) {
                               const std::string count_feature =
                                   Printf("%s>=%d", feature.c_str(), static_cast<int>(c));
                               output_session.feature.emplace_back(count_feature);
                               realm.feature[count_feature].tag = feature;
                               realm.feature[count_feature].yes =
                                   Printf("%d or more '%s'", static_cast<int>(c), feature.c_str());
                               realm.feature[count_feature].no =
                                   Printf("%d or less '%s'", static_cast<int>(c) - 1, feature.c_str());
                             }
                           }
                         }
                       }
                       return payload;
                     },
                     std::move(r));
    });

    // Export data for cubes generation.
    HTTP(FLAGS_port).Register(FLAGS_route + "c", [this, &db](Request r) {
      db.Transaction(
          [this](typename DB::T_DATA data) {
            CubeGeneratorInput payload;
            auto& dimensions = payload.space.dimensions;
            auto& sessions = payload.sessions;

            // map<FEATURE, map<FEATURE_COUNT_IN_SESSION, NUMBER_OF_SESSION_CONTAINING_THIS_COUNT>>
            std::map<std::string, std::map<size_t, size_t>> feature_stats;

            // Populate all the sessions.
            const auto& accessor = yoda::Matrix<AggregatedSessionInfo>::Accessor(data);
            for (const auto& sessions_per_group : accessor.Cols()) {
              for (const auto& individual_session : sessions_per_group) {
                sessions.resize(sessions.size() + 1);
                CubeGeneratorInput::Session& output_session = sessions.back();
                output_session.id = individual_session.sid;
                // Dedicated handling for the "number of seconds" dimension.
                output_session.feature_count[TIME_DIMENSION_NAME] = individual_session.number_of_seconds;
                ++feature_stats[TIME_DIMENSION_NAME][individual_session.number_of_seconds];
                // Generic handling for all tracked dimensions.
                for (const auto& feature_counter : individual_session.counters) {
                  const std::string& feature = feature_counter.first;
                  output_session.feature_count[feature] = feature_counter.second;
                  ++feature_stats[feature][feature_counter.second];
                }
              }
            }

            // Put `Session Length` and `Device` dimensions first.
            const std::vector<size_t> second_marks({5, 10, 15, 30, 60, 120, 300});
            dimensions.emplace_back(TIME_DIMENSION_NAME);
            Dimension& time_dimension = dimensions.back();
            assert(second_marks.size() > 1u);
            for (size_t i = 0; i < second_marks.size() - 1u; ++i) {
              const size_t a = second_marks[i];
              const size_t b = (i != second_marks.size() - 2u) ? second_marks[i + 1] - 1u : second_marks[i + 1];
              if (i == 0) {
                Bin first_bin("< " + std::to_string(a), a, Bin::RangeType::LESS);
                time_dimension.bins.push_back(first_bin);
              }
              Bin bin_range(std::to_string(a) + " - " + std::to_string(b), a, b, Bin::RangeType::INTERVAL);
              time_dimension.bins.push_back(bin_range);
              if (i == second_marks.size() - 2u) {
                Bin last_bin("> " + std::to_string(b), b, Bin::RangeType::GREATER);
                time_dimension.bins.push_back(last_bin);
              }
            }
            dimensions.emplace_back(DEVICE_DIMENSION_NAME);
            dimensions.back().bins.emplace_back(DEVICE_UNSPECIFIED_BIN_NAME);

            // Fill dimensions info in the response.
            for (const auto& cit : feature_stats) {
              const std::string feature = cit.first;

              const auto dim_bin = payload.space.SplitFeatureIntoDimensionAndBinNames(feature);
              if (dim_bin.first.empty()) {
                // Skip filtered out features.
                continue;
              }

              Dimension* dim_in_space = payload.space.DimensionByName(dim_bin.first);
              if (dim_bin.second.empty()) {
                assert(!dim_in_space);
                Dimension dim(dim_bin.first);
                dim.bins.emplace_back(NONE_BIN_NAME);
                dim.SmartCreateBins(cit.second);
                dimensions.push_back(dim);
              } else {
                if (!dim_in_space) {
                  Dimension dim(dim_bin.first);
                  dimensions.push_back(dim);
                  dim_in_space = &dimensions.back();
                }
                Bin bin(dim_bin.second, feature);
                dim_in_space->AddBinIfNotExists(bin);
              }
            }

            return payload;
          },
          std::move(r));
    });
  }

  void RealEvent(EID eid, const MidichloriansEventWithTimestamp& event, typename DB::T_DATA& data) {
    PROFILER_SCOPE("Splitter::RealEvent()");

    // Only real events, not ticks with empty `event.e`, should make it here.
    const auto& e = event.e;
    assert(e);

    // Start / update / end active sessions.
    const std::string& cid = e->device_id;
    if (!cid.empty()) {
      // Keep track of events per group.
      const std::string gid = "CID:" + cid;
      {
        PROFILER_SCOPE("`data.Add()`.");
        data.Add(EventsByGID(gid, static_cast<uint64_t>(eid)));
      }

      // Keep track of current and finalized sessions.
      // TODO(dkorolev): This should be a listener to support a chain of streams, not a WaitableAtomic<>.
      {
        PROFILER_SCOPE("`current_sessions.MutableUse()`.");
        current_sessions.MutableUse([this, eid, &gid, &e, &event, &data](CurrentSessions& current) {
          const uint64_t ms = event.ms;
          current.EndTimedOutSessions(ms, data);
          auto& s = current.map[gid];
          if (s.gid.empty()) {
            // A new session is to be created.
            static int index = 100000;
            s.sid = Printf("K%d", ++index);
            s.gid = gid;
            s.ms_first = ms;
          }
          s.ms_last = ms;
          const std::string counter_name = event.CanonicalDescription();
          if (!counter_name.empty()) {
            ++s.counters[counter_name];
          }
          s.events.push_back(static_cast<uint64_t>(eid));
        });
      }

      // Keep events searchable.
      {
        PROFILER_SCOPE("`Singleton<WaitableAtomic<SearchIndex>>().MutableUse()`.");
        Singleton<WaitableAtomic<SearchIndex>>().MutableUse([this, eid, &gid, &e, &event](SearchIndex& index) {
          // Landing pages for searched are grouped event URI and individual event URI.
          std::vector<std::string> values = {"/g?gid=" + gid,
                                             Printf("/e?eid=%llu", static_cast<uint64_t>(eid))};
          for (const auto& rhs : values) {
            // Populate each term.
            RTTIDynamicCall<typename SearchIndex::Populator::T_TYPES>(*e.get(),  // Yes, `const unique_ptr<>`.
                                                                      SearchIndex::Populator(index, rhs));
            index.AddToIndex(gid, rhs);
            index.AddToIndex(ToString(event.ms), rhs);
            // Make keys and parts of keys themselves searchable.
            for (const auto& lhs : values) {
              index.AddToIndex(lhs, rhs);
            }
          }
        });
      }
    }
  }

  void TickEvent(uint64_t ms, typename DB::T_DATA& data) {
    // End active sessions.
    current_sessions.MutableUse(
        [this, ms, &data](CurrentSessions& current) { current.EndTimedOutSessions(ms, data); });
  }
};

// Event listening logic.
struct Listener {
  DB& db;
  Splitter splitter;
  std::atomic_size_t total_processed_entries;

  explicit Listener(DB& db) : db(db), splitter(db), total_processed_entries(0) {}

  inline bool operator()(const EID eid, size_t index) {
    PROFILER_SCOPE("Listener::operator()");
    // auto transaction =
    db.Transaction([this, eid, index](typename DB::T_DATA data) {
      // Yep, it's an extra, synchronous, lookup. But this solution is cleaner data-wise.
      PROFILER_SCOPE("`db.Transaction()`");
      const auto entry = yoda::Dictionary<MidichloriansEventWithTimestamp>::Accessor(data).Get(eid);
      if (entry) {
        // Found in the DB: we have a log-entry-based event.
        PROFILER_SCOPE("Call `RealEvent()`");
        splitter.RealEvent(eid, static_cast<const MidichloriansEventWithTimestamp&>(entry), data);
      } else {
        PROFILER_SCOPE("Call `TickEvent()`");
        // Not found in the DB: we have a tick event.
        // Notify each active session whether it's interested in ending itself at this moment,
        // since some session types do use the "idle time" signal.
        // Also, this results in the output of the "current" sessions to actually be Current!
        uint64_t tmp = static_cast<uint64_t>(eid);
        assert(tmp % 1000 == 999);
        splitter.TickEvent(static_cast<uint64_t>(eid) / 1000, std::ref(data));
      }
      total_processed_entries = index + 1;
    });
    // TODO(dkorolev): Add extra logic to ensure this is safe.
    // Caveat: `Listener` gets deleted before its transactions are complete.
    // Obvious solution: Wrap `total_processed_entries` into a `shared_ptr`.
    // {
    //   PROFILER_SCOPE("transaction.Go()");
    //   transaction.Go();
    // }
    // total_processed_entries = index + 1;
    return true;
  }
};

// Top-level response: list of user-facing endpoints, and simple search.
struct TopLevelResponse {
  struct Route {
    std::string uri;
    std::string description;
    template <typename A>
    void serialize(A& ar) {
      ar(CEREAL_NVP(uri), CEREAL_NVP(description));
    }
  };
  std::vector<std::string> search_results;
  std::vector<Route> route = {{"/?q=<SEARCH_QUERY>", "This view, optionally with search results."},
                              {"/s", "Sessions browser (top-level)."},  // TODO(dkorolev): REST-ful interface.
                              {"/g?gid=<GID>", "Grouped events browser (mid-level)."},
                              {"/e?eid=<EID>", "Events details browser (low-level)."},
                              {"/log", "Raw events log, persisent connection."},
                              {"/stats", "Total counters."}};
  void Prepare(const std::string& query) {
    for (auto& route_entry : route) {
      route_entry.uri = FLAGS_output_uri_prefix + route_entry.uri;
    }
    if (!query.empty()) {
      Singleton<WaitableAtomic<SearchIndex>>().ImmutableUse([this, &query](const SearchIndex& index) {
        std::set<std::string> current;
        for (const auto& term : Split(ToLower(query), ::isalnum)) {
          const auto cit = index.terms.find(term);
          if (cit != index.terms.end()) {
            const std::set<std::string>& matches = cit->second;
            if (current.empty()) {
              current = matches;
            } else {
              std::set<std::string> intersected;
              std::set_intersection(current.begin(),
                                    current.end(),
                                    matches.begin(),
                                    matches.end(),
                                    std::inserter(intersected, intersected.begin()));
              if (!intersected.empty()) {
                current.swap(intersected);
              }
            }
          }
        }
        search_results.assign(current.rbegin(), current.rend());
        for (auto& uri : search_results) {
          uri = FLAGS_output_uri_prefix + uri;
        }
      });
    }
  }
  template <typename A>
  void serialize(A& ar) {
    ar(CEREAL_NVP(search_results), CEREAL_NVP(route));
  }
};

int main(int argc, char** argv) {
  ParseDFlags(&argc, &argv);

  if (FLAGS_route.empty() || FLAGS_route.back() != '/') {
    std::cerr << "`--route` should end with a slash." << std::endl;
    return -1;
  }

#ifdef PROFILER_ENABLED
  PROFILER_HTTP_ROUTE(FLAGS_port, FLAGS_profiler_route);
#endif

  HTTP(FLAGS_port).Register(FLAGS_route, [](Request r) {
    TopLevelResponse e;
    e.Prepare(r.url.query["q"]);
    r(e);
  });

  // "raw" is a raw stream of event identifiers (EID-s).
  // "raw" has tick events interleaved.
  // If a given EID can be found in the database, it's a user event, otherwise it's a tick event.
  // "raw" is to be internally listened to, it is not exposed over HTTP.
  auto raw = sherlock::Stream<EID>("raw");
  HTTP(FLAGS_port).Register(FLAGS_route + "ok", [](Request r) { r("OK\n"); });

  // "db" is a structured Yoda storage of processed events, sessions, and so on.
  // "db" is exposed via HTTP.
  DB db("db");

  // Expose events, without timestamps, under "/log" for subscriptions, and under "/e" for browsing.
  db.ExposeViaHTTP(FLAGS_port, FLAGS_route + "log");
  HTTP(FLAGS_port).Register(FLAGS_route + "e", [&db](Request r) {
    db.GetWithNext(static_cast<EID>(FromString<uint64_t>(r.url.query["eid"])), std::move(r));
  });

  Listener listener(db);
  auto scope = raw.SyncSubscribe(listener);

  std::atomic_size_t total_stream_entries(0);
  std::atomic_bool done_processing_stdin(false);
  std::atomic_bool graceful_shutdown(false);

  if (FLAGS_enable_graceful_shutdown) {
    HTTP(FLAGS_port).Register(FLAGS_route + "graceful_wait",
                              [&done_processing_stdin, &total_stream_entries, &listener](Request r) {
      while (!done_processing_stdin) {
        const size_t total = total_stream_entries;
        const size_t processed = listener.total_processed_entries;
        if (total) {
          std::cerr << processed * 100 / total << "% (" << processed << " / " << total
                    << ") entries processed.\n";
        } else {
          std::cerr << "Not done receiving entries from standard input.\n";
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
      }
      std::cerr << "All entries from standard input have been successfully processed.\n";
      r("Completed.\n");
    });
    HTTP(FLAGS_port).Register(FLAGS_route + "graceful_shutdown", [&graceful_shutdown](Request r) {
      graceful_shutdown = true;
      r("Bye.\n");
    });
  }

  // Read from standard input forever.
  // The rest of the logic is handled asynchronously, by the corresponding listeners.
  {
    PROFILER_SCOPE("BlockingParseLogEventsAndInjectIdleEventsFromStandardInput");
    total_stream_entries =
        BlockingParseLogEventsAndInjectIdleEventsFromStandardInput<MidichloriansEvent,
                                                                   MidichloriansEventWithTimestamp>(
            raw, db, FLAGS_port, FLAGS_route) +
        1;
  }

  if (FLAGS_enable_graceful_shutdown) {
    PROFILER_SCOPE("GracefulShutdown");
    while (listener.total_processed_entries != total_stream_entries) {
      ;  // Spin lock.
    }
    PROFILER_SCOPE("`done_processing_stdin = true`.");
    done_processing_stdin = true;
    scope.Join();
    // `curl` "/graceful_shutdown" to stop.
    while (!graceful_shutdown) {
      ;  // Spin lock.
    }
    return 0;
  } else {
    // Production code should never reach this point.
    // For non-production code, print an explanatory message before terminating.
    // Not terminating would be a bad idea, since it sure will break production one day. -- D.K.
    std::cerr << "Note: This binary is designed to run forever, and/or be restarted in an infinite loop.\n";
    std::cerr << "In test mode, to run against a small subset of data, consider `tail -n +1 -f input.txt`,\n";
    std::cerr << "or using the `--graceful_shutdown=true` mode, see `./run.sh` for more details.\n";
    return -1;
  }
}
