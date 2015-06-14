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
enum class ANSWER : int { UNSEEN = 0, CTFO = 1, TFU = 2, SKIP = -1 };
enum class AUTH_TYPE : int { UNDEFINED = 0, IOS };

const std::vector<unsigned int> LEVEL_SCORES{
    0,        // "Fish"
    15000,    // "Turkey"
    30000,    // "Rat"
    60000,    // "Pig"
    120000,   // "Octopus"
    240000,   // "Raven"
    480000,   // "Dolphin"
    960000,   // "Elephant"
    1920000,  // "Chimp"
    3840000   // "Skrik"
};

struct User : yoda::Padawan {
  UID uid = UID::INVALID;
  uint8_t level = 0u;   // User level [0, 9].
  uint64_t score = 0u;  // User score.

  UID key() const { return uid; }
  void set_key(UID value) { uid = value; }

  template <typename A>
  void serialize(A& ar) {
    Padawan::serialize(ar);
    ar(CEREAL_NVP(level), CEREAL_NVP(score));
  }
};

// AuthKey structure defines generic authentication key.
// `key` is supposed to be a long string, combined from several parameters (depending on the authentication type) with "::" as a delimiter.
// For `type = AUTH_TYPE::IOS`:
//   key = "iOS::" + <Device ID> + "::" + <Application Key>
//   Application key is a random number, generated once in a lifetime in iOS application on its first launch.
struct AuthKey {
  std::string key = "";
  AUTH_TYPE type = AUTH_TYPE::UNDEFINED;

  AuthKey() = default;
  AuthKey(const std::string& key, AUTH_TYPE type) : key(key), type(type) {}
  size_t Hash() const { return std::hash<std::string>()(key); }
  bool operator==(const AuthKey& rhs) const { return key == rhs.key && type == rhs.type; }

  template <typename A>
  void serialize(A& ar) {
    ar(CEREAL_NVP(key), CEREAL_NVP(type));
  }
};

struct AuthKeyTokenPair : yoda::Padawan {
  AuthKey auth_key;
  std::string token = "";
  bool valid = false;

  AuthKeyTokenPair() = default;
  AuthKeyTokenPair(const AuthKeyTokenPair&) = default;
  AuthKeyTokenPair(const AuthKey& auth_key, const std::string& token, bool valid = false)
      : auth_key(auth_key), token(token), valid(valid) {}

  const AuthKey& row() const { return auth_key; }
  void set_row(const AuthKey& value) { auth_key = value; }
  const std::string& col() const { return token; }
  void set_col(const std::string& value) { token = value; }

  template <typename A>
  void serialize(A& ar) {
    Padawan::serialize(ar);
    ar(CEREAL_NVP(auth_key), CEREAL_NVP(token), CEREAL_NVP(valid));
  }
};

struct AuthKeyUIDPair : yoda::Padawan {
  AuthKey auth_key;
  UID uid = UID::INVALID;

  AuthKeyUIDPair() = default;
  AuthKeyUIDPair(const AuthKey& auth_key, const UID uid) : auth_key(auth_key), uid(uid) {}

  const AuthKey& row() const { return auth_key; }
  void set_row(const AuthKey& value) { auth_key = value; }
  UID col() const { return uid; }
  void set_col(UID value) { uid = value; }

  template <typename A>
  void serialize(A& ar) {
    Padawan::serialize(ar);
    ar(CEREAL_NVP(auth_key), CEREAL_NVP(uid));
  }
};

struct Card : yoda::Padawan {
  CID cid = CID::INVALID;
  std::string text = "";     // Card text.
  uint64_t ctfo_count = 0u;  // Number of users, who said "CTFO" on this card.
  uint64_t tfu_count = 0u;   // Number of users, who said "TFU" on this card.
  uint64_t skip_count = 0u;  // Number of users, who said "SKIP" on this card.

  Card() = default;
  Card(const Card&) = default;
  Card(CID cid, const std::string& text) : cid(cid), text(text) {}

  CID key() const { return cid; }
  void set_key(CID value) { cid = value; }

  template <typename A>
  void serialize(A& ar) {
    Padawan::serialize(ar);
    ar(CEREAL_NVP(cid),
       CEREAL_NVP(text),
       CEREAL_NVP(ctfo_count),
       CEREAL_NVP(tfu_count),
       CEREAL_NVP(skip_count));
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
  std::string uid = "uINVALID";    // User id, format 'u01XXX...'.
  std::string token = "";          // User token.
  uint8_t level = 0u;              // User level, [0, 9].
  uint64_t score = 0u;             // User score.
  uint64_t next_level_score = 0u;  // Score value when user is promoted to the next level.

  template <typename A>
  void serialize(A& ar) {
    ar(CEREAL_NVP(uid), CEREAL_NVP(token), CEREAL_NVP(level), CEREAL_NVP(score), CEREAL_NVP(next_level_score));
  }
};

struct ResponseCardEntry {
  std::string cid = "cINVALID";  // Card id, format 'c02XXX...'.
  std::string text = "";         // Card text.
  double relevance = 0.0;        // Card relevance for particular user, [0.0, 1.0].
  uint64_t ctfo_score = 0u;      // Number of points, which user gets for "CTFO" answer.
  uint64_t tfu_score = 0u;       // Number of points, which user gets for "TFU" answer.
  double ctfo_percentage = 0.5;  // Proportion of users, who answered "CTFO" for this card, [0.0, 1.0].

  template <typename A>
  void serialize(A& ar) {
    ar(CEREAL_NVP(cid),
       CEREAL_NVP(text),
       CEREAL_NVP(relevance),
       CEREAL_NVP(ctfo_score),
       CEREAL_NVP(tfu_score),
       CEREAL_NVP(ctfo_percentage));
  }
};

// Universal response structure, combining user info & cards payload.
struct ResponseFeed {
  uint64_t ms;                           // Server timestamp, milliseconds from epoch.
  ResponseUserEntry user;                // User information.
  std::vector<ResponseCardEntry> cards;  // Cards feed.

  template <typename A>
  void serialize(A& ar) {
    ar(CEREAL_NVP(ms), CEREAL_NVP(user), CEREAL_NVP(cards));
  }
};

#endif  // CTFO_SCHEMA_H
