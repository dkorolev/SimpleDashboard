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

#include "../Current/Bricks/cerealize/cerealize.h"
#include "../Current/Bricks/dflags/dflags.h"
#include "../Current/Bricks/file/file.h"

#include "cubes.h"

DEFINE_string(input, "data/cube_input.json", "");
DEFINE_string(output, "data/cube.tsv", "");

using bricks::FileSystem;

struct Cell {
  // Coordinates are [Dimension name, Bin name].
  typedef std::pair<std::string, std::string> Coordinate;
  std::vector<Coordinate> coordinates;

  Cell() = delete;
  Cell(const Space& space) {
    for (const auto& dim : space.dimensions) {
      coordinates.emplace_back(dim.name, NOT_SET_BIN_NAME);
    }
  }

  bool SetCoordinate(const std::string& dimension, const std::string& value) {
    for (auto& it : coordinates) {
      if (it.first == dimension) {
        it.second = value;
        return true;
      }
    }
    return false;
  }
};

int main(int argc, char** argv) {
  ParseDFlags(&argc, &argv);

  fprintf(stderr, "Reading '%s' ...\n", FLAGS_input.c_str());
  fflush(stderr);
  auto input = ParseJSON<CubeGeneratorInput>(FileSystem::ReadFileAsString(FLAGS_input));
  fprintf(stderr,
          "Done reading file. Got %lu dimensions with %lu sessions.\n",
          input.space.dimensions.size(),
          input.sessions.size());
  const auto& dimensions = input.space.dimensions;
  const auto& sessions = input.sessions;
  std::ofstream fo(FLAGS_output);
  std::string header;
  for (const Dimension& dim : dimensions) {
    header += "FEATURE|" + dim.name;
    for (const Bin& bin : dim.bins) {
      header += "|" + bin.name;
    }
    header += "\t";
  }
  header += "TOTAL|" + std::to_string(sessions.size()) + "\n";
  fo << header;

  for (const auto& session : input.sessions) {
    Cell cell(input.space);
    for (const auto& feature_counter : session.feature_count) {
      const std::string feature = feature_counter.first;
      std::string dimension_name;
      std::string bin_name;
      if (feature != TIME_DIMENSION_NAME) {
        const auto dim_bin = input.space.SplitFeatureIntoDimensionAndBinNames(feature);
        if (dim_bin.first.empty()) {
          continue;
        }
        dimension_name = dim_bin.first;
        if (!dim_bin.second.empty()) {
          bin_name = dim_bin.second;
        }
      } else {  // Time dimension.
        dimension_name = feature;
      }
      if (bin_name.empty()) {
        Dimension* dim = input.space.DimensionByName(dimension_name);
        assert(dim);
        bin_name = dim->BinNameByValue(feature_counter.second);
        assert(!bin_name.empty());
      }
      cell.SetCoordinate(dimension_name, bin_name);
    }

    bool first = true;
    for (const auto& cit : cell.coordinates) {
      if (first) {
        first = false;
      } else {
        fo << "\t";
      }
      fo << cit.second;
    }
    fo << "\t1\n";
  }
}
