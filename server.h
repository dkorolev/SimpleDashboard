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

#include <cassert>
#include <cstdlib>
#include <functional>
#include <random>
#include <string>
#include <vector>

#include "../Current/Bricks/file/file.h"
#include "../Current/Bricks/strings/split.h"
#include "../Current/Bricks/net/api/api.h"

#include "../Current/EventCollector/event_collector.h"

#include "schema.h"

using namespace yoda;
using namespace bricks::strings;

class CTFOServer {
 public:
  explicit CTFOServer(int rand_seed,
                      int port,
                      int event_log_port,
                      const std::string& event_log_file,
                      const bricks::time::MILLISECONDS_INTERVAL tick_interval_ms)
      : rng_(rand_seed),
        port_(port),
        event_log_file_(event_log_file),
        event_collector_(event_log_port ? event_log_port : port,
                         event_log_stream_,
                         tick_interval_ms,
                         "/ctfo/log",
                         "OK\n",
                         std::bind(&CTFOServer::OnMidichloriansEvent, this, std::placeholders::_1)),
        storage_("CTFO storage"),
        cards_(Split<ByLines>(bricks::FileSystem::ReadFileAsString("cards.txt"))) {
    event_log_stream_.open(event_log_file_, std::ofstream::out | std::ofstream::app);

    storage_.Transaction([this](StorageAPI::T_DATA data) {
      for (const auto& text : cards_) {
        CID cid;
        do {
          cid = RandomCID();
        } while (data.Has(cid));
        Card card(cid, text);
        card.ctfo_count = random_10_99_picker_();
        card.tfu_count = random_10_99_picker_();
        card.skip_count = random_10_99_picker_();
        data.Add(card);
      }
    });

    HTTP(port_).Register("/ctfo/auth/ios", [this](Request r) {
      if (r.method != "POST") {
        r("METHOD NOT ALLOWED\n", HTTPResponseCode.MethodNotAllowed);
      } else {
        const std::string device_id = r.url.query.get("id", "");
        const std::string app_key = r.url.query.get("key", "");
        if (device_id.empty() || app_key.empty()) {
          r("NEED VALID ID-KEY PAIR\n", HTTPResponseCode.BadRequest);
        } else {
          AuthKey auth_key("iOS::" + device_id + "::" + app_key, AUTH_TYPE::IOS);
          UID uid = UID::INVALID;
          User user;
          ResponseUserEntry user_entry;
          std::string token;

          // Searching for users with the authentication key, consisting of `device_id` and `app_key`.
          storage_.Transaction([this, &uid, &auth_key, &user, &token](StorageAPI::T_DATA data) {
                                 const auto auth_uid_accessor = Matrix<AuthKeyUIDPair>::Accessor(data);
                                 if (auth_uid_accessor.Rows().Has(auth_key)) {
                                   // Something went terribly wrong
                                   // if we have more than one UID for `device_id` + `app_key` pair.
                                   assert(auth_uid_accessor[auth_key].size() == 1);
                                   uid = auth_uid_accessor[auth_key].begin()->uid;
                                 }

                                 auto auth_token_mutator = Matrix<AuthKeyTokenPair>::Mutator(data);
                                 if (uid != UID::INVALID) {
                                   // User exists => invalidate all tokens.
                                   for (const auto& auth_token : auth_token_mutator[auth_key]) {
                                     auth_token_mutator.Add(
                                         AuthKeyTokenPair(auth_key, auth_token.token, false));
                                   }
                                   user = data.Get(uid);
                                 }

                                 // Generate a new token.
                                 do {
                                   token = RandomToken();
                                 } while (auth_token_mutator.Cols().Has(token));
                                 auth_token_mutator.Add(AuthKeyTokenPair(auth_key, token, true));
                               }).Go();

          if (uid != UID::INVALID) {  // Existing user.
            user_entry.score = user.score;
          } else {  // New user.
            uid = RandomUID();
            user.uid = uid;
            storage_.Transaction([&user, &auth_key](StorageAPI::T_DATA data) {
                                   data.Add(user);
                                   data.Add(AuthKeyUIDPair(auth_key, user.uid));
                                }).Go();
          }

          CopyUserInfoToResponseEntry(user, user_entry);
          user_entry.token = token;

          RespondWithFeed(user_entry, FromString<size_t>(r.url.query.get("feed_count", "20")), std::move(r));
        }
      }
    });

    HTTP(port_).Register("/ctfo/feed", [this](Request r) {
      const UID uid = StringToUID(r.url.query["uid"]);
      const std::string token = r.url.query["token"];
      if (r.method != "GET") {
        r("METHOD NOT ALLOWED\n", HTTPResponseCode.MethodNotAllowed);
      } else {
        if (uid == UID::INVALID) {
          r("NEED VALID UID-TOKEN PAIR\n", HTTPResponseCode.BadRequest);
        } else {
          bool valid_token = false;
          storage_.Transaction([&uid, &token, &valid_token](StorageAPI::T_DATA data) {
                                 const auto auth_token_accessor = Matrix<AuthKeyTokenPair>::Accessor(data);
                                 if (auth_token_accessor.Cols().Has(token)) {
                                   // Something went terribly wrong
                                   // if we have more than one `device_id` + `app_key` pair for token.
                                   assert(auth_token_accessor[token].size() == 1);
                                   if (auth_token_accessor[token].begin()->valid) {
                                     // Double check, if the provided `uid` is correct as well.
                                     const auto auth_uid_accessor = Matrix<AuthKeyUIDPair>::Accessor(data);
                                     valid_token = auth_uid_accessor.Has(auth_token_accessor[token].begin().key(), uid);
                                   }
                                 }
                               }).Go();
          if (!valid_token) {
            r("NEED VALID UID-TOKEN PAIR\n", HTTPResponseCode.Unauthorized);
          } else {
            const auto user = storage_.Get(uid).Go();
            ResponseUserEntry user_entry;
            CopyUserInfoToResponseEntry(user, user_entry);
            user_entry.token = token;
            RespondWithFeed(user_entry, FromString<size_t>(r.url.query.get("feed_count", "20")), std::move(r));
          }
        }
      }
    });
  }

  void RespondWithFeed(ResponseUserEntry user_entry, size_t max_count, Request r) {
    storage_.Transaction(
        [this, user_entry, max_count](StorageAPI::T_DATA data) {
          ResponseFeed response;
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
          std::shuffle(candidates.begin(), candidates.end(), rng_);

          for (size_t i = 0; i < candidates.size() && (max_count ? (i < max_count) : true); ++i) {
            const CID cid = candidates[i];
            response.cards.resize(response.cards.size() + 1);
            ResponseCardEntry& card_entry = response.cards.back();
            card_entry.cid = CIDToString(cid);
            const Card& card = data.Get(cid);
            card_entry.text = card.text;
            card_entry.relevance = random_0_1_picker_();
            card_entry.ctfo_score = 50u;
            card_entry.tfu_score = 50u;
            const uint64_t total_answers = card.ctfo_count + card.tfu_count;
            if (total_answers > 0) {
              card_entry.ctfo_percentage = static_cast<double>(card.ctfo_count) / total_answers;
            } else {
              card_entry.ctfo_percentage = 0.5;
            }
          }

          response.ms = static_cast<uint64_t>(bricks::time::Now());
          return response;
        },
        std::move(r));
  }

  void Join() { HTTP(port_).Join(); }

 private:
  std::mt19937_64 rng_;
  const int port_;
  const std::string event_log_file_;
  std::ofstream event_log_stream_;
  EventCollectorHTTPServer event_collector_;

  static constexpr uint64_t id_range_ = static_cast<uint64_t>(1e18);
  std::function<uint64_t()> random_uid_ =
      std::bind(std::uniform_int_distribution<uint64_t>(1 * id_range_, 2 * id_range_ - 1), std::ref(rng_));
  std::function<uint64_t()> random_cid_ =
      std::bind(std::uniform_int_distribution<uint64_t>(2 * id_range_, 3 * id_range_ - 1), std::ref(rng_));
  std::function<uint64_t()> random_token_ =
      std::bind(std::uniform_int_distribution<uint64_t>(3 * id_range_, 4 * id_range_ - 1), std::ref(rng_));
  std::function<double()> random_0_1_picker_ =
      std::bind(std::uniform_real_distribution<double>(0.0, 1.0), std::ref(rng_));
  std::function<int()> random_10_99_picker_ =
      std::bind(std::uniform_int_distribution<int>(10, 99), std::ref(rng_));

  typedef API<Dictionary<User>,
              Matrix<AuthKeyTokenPair>,
              Matrix<AuthKeyUIDPair>,
              Dictionary<Card>,
              Matrix<Answer>> StorageAPI;
  StorageAPI storage_;
  std::vector<std::string> cards_;

  const std::map<std::string, ANSWER> valid_answers_ = {
      {"CTFO", ANSWER::CTFO}, {"TFU", ANSWER::TFU}, {"SKIP", ANSWER::SKIP}};

  UID RandomUID() { return static_cast<UID>(random_uid_()); }
  CID RandomCID() { return static_cast<CID>(random_cid_()); }
  std::string RandomToken() { return bricks::strings::Printf("t%020llu", random_token_()); }

  std::string UIDToString(UID uid) { return bricks::strings::Printf("u%020llu", static_cast<uint64_t>(uid)); }

  static UID StringToUID(const std::string& s) {
    if (s.length() == 21 && s[0] == 'u') {  // 'u' + 20 digits of `uint64_t` decimal representation.
      return static_cast<UID>(FromString<uint64_t>(s.substr(1)));
    }
    return UID::INVALID;
  }

  std::string CIDToString(CID cid) { return bricks::strings::Printf("c%020llu", static_cast<uint64_t>(cid)); }

  static CID StringToCID(const std::string& s) {
    if (s.length() == 21 && s[0] == 'c') {  // 'c' + 20 digits of `uint64_t` decimal representation.
      return static_cast<CID>(FromString<uint64_t>(s.substr(1)));
    }
    return CID::INVALID;
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

  void OnMidichloriansEvent(const LogEntry& entry) {
    // TODO(mzhurovich): update answers here.
    static_cast<void>(entry);
  }
};

#endif  // CTFO_SERVER_H
