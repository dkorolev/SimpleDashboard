/*******************************************************************************
The MIT License (MIT)

Copyright (c) 2015 Maxim Zhurovich <zhurovich@gmail.com>
          (c) 2015 Dmitry "Dima" Korolev <dmitry.korolev@gmail.com>

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

#ifndef CTFO_SERVER_H
#define CTFO_SERVER_H

#include "../Current/Bricks/file/file.h"
#include "../Current/Bricks/net/api/api.h"
#include "../Current/Bricks/strings/strings.h"
#include "../Current/Bricks/util/random.h"

#include "../Current/EventCollector/event_collector.h"

#include "schema.h"
#include "util.h"

// Structured iOS events.
#include "../Current/Midichlorians/Dev/Beta/MidichloriansDataDictionary.h"

using namespace yoda;
using namespace bricks::random;
using namespace bricks::strings;

class CTFOServer {
 public:
  explicit CTFOServer(const std::string& cards_file,
                      int port,
                      int event_log_port,
                      const std::string& event_log_file,
                      const bricks::time::MILLISECONDS_INTERVAL tick_interval_ms,
                      const bool debug_print_to_stderr = false)
      : port_(port),
        event_log_file_(event_log_file),
        event_collector_(event_log_port ? event_log_port : port,
                         event_log_stream_,
                         tick_interval_ms,
                         "/ctfo/log",
                         "OK\n",
                         std::bind(&CTFOServer::OnMidichloriansEvent, this, std::placeholders::_1)),
        debug_print_(debug_print_to_stderr),
        storage_("CTFO storage") {
    event_log_stream_.open(event_log_file_, std::ofstream::out | std::ofstream::app);

    bricks::cerealize::CerealFileParser<Card, bricks::cerealize::CerealFormat::JSON> cf(cards_file);
    storage_.Transaction([&cf](StorageAPI::T_DATA data) {
                           while (cf.Next([&data](const Card& card) { data.Add(card); })) {
                             ;
                           }
                         }).Go();

    HTTP(port_).Register("/ctfo/auth/ios", [this](Request r) {
      if (r.method != "POST") {
        DebugPrint(Printf("[/ctfo/auth/ios] Wrong method '%s'. Requested URL = '%s'",
                          r.method.c_str(),
                          r.url.ComposeURL().c_str()));
        r("METHOD NOT ALLOWED\n", HTTPResponseCode.MethodNotAllowed);
      } else {
        const std::string device_id = r.url.query.get("id", "");
        const std::string app_key = r.url.query.get("key", "");
        if (device_id.empty() || app_key.empty()) {
          DebugPrint(Printf("[/ctfo/auth/ios] Wrong query parameters. Requested URL = '%s'",
                            r.url.ComposeURL().c_str()));
          r("NEED VALID ID-KEY PAIR\n", HTTPResponseCode.BadRequest);
        } else {
          const size_t feed_count = FromString<size_t>(r.url.query.get("feed_count", "20"));
          // Searching for users with the corresponding authentication key.
          storage_.Transaction(
              [this, device_id, app_key, feed_count](StorageAPI::T_DATA data) {
                AuthKey auth_key("iOS::" + device_id + "::" + app_key, AUTH_TYPE::IOS);
                UID uid = UID::INVALID;
                User user;
                ResponseUserEntry user_entry;
                std::string token;

                const auto auth_uid_accessor = Matrix<AuthKeyUIDPair>::Accessor(data);
                if (auth_uid_accessor.Rows().Has(auth_key)) {
                  // Something went terribly wrong
                  // if we have more than one UID for authentication key.
                  assert(auth_uid_accessor[auth_key].size() == 1);
                  uid = auth_uid_accessor[auth_key].begin()->uid;
                }

                auto auth_token_mutator = Matrix<AuthKeyTokenPair>::Mutator(data);
                if (uid != UID::INVALID) {
                  // User exists => invalidate all tokens.
                  for (const auto& auth_token : auth_token_mutator[auth_key]) {
                    auth_token_mutator.Add(AuthKeyTokenPair(auth_key, auth_token.token, false));
                  }
                  user = data.Get(uid);
                }

                // Generate a new token.
                do {
                  token = RandomToken();
                } while (auth_token_mutator.Cols().Has(token));
                auth_token_mutator.Add(AuthKeyTokenPair(auth_key, token, true));

                if (uid != UID::INVALID) {  // Existing user.
                  user_entry.score = user.score;
                  DebugPrint(
                      Printf("[/ctfo/auth/ios] Existing user: UID='%s', DeviceID='%s', AppKey='%s', Token='%s'",
                             UIDToString(uid).c_str(),
                             device_id.c_str(),
                             app_key.c_str(),
                             token.c_str()));
                } else {  // New user.
                  uid = RandomUID();
                  user.uid = uid;
                  data.Add(user);
                  data.Add(AuthKeyUIDPair(auth_key, user.uid));
                }
                DebugPrint(Printf("[/ctfo/auth/ios] New user: UID='%s', DeviceID='%s', AppKey='%s', Token='%s'",
                                  UIDToString(uid).c_str(),
                                  device_id.c_str(),
                                  app_key.c_str(),
                                  token.c_str()));

                CopyUserInfoToResponseEntry(user, user_entry);
                user_entry.token = token;

                ResponseFeed rfeed;
                GenerateResponseFeed(data, user_entry, feed_count, rfeed);
                return Response(rfeed);
              },
              std::move(r));
        }
      }
    });

    HTTP(port_).Register("/ctfo/feed", [this](Request r) {
      const UID uid = StringToUID(r.url.query["uid"]);
      const std::string token = r.url.query["token"];
      if (r.method != "GET") {
        DebugPrint(Printf("[/ctfo/feed] Wrong method '%s'. Requested URL = '%s'",
                          r.method.c_str(),
                          r.url.ComposeURL().c_str()));
        r("METHOD NOT ALLOWED\n", HTTPResponseCode.MethodNotAllowed);
      } else {
        if (uid == UID::INVALID) {
          DebugPrint(Printf("[/ctfo/feed] Wrong UID. Requested URL = '%s'", r.url.ComposeURL().c_str()));
          r("NEED VALID UID-TOKEN PAIR\n", HTTPResponseCode.BadRequest);
        } else {
          const size_t feed_count = FromString<size_t>(r.url.query.get("feed_count", "20"));
          const std::string requested_url = r.url.ComposeURL();
          storage_.Transaction(
              [this, uid, token, requested_url, feed_count](StorageAPI::T_DATA data) {
                bool token_is_valid = false;
                const auto auth_token_accessor = Matrix<AuthKeyTokenPair>::Accessor(data);
                if (auth_token_accessor.Cols().Has(token)) {
                  // Something went terribly wrong
                  // if we have more than one authentication key for token.
                  assert(auth_token_accessor[token].size() == 1);
                  if (auth_token_accessor[token].begin()->valid) {
                    // Double check, if the provided `uid` is correct as well.
                    const auto auth_uid_accessor = Matrix<AuthKeyUIDPair>::Accessor(data);
                    token_is_valid = auth_uid_accessor.Has(auth_token_accessor[token].begin().key(), uid);
                  }
                }
                if (!token_is_valid) {
                  DebugPrint(Printf("[/ctfo/feed] Invalid token. Requested URL = '%s'", requested_url.c_str()));
                  return Response("NEED VALID UID-TOKEN PAIR\n", HTTPResponseCode.Unauthorized);
                } else {
                  DebugPrint(
                      Printf("[/ctfo/feed] Token validated. Requested URL = '%s'", requested_url.c_str()));
                  const auto user = data.Get(uid);
                  ResponseUserEntry user_entry;
                  CopyUserInfoToResponseEntry(user, user_entry);
                  user_entry.token = token;
                  ResponseFeed rfeed;
                  GenerateResponseFeed(data, user_entry, feed_count, rfeed);
                  return Response(rfeed);
                }
              },
              std::move(r));
        }
      }
    });
  }

  void Join() { HTTP(port_).Join(); }

 private:
  const int port_;
  const std::string event_log_file_;
  std::ofstream event_log_stream_;
  EventCollectorHTTPServer event_collector_;
  const bool debug_print_;

  typedef API<Dictionary<User>,
              Matrix<AuthKeyTokenPair>,
              Matrix<AuthKeyUIDPair>,
              Dictionary<Card>,
              Matrix<Answer>> StorageAPI;
  StorageAPI storage_;

  const std::map<std::string, ANSWER> valid_answers_ = {
      {"CTFO", ANSWER::CTFO}, {"TFU", ANSWER::TFU}, {"SKIP", ANSWER::SKIP}};

  void DebugPrint(const std::string& message) {
    if (debug_print_) {
      std::cerr << message << std::endl;
    }
  }

  void CopyUserInfoToResponseEntry(const User& user, ResponseUserEntry& entry) {
    entry.uid = UIDToString(user.uid);
    entry.score = user.score;
    entry.level = user.level;
    if (user.level < LEVEL_SCORES.size() - 1) {
      entry.next_level_score = LEVEL_SCORES[user.level + 1];  // LEVEL_SCORES = { 0u, ... }
    } else {
      entry.next_level_score = 0u;
    }
  }

  void GenerateResponseFeed(StorageAPI::T_DATA& data,
                            ResponseUserEntry user_entry,
                            size_t feed_size,
                            ResponseFeed& response) {
    constexpr size_t FEED_SIZE_LIMIT = 300ul;
    const size_t max_count = std::min(feed_size, FEED_SIZE_LIMIT);
    response.user = user_entry;

    std::vector<CID> candidates;
    const UID uid = StringToUID(response.user.uid);
    const auto answers = Matrix<Answer>::Accessor(data);
    const auto cards = Dictionary<Card>::Accessor(data);
    for (const auto& card : cards) {
      if (!answers.Has(uid, card.cid)) {
        candidates.push_back(card.cid);
      }
    }
    std::shuffle(candidates.begin(), candidates.end(), mt19937_64_tls());

    auto GenerateCardForFeed = [this](const Card& card) {
      ResponseCardEntry card_entry;
      card_entry.cid = CIDToString(card.cid);
      card_entry.text = card.text;
      card_entry.relevance = RandomDouble(0, 1);
      card_entry.ctfo_score = 50u;
      card_entry.tfu_score = 50u;
      const uint64_t total_answers = card.ctfo_count + card.tfu_count;
      if (total_answers > 0) {
        card_entry.ctfo_percentage = static_cast<double>(card.ctfo_count) / total_answers;
      } else {
        card_entry.ctfo_percentage = 0.5;
      }
      return card_entry;
    };

    std::vector<std::reference_wrapper<std::vector<ResponseCardEntry>>> feeds;
    feeds.push_back(response.feed_hot);
    feeds.push_back(response.feed_recent);
    for (size_t i = 0; i < candidates.size() && (max_count ? (i < max_count * 2) : true); ++i) {
      auto& feed = feeds[i % 2].get();
      const CID cid = candidates[i];
      feed.push_back(GenerateCardForFeed(data.Get(cid)));
    }

    response.ms = static_cast<uint64_t>(bricks::time::Now());
    DebugPrint(Printf("[RespondWithFeed] Generated response for UID '%s' with %u 'hot' and %u 'recent' cards",
                      response.user.uid.c_str(),
                      response.feed_hot.size(),
                      response.feed_recent.size()));
  }

  void OnMidichloriansEvent(const LogEntry& entry) {
    std::unique_ptr<MidichloriansEvent> event;
    if (entry.m == "POST") {
      try {
        ParseJSON(entry.b, event);
        UpdateStateOnEvent(event);
      } catch (const bricks::ParseJSONException&) {
        DebugPrint(Printf("[OnMidichloriansEvent] ParseJSON failed. entry.b = '%s')", entry.b.c_str()));
      }
    } else {
      if (entry.m != "TICK") {
        DebugPrint(Printf(
            "[OnMidichloriansEvent] Suspicious event with method '%s' (t = %llu)", entry.m.c_str(), entry.t));
      }
    }
  }

  void UpdateStateOnEvent(const std::unique_ptr<MidichloriansEvent>& event) {
    try {
      const iOSGenericEvent& ge = dynamic_cast<const iOSGenericEvent&>(*event.get());
      try {
        const ANSWER answer = valid_answers_.at(ge.event);
        const UID uid = StringToUID(ge.fields.at("uid"));
        const CID cid = StringToCID(ge.fields.at("cid"));
        const std::string& uid_str = ge.fields.at("uid");
        const std::string& cid_str = ge.fields.at("cid");
        const std::string token = ge.fields.at("token");
        if (uid != UID::INVALID && cid != CID::INVALID) {
          storage_.Transaction([this, uid, cid, uid_str, cid_str, token, answer](
              StorageAPI::T_DATA data) {
            const auto auth_token_accessor = Matrix<AuthKeyTokenPair>::Accessor(data);
            bool token_is_valid = false;
            if (auth_token_accessor.Cols().Has(token)) {
              // Something went terribly wrong
              // if we have more than one authentication key for token.
              assert(auth_token_accessor[token].size() == 1);
              if (auth_token_accessor[token].begin()->valid) {
                token_is_valid = true;
              }
            }
            if (token_is_valid) {
              if (!data.Has(uid)) {
                DebugPrint(Printf("[UpdateStateOnEvent] Nonexistent UID '%s' in answer.", uid_str.c_str()));
                return;
              }
              if (!data.Has(cid)) {
                DebugPrint(Printf("[UpdateStateOnEvent] Nonexistent CID '%s' in answer.", cid_str.c_str()));
                return;
              }
              auto answers_mutator = Matrix<Answer>::Mutator(data);
              if (!answers_mutator.Has(uid, cid)) {  // Do not overwrite existing answers.
                data.Add(Answer(uid, cid, answer));
                DebugPrint(Printf("[UpdateStateOnEvent] Added new answer: [%s, %s, %d]",
                                  UIDToString(uid).c_str(),
                                  CIDToString(cid).c_str(),
                                  static_cast<int>(answer)));
                Card card = data.Get(cid);
                auto user_mutator = Dictionary<User>::Mutator(data);
                User user = user_mutator.Get(uid);
                if (answer != ANSWER::SKIP) {
                  if (answer == ANSWER::CTFO) {
                    ++card.ctfo_count;
                    DebugPrint(Printf("[UpdateStateOnEvent] Card '%s' new ctfo_count = %u",
                                      CIDToString(cid).c_str(),
                                      card.ctfo_count));
                    user.score += 50u;
                    DebugPrint(Printf("[UpdateStateOnEvent] User '%s' got %u points for 'CTFO' answer",
                                      UIDToString(uid).c_str(),
                                      50u));
                  }
                  if (answer == ANSWER::TFU) {
                    ++card.tfu_count;
                    DebugPrint(Printf("[UpdateStateOnEvent] Card '%s' new tfu_count = %u",
                                      CIDToString(cid).c_str(),
                                      card.tfu_count));
                    user.score += 50u;
                    DebugPrint(Printf("[UpdateStateOnEvent] User '%s' got %u points for 'TFU' answer",
                                      UIDToString(uid).c_str(),
                                      50u));
                  }
                  if (user.level < LEVEL_SCORES.size() - 1 && user.score > LEVEL_SCORES[user.level + 1]) {
                    user.score -= LEVEL_SCORES[user.level + 1];
                    ++user.level;
                    DebugPrint(Printf("[UpdateStateOnEvent] User '%s' got promoted to a new level = %u",
                                      UIDToString(uid).c_str(),
                                      user.level));
                  }
                  data.Add(card);
                  data.Add(user);
                }
              } else {
                DebugPrint(Printf("[UpdateStateOnEvent] Answer already exists: [%s, %s, %d]",
                                  UIDToString(uid).c_str(),
                                  CIDToString(cid).c_str(),
                                  static_cast<int>(static_cast<Answer>(answers_mutator.Get(uid, cid)).answer)));
              }
            }
            if (!token_is_valid) {
              DebugPrint(Printf("[UpdateStateOnEvent] Not valid token '%s' found in event.", token.c_str()));
            }
          });
        }
      } catch (const std::out_of_range& e) {
        DebugPrint(Printf("[UpdateStateOnEvent] std::out_of_range: '%s'", e.what()));
      }
    } catch (const std::bad_cast&) {
      // `event` is not an `iOSGenericEvent`.
    }
  }
};

#endif  // CTFO_SERVER_H
