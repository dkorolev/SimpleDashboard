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
#include <vector>
#include <string>
#include <random>

#include "../Current/Bricks/file/file.h"
#include "../Current/Bricks/strings/split.h"
#include "../Current/Bricks/net/api/api.h"

#include "schema.h"

using namespace yoda;
using namespace bricks::strings;

class CTFOServer {
 public:
  explicit CTFOServer(int http_port, int rand_seed)
      : http_port_(http_port),
        rng_(rand_seed),
        random_uid_(std::bind(
            std::uniform_int_distribution<uint64_t>(1000000000000000000ull, 1999999999999999999ull), rng_)),
        random_cid_(std::bind(
            std::uniform_int_distribution<uint64_t>(2000000000000000000ull, 2999999999999999999ull), rng_)),
        random_token_(std::bind(
            std::uniform_int_distribution<uint64_t>(3000000000000000000ull, 3999999999999999999ull), rng_)),
        random_0_1_picker_(std::bind(std::uniform_real_distribution<double>(0.0, 1.0), rng_)),
        random_10_99_picker_(std::bind(std::uniform_int_distribution<int>(10, 99), rng_)),
        storage_("CTFO storage"),
        cards_(Split<ByLines>(bricks::FileSystem::ReadFileAsString("cards.txt"))) {
    assert(cards_.size() >= 2u);
    assert(cards_[0] == "ZERO_INDEX_SHOULD_BE_KEPT_UNUSED");

    storage_.Transaction([this](StorageAPI::T_DATA data) {
      for (const auto& text : cards_) {
        CID cid;
        do {
          cid = RandomCID();
        } while (data.Has(cid));
        Card card(cid, text);
        card.ctfo_count = random_10_99_picker_();
        card.tfu_count = random_10_99_picker_();
        card.tifb_count = random_10_99_picker_();
        data.Add(card);
      }
    });

    HTTP(http_port_).Register("/auth/browser", [this](Request r) {
      if (r.method != "POST") {
        r("METHOD NOT ALLOWED\n", HTTPResponseCode.MethodNotAllowed);
      } else {
        const std::string device_id = r.url.query.get("device_id", "");
        if (device_id.empty()) {
          r("NEED VALID DEVICE ID\n", HTTPResponseCode.BadRequest);
        } else {
          UID uid = UID::INVALID;
          User user;
          ResponseUserEntry user_entry;

          // Searching for users with provided device ID.
          storage_.Transaction([&](StorageAPI::T_DATA data) {
                                 const auto accessor = MatrixEntry<DeviceIdUIDPair>::Accessor(data);
                                 if (accessor.Rows().Has(device_id)) {
                                   // Something went terribly wrong
                                   // if we have more than one UID for the device ID.
                                   assert(accessor[device_id].size() == 1);
                                   uid = accessor[device_id].begin()->uid;
                                 }
                               }).Go();

          if (uid != UID::INVALID) {  // Existing user.
            // Invalidating all old tokens.
            storage_.Transaction([&](StorageAPI::T_DATA data) {
                                   auto mutator = MatrixEntry<UIDTokenPair>::Mutator(data);
                                   for (const auto& uid_token : mutator[uid]) {
                                     mutator.Add(UIDTokenPair(uid_token.uid, uid_token.token, false));
                                   }
                                   user = data.Get(uid);
                                 }).Go();
            user_entry.score = user.score;
          } else {  // New user.
            uid = RandomUID();
            user.uid = uid;
            storage_.Add(user).Go();
          }

          CopyUserInfoToResponseEntry(user, user_entry);
          // Generate a new token.
          const std::string token = RandomToken();
          user_entry.token = token;
          storage_.Add(UIDTokenPair(uid, token, true)).Go();

          RespondWithFeed(user_entry, FromString<size_t>(r.url.query.get("feed_count", "20")), std::move(r));
        }
      }
    });

    HTTP(http_port_).Register("/feed", [this](Request r) {
      const UID uid = StringToUID(r.url.query["uid"]);
      const std::string token = r.url.query["token"];
      if (r.method != "GET") {
        r("METHOD NOT ALLOWED\n", HTTPResponseCode.MethodNotAllowed);
      } else {
        if (uid == UID::INVALID) {
          r("NEED VALID UID\n", HTTPResponseCode.BadRequest);
        } else {
          bool valid_token = storage_.Get(uid, token).Go();
          if (!valid_token) {
            r("NEED VALID TOKEN\n", HTTPResponseCode.Unauthorized);
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
          const auto answers = MatrixEntry<Answer>::Accessor(data);
          const auto cards = Dictionary<Card>::Accessor(data);
          for (const auto& card : cards) {
            if (!answers.Get(uid, card.cid)) {
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
            card_entry.score = random_10_99_picker_();
            const uint64_t total_answers = card.ctfo_count + card.tfu_count;
            if (total_answers > 0) {
              card_entry.ctfo_percentage = static_cast<double>(card.ctfo_count) / total_answers;
            } else {
              card_entry.ctfo_percentage = 0.5;
            }
          }

          response.ts = static_cast<uint64_t>(bricks::time::Now());
          return response;
        },
        std::move(r));
  }

  void Join() { HTTP(http_port_).Join(); }

 private:
  const int http_port_;
  std::mt19937_64 rng_;
  std::function<uint64_t()> random_uid_;
  std::function<uint64_t()> random_cid_;
  std::function<uint64_t()> random_token_;
  std::function<double()> random_0_1_picker_;
  std::function<int()> random_10_99_picker_;

  typedef API<Dictionary<User>,
              MatrixEntry<UIDTokenPair>,
              MatrixEntry<DeviceIdUIDPair>,
              Dictionary<Card>,
              MatrixEntry<Answer>> StorageAPI;
  StorageAPI storage_;
  std::vector<std::string> cards_;

  const std::map<std::string, ANSWER> valid_answers_ = {
      {"ctfo", ANSWER::CTFO}, {"tfu", ANSWER::TFU}, {"tifb", ANSWER::TIFB}};

  UID RandomUID() { return static_cast<UID>(random_uid_()); }
  CID RandomCID() { return static_cast<CID>(random_cid_()); }
  std::string RandomToken() { return bricks::strings::Printf("t%020llu", random_token_()); }

  std::string UIDToString(UID uid) { return bricks::strings::Printf("u%020llu", static_cast<uint64_t>(uid)); }

  static UID StringToUID(const std::string& s) {
    if (s.length() == 21 && s[0] == 'u') {  // 'u' + 20 digits of `uint64_t` decimal representation;
      return static_cast<UID>(FromString<uint64_t>(s.substr(1)));
    }
    return UID::INVALID;
  }

  std::string CIDToString(CID cid) { return bricks::strings::Printf("c%020llu", static_cast<uint64_t>(cid)); }

  static CID StringToCID(const std::string& s) {
    if (s.length() == 21 && s[0] == 'c') {  // 'c' + 20 digits of `uint64_t` decimal representation;
      return static_cast<CID>(FromString<uint64_t>(s.substr(1)));
    }
    return CID::INVALID;
  }

  void CopyUserInfoToResponseEntry(const User& user, ResponseUserEntry& entry) {
    entry.uid = UIDToString(user.uid);
    entry.score = user.score;
  }
};

#endif  // CTFO_SERVER_H
