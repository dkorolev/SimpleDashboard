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

#ifndef CTFO_SCHEMA_H
#define CTFO_SCHEMA_H

#include <vector>
#include <string>

#include "../Current/Bricks/cerealize/cerealize.h"
#include "../Current/Sherlock/yoda/yoda.h"

// Data structures for internal storage.
enum class UID : uint64_t { INVALID = 0u };
enum class CID : uint64_t { INVALID = 0u };
enum class ANSWER : int { UNSEEN = 0, CTFO = 1, TFU = 2, TIFB = -1 };

struct User : yoda::Padawan {
  UID uid = UID::INVALID;
  uint64_t score = 0u;

  UID key() const { return uid; }
  void set_key(UID value) { uid = value; }

  template <typename A>
  void serialize(A& ar) {
    Padawan::serialize(ar);
    ar(CEREAL_NVP(score));
  }
};

struct UIDTokenPair : yoda::Padawan {
  UID uid = UID::INVALID;
  std::string token = "";
  bool valid = false;

  UIDTokenPair() = default;
  UIDTokenPair(const UIDTokenPair&) = default;
  UIDTokenPair(UID uid, const std::string& token, bool valid = false) : uid(uid), token(token), valid(valid) {}

  UID row() const { return uid; }
  void set_row(UID value) { uid = value; }
  const std::string& col() const { return token; }
  void set_col(const std::string& value) { token = value; }

  template <typename A>
  void serialize(A& ar) {
    Padawan::serialize(ar);
    ar(CEREAL_NVP(uid), CEREAL_NVP(token), CEREAL_NVP(valid));
  }
};

struct DeviceIdUIDPair : yoda::Padawan {
  std::string device_id = "";
  UID uid = UID::INVALID;

  const std::string& row() const { return device_id; }
  void set_row(const std::string& value) { device_id = value; }
  UID col() const { return uid; }
  void set_col(UID value) { uid = value; }

  template <typename A>
  void serialize(A& ar) {
    Padawan::serialize(ar);
    ar(CEREAL_NVP(device_id), CEREAL_NVP(uid));
  }
};

struct Card : yoda::Padawan {
  CID cid = CID::INVALID;
  std::string text = "";  // Text to display.
  uint64_t ctfo_count = 0;
  uint64_t tfu_count = 0;
  uint64_t tifb_count = 0;

  Card() = default;
  Card(const Card&) = default;
  Card(CID cid, const std::string& text) : cid(cid), text(text) {}

  CID key() const { return cid; }
  void set_key(CID value) { cid = value; }

  template <typename A>
  void serialize(A& ar) {
    Padawan::serialize(ar);
    ar(CEREAL_NVP(cid), CEREAL_NVP(text), CEREAL_NVP(ctfo_count), CEREAL_NVP(tfu_count),
       CEREAL_NVP(tifb_count));
  }
};

struct Answer : yoda::Padawan {
  UID uid = UID::INVALID;
  CID cid = CID::INVALID;
  ANSWER answer = ANSWER::UNSEEN;

  Answer() = default;
  Answer(const Answer&) = default;
  Answer(UID uid, CID cid, ANSWER answer) : uid(uid), cid(cid), answer(answer) {}

  UID row() const { return uid; }
  void set_row(UID value) { uid = value; }
  CID col() const { return cid; }
  void set_col(CID value) { cid = value; }

  template <typename A>
  void serialize(A& ar) {
    Padawan::serialize(ar);
    ar(CEREAL_NVP(uid), CEREAL_NVP(cid), CEREAL_NVP(answer));
  }
};

// Data structures for generating RESTful response.
struct ResponseUserEntry {
  std::string uid = "uINVALID";  // User id, format 'u01XXX...'.
  std::string token = "";        // User token.
  uint64_t score = 0u;           // User score.

  template <typename A>
  void serialize(A& ar) {
    ar(CEREAL_NVP(uid), CEREAL_NVP(token), CEREAL_NVP(score));
  }
};

struct ResponseCardEntry {
  std::string cid = "cINVALID";  // Card id, format 'c02XXX...'.
  std::string text = "";         // Card text.
  double relevance = 0.0;        // Card relevance for particular user, [0.0, 1.0].
  uint64_t score = 0u;           // Number of points, which user gets for "right" answer.
  double ctfo_percentage = 0.5;  // Percentage of users, who answered "CTFO" for this card.

  template <typename A>
  void serialize(A& ar) {
    ar(CEREAL_NVP(cid), CEREAL_NVP(text), CEREAL_NVP(relevance), CEREAL_NVP(score),
       CEREAL_NVP(ctfo_percentage));
  }
};

// Universal response structure, combining user info & cards payload.
struct ResponseFeed {
  uint64_t ts;                           // Feed timestamp.
  ResponseUserEntry user;                // User information.
  std::vector<ResponseCardEntry> cards;  // Cards feed.

  template <typename A>
  void serialize(A& ar) {
    ar(CEREAL_NVP(ts), CEREAL_NVP(user), CEREAL_NVP(cards));
  }
};

#endif  // CTFO_SCHEMA_H
