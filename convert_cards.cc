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

#include "../Current/Bricks/dflags/dflags.h"
#include "../Current/Bricks/file/file.h"
#include "../Current/Bricks/strings/strings.h"
#include "../Current/Bricks/util/random.h"

#include "util.h"
#include "schema.h"

CEREAL_REGISTER_TYPE(Card);

using namespace bricks::strings;

DEFINE_string(in, "cards.txt", "Default input file in raw text format.");
DEFINE_string(out, "cards.json", "Default output file in JSON format.");

CID CIDByHash(const std::string& text) {
  CID cid = static_cast<CID>(std::hash<std::string>()(text) % ID_RANGE + 2 * ID_RANGE);
  return cid;
}

int main(int argc, char** argv) {
  ParseDFlags(&argc, &argv);
  std::vector<std::string> raw_cards;
  try {
    raw_cards = Split<ByLines>(bricks::FileSystem::ReadFileAsString(FLAGS_in));
  } catch (const bricks::CannotReadFileException& e) {
    std::cerr << "Unable to read file '" << FLAGS_in << "': " << e.what() << std::endl;
    return -1;
  }
  bricks::cerealize::CerealFileAppender<bricks::cerealize::CerealFormat::JSON> out_json(FLAGS_out);
  std::set<CID> cids;

  for (auto& text : raw_cards) {
    // Generate unique CID.
    CID cid;
    std::string addition = "";
    do {
      cid = CIDByHash(text + addition);
      addition += " ";
    } while (cids.find(cid) != cids.end());
    cids.insert(cid);

    out_json << Card(cid, text);
  }
}
