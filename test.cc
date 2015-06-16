/*******************************************************************************
The MIT License (MIT)

Copyright (c) 2015 Dmitry "Dima" Korolev <dmitry.korolev@gmail.com>
          (c) 2015 Maxim Zhurovich <zhurovich@gmail.com>

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

#define BRICKS_MOCK_TIME

#include "server.h"

#include "../Current/Bricks/file/file.h"

#include "../Current/Bricks/dflags/dflags.h"
#include "../Current/3rdparty/gtest/gtest-main-with-dflags.h"

CEREAL_REGISTER_TYPE(User);
CEREAL_REGISTER_TYPE(AuthKeyTokenPair);
CEREAL_REGISTER_TYPE(AuthKeyUIDPair);
CEREAL_REGISTER_TYPE(Card);
CEREAL_REGISTER_TYPE(Answer);

DEFINE_int32(api_port, 8383, "Port to spawn CTFO RESTful server on.");
DEFINE_int32(event_log_port, 8384, "Port to spawn event collector on.");

TEST(CTFO, SmokeTest) {
  const std::string log_file = bricks::FileSystem::GenTmpFileName();
  bricks::FileSystem::ScopedRmFile scoped_rmfile(log_file);

  bricks::random::SetSeed(42);
  CTFOServer server(
      FLAGS_api_port, FLAGS_event_log_port, log_file, static_cast<bricks::time::MILLISECONDS_INTERVAL>(100));
  bricks::time::SetNow(static_cast<bricks::time::EPOCH_MILLISECONDS>(123));

  const std::string auth_id_str = "A_BUNCH_OF_DIGITS";
  const std::string auth_key_str = "1234567890abcdef";
  const std::string some_uid_str = "u01000000000000000001";
  const std::string some_token_str = "t03000000000000000001";
  const char* auth_id = auth_id_str.c_str();
  const char* auth_key = auth_key_str.c_str();
  const char* some_uid = some_uid_str.c_str();
  const char* some_token = some_token_str.c_str();

  const auto post_feed_response = HTTP(
      POST(Printf("http://localhost:%d/ctfo/feed?uid=%s&token=%s", FLAGS_api_port, some_uid, some_token), ""));
  EXPECT_EQ(405, static_cast<int>(post_feed_response.code));
  EXPECT_EQ("METHOD NOT ALLOWED\n", post_feed_response.body);

  const auto no_auth_feed_response =
      HTTP(GET(Printf("http://localhost:%d/ctfo/feed?uid=%s&token=%s", FLAGS_api_port, some_uid, some_token)));
  EXPECT_EQ(401, static_cast<int>(no_auth_feed_response.code));
  EXPECT_EQ("NEED VALID UID-TOKEN PAIR\n", no_auth_feed_response.body);

  const auto no_device_id_auth_response =
      HTTP(POST(Printf("http://localhost:%d/ctfo/auth/ios", FLAGS_api_port, some_uid, some_token), ""));
  EXPECT_EQ(400, static_cast<int>(no_device_id_auth_response.code));
  EXPECT_EQ("NEED VALID ID-KEY PAIR\n", no_device_id_auth_response.body);

  ResponseFeed response;
  const auto auth_response = HTTP(
      POST(Printf("http://localhost:%d/ctfo/auth/ios?id=%s&key=%s", FLAGS_api_port, auth_id, auth_key), ""));
  EXPECT_EQ(200, static_cast<int>(auth_response.code));
  response = ParseJSON<ResponseFeed>(auth_response.body);
  EXPECT_EQ(123u, response.ms);
  const std::string actual_uid = response.user.uid;
  const std::string actual_token = response.user.token;

  bricks::time::SetNow(static_cast<bricks::time::EPOCH_MILLISECONDS>(234));

  const auto feed_response = HTTP(GET(Printf("http://localhost:%d/ctfo/feed?uid=%s&token=%s&feed_count=40",
                                             FLAGS_api_port,
                                             actual_uid.c_str(),
                                             actual_token.c_str())));
  EXPECT_EQ(200, static_cast<int>(feed_response.code));
  response = ParseJSON<ResponseFeed>(feed_response.body);
  EXPECT_EQ(234u, response.ms);
  EXPECT_EQ(actual_uid, response.user.uid);
  EXPECT_EQ(actual_token, response.user.token);
  EXPECT_EQ(0u, response.user.level);
  EXPECT_EQ(0u, response.user.score);
  EXPECT_EQ(15000u, response.user.next_level_score);
  EXPECT_EQ(40u, response.feed_hot.size());
  EXPECT_EQ(40u, response.feed_recent.size());

  std::unordered_set<std::string> hot_cids;
  std::unordered_set<std::string> hot_texts;
  std::unordered_set<std::string> recent_cids;
  std::unordered_set<std::string> recent_texts;
  for (const ResponseCardEntry& card : response.feed_hot) {
    hot_cids.insert(card.cid);
    hot_texts.insert(card.text);
  }
  for (const ResponseCardEntry& card : response.feed_recent) {
    recent_cids.insert(card.cid);
    recent_texts.insert(card.text);
  }
  EXPECT_EQ(40u, hot_cids.size());
  EXPECT_EQ(40u, hot_texts.size());
  EXPECT_EQ(40u, recent_cids.size());
  EXPECT_EQ(40u, recent_texts.size());

  std::vector<std::string> cids_intersection;
  std::set_intersection(hot_cids.begin(),
                        hot_cids.end(),
                        recent_cids.begin(),
                        recent_cids.end(),
                        std::back_inserter(cids_intersection));
  EXPECT_EQ(0u, cids_intersection.size());
  std::vector<std::string> texts_intersection;
  std::set_intersection(hot_texts.begin(),
                        hot_texts.end(),
                        recent_texts.begin(),
                        recent_texts.end(),
                        std::back_inserter(texts_intersection));
  EXPECT_EQ(0u, texts_intersection.size());
}
