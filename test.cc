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

#include "../Current/Bricks/dflags/dflags.h"
#include "../Current/Bricks/3party/gtest/gtest-main-with-dflags.h"

CEREAL_REGISTER_TYPE(User);
CEREAL_REGISTER_TYPE(UIDTokenPair);
CEREAL_REGISTER_TYPE(DeviceIdUIDPair);
CEREAL_REGISTER_TYPE(Card);
CEREAL_REGISTER_TYPE(Answer);

DEFINE_int32(port, 8383, "Port to spawn CTFO backend server on.");
DEFINE_int32(rand_seed, 42, "The answer to the question of life, universe and everything.");

TEST(CTFO, SmokeTest) {
  const int port = FLAGS_port;
  const int seed = FLAGS_rand_seed;
  CTFOServer server(port, seed);
  bricks::time::SetNow(static_cast<bricks::time::EPOCH_MILLISECONDS>(123));

  const std::string device_id_str = "A_BUNCH_OF_NUMBERS";
  const std::string golden_uid_str = "u01387193433063974682";
  const std::string golden_token_str = "t03611582183532963863";
  const char* device_id = device_id_str.c_str();
  const char* golden_uid = golden_uid_str.c_str();
  const char* golden_token = golden_token_str.c_str();

  const auto post_feed_response =
      HTTP(POST(Printf("http://localhost:%d/feed?uid=%s&token=%s", port, golden_uid, golden_token), ""));
  EXPECT_EQ(405, static_cast<int>(post_feed_response.code));
  EXPECT_EQ("METHOD NOT ALLOWED\n", post_feed_response.body);

  const auto no_auth_feed_response =
      HTTP(GET(Printf("http://localhost:%d/feed?uid=%s&token=%s", port, golden_uid, golden_token)));
  EXPECT_EQ(401, static_cast<int>(no_auth_feed_response.code));
  EXPECT_EQ("NEED VALID TOKEN\n", no_auth_feed_response.body);

  const auto no_device_id_auth_response =
      HTTP(POST(Printf("http://localhost:%d/auth/browser", port, golden_uid, golden_token), ""));
  EXPECT_EQ(400, static_cast<int>(no_device_id_auth_response.code));
  EXPECT_EQ("NEED VALID DEVICE ID\n", no_device_id_auth_response.body);

  ResponseFeed feed;
  const auto auth_response =
      HTTP(POST(Printf("http://localhost:%d/auth/browser?device_id=%s", port, device_id), ""));
  EXPECT_EQ(200, static_cast<int>(auth_response.code));
  feed = ParseJSON<ResponseFeed>(auth_response.body);
  EXPECT_EQ(123u, feed.ts);
  EXPECT_EQ(golden_uid_str, feed.user.uid);
  EXPECT_EQ(golden_token_str, feed.user.token);

  bricks::time::SetNow(static_cast<bricks::time::EPOCH_MILLISECONDS>(234));

  const auto feed_response = HTTP(
      GET(Printf("http://localhost:%d/feed?uid=%s&token=%s&feed_count=40", port, golden_uid, golden_token)));
  EXPECT_EQ(200, static_cast<int>(feed_response.code));
  feed = ParseJSON<ResponseFeed>(feed_response.body);
  EXPECT_EQ(234u, feed.ts);
  EXPECT_EQ(golden_uid_str, feed.user.uid);
  EXPECT_EQ(golden_token_str, feed.user.token);
  EXPECT_EQ(40u, feed.cards.size());
}
