/*******************************************************************************
The MIT License (MIT)

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

#ifndef CTFO_UTIL_H
#define CTFO_UTIL_H

#include "../Current/Bricks/strings/strings.h"
#include "../Current/Bricks/util/random.h"

#include "schema.h"

using namespace bricks::random;
using namespace bricks::strings;

static constexpr uint64_t id_range_ = static_cast<uint64_t>(1e18);

inline UID RandomUID() { return static_cast<UID>(RandomUInt64(1 * id_range_ + 1, 2 * id_range_ - 1)); }

inline CID RandomCID() { return static_cast<CID>(RandomUInt64(2 * id_range_ + 1, 3 * id_range_ - 1)); }

inline std::string RandomToken() {
  return bricks::strings::Printf("t%020llu", RandomUInt64(3 * id_range_ + 1, 4 * id_range_ - 1));
}

inline std::string UIDToString(const UID uid) {
  return bricks::strings::Printf("u%020llu", static_cast<uint64_t>(uid));
}

inline UID StringToUID(const std::string& s) {
  if (s.length() == 21 && s[0] == 'u') {  // 'u' + 20 digits of `uint64_t` decimal representation.
    return static_cast<UID>(FromString<uint64_t>(s.substr(1)));
  }
  return UID::INVALID;
}

inline std::string CIDToString(const CID cid) {
  return bricks::strings::Printf("c%020llu", static_cast<uint64_t>(cid));
}

inline CID StringToCID(const std::string& s) {
  if (s.length() == 21 && s[0] == 'c') {  // 'c' + 20 digits of `uint64_t` decimal representation.
    return static_cast<CID>(FromString<uint64_t>(s.substr(1)));
  }
  return CID::INVALID;
}

#endif  // CTFO_UTIL_H
