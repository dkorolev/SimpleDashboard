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

#include <string>
#include <vector>
#include <map>

const std::string TIME_DIMENSION_NAME = "Session length (seconds)";
const std::string DEVICE_DIMENSION_NAME = "Device";
const std::string NOT_SET_BIN_NAME = "Not set";

struct Bin {
  enum class Type : int { INVALID = -1, NOT_SET, TEXT, INTEGRAL };
  enum class RangeType : int { INVALID = -1, EXACT_MATCH, GREATER, LESS, INTERVAL };

  Type type = Type::INVALID;
  RangeType range_type = RangeType::INVALID;
  std::string name = "";
  std::string text_value = "";
  size_t a = 0u;
  size_t b = 0u;

  Bin() = default;
  Bin(const std::string& name) : type(Type::NOT_SET), name(name) {}
  Bin(const std::string& name, const std::string& value)
      : type(Type::TEXT), range_type(RangeType::EXACT_MATCH), name(name), text_value(value) {}
  Bin(const std::string& name, size_t a, size_t b, RangeType range_type)
      : type(Type::INTEGRAL), range_type(range_type), name(name), a(a), b(b) {}
  Bin(const std::string& name, size_t value, RangeType range_type)
      : type(Type::INTEGRAL), range_type(range_type), name(name), a(value), b(value) {
    assert(range_type == RangeType::LESS || range_type == RangeType::GREATER ||
           range_type == RangeType::EXACT_MATCH);
  }

  bool MatchValue(const std::string& rhs) const {
    assert(type == Type::TEXT);
    return (text_value == rhs);
  }

  bool MatchValue(size_t rhs) const {
    assert(type == Type::INTEGRAL);
    switch (range_type) {
      case RangeType::EXACT_MATCH:
        assert(a == b);
        return (rhs == a);
        break;
      case RangeType::GREATER:
        return (rhs > a);
        break;
      case RangeType::LESS:
        return (rhs < b);
        break;
      case RangeType::INTERVAL:
        return (rhs >= a && rhs <= b);
        break;
      default:
        return false;
    }
  }

  template <typename A>
  void serialize(A& ar) {
    ar(CEREAL_NVP(type),
       CEREAL_NVP(range_type),
       CEREAL_NVP(name),
       CEREAL_NVP(text_value),
       CEREAL_NVP(a),
       CEREAL_NVP(b));
  }
};

struct Dimension {
  std::string name;
  std::vector<Bin> bins;

  Dimension() = default;
  Dimension(const std::string& name) : name(name) {}

  template <typename T>
  std::string BinNameByValue(T value) const {
    for (const auto& cit : bins) {
      if (cit.type != Bin::Type::NOT_SET && cit.MatchValue(value)) {
        return cit.name;
      }
    }
    return std::string("");
  }

  void AddBinIfNotExists(const Bin& value) {
    bool exists = false;
    for (const auto& cit : bins) {
      if (cit.name == value.name) {
        exists = true;
        break;
      }
    }
    if (!exists) {
      bins.push_back(value);
    }
  }

  void SmartCreateBins(const std::map<size_t, size_t>& stats) {
    std::cout << "Stats size = " << stats.size() << std::endl;
    std::vector<size_t> marks;
    // Small amount of distinct values => base bin ranges on them directly.
    if (stats.size() <= 5) {
      for (const auto& cit : stats) {
        marks.push_back(cit.first);
      }
    } else {
      // Create marks by simply splitting the whole range into equal parts.
      // TODO: use more sophisticated way, accounting not only `stats` keys, but also values.
      const size_t min_event_count = stats.begin()->first;
      const size_t max_event_count = stats.rbegin()->first;
      size_t delta = (max_event_count - min_event_count) / 4;
      for (int i = 0; i <= 4; ++i) {
        marks.push_back(min_event_count + delta * i);
      }
    }

    // Only one value => fill with exact match.
    if (marks.size() == 1u) {
      Bin the_only(std::to_string(marks[0]), marks[0], Bin::RangeType::EXACT_MATCH);
      bins.push_back(the_only);
      return;
    }

    // Generic case: 2+ marks.
    for (size_t i = 0; i < marks.size() - 1u; ++i) {
      const size_t a = marks[i];
      size_t b = marks[i + 1] - 1;
      if (i == 0 && a != 1u) {
        if (a == 2u) {
          Bin first_bin("1", 1, Bin::RangeType::EXACT_MATCH);
          bins.push_back(first_bin);
        } else {
          Bin first_bin("1 - " + std::to_string(a - 1u), 1u, a - 1u, Bin::RangeType::INTERVAL);
          bins.push_back(first_bin);
        }
      }
      if (a == b) {
        Bin bin_exact(std::to_string(a), a, Bin::RangeType::EXACT_MATCH);
        bins.push_back(bin_exact);
      } else {
        if (i == marks.size() - 2u) {
          ++b;
        }
        Bin bin_range(std::to_string(a) + " - " + std::to_string(b), a, b, Bin::RangeType::INTERVAL);
        bins.push_back(bin_range);
      }
      if (i == marks.size() - 2u) {
        Bin last_bin("> " + std::to_string(b), b, Bin::RangeType::GREATER);
        bins.push_back(last_bin);
      }
    }
  }

  template <typename A>
  void serialize(A& ar) {
    ar(CEREAL_NVP(name), CEREAL_NVP(bins));
  }
};

struct Space {
  std::vector<Dimension> dimensions;

  Dimension* DimensionByName(const std::string& name) {
    for (auto& cit : dimensions) {
      if (cit.name == name) {
        return &cit;
      }
    }
    return nullptr;
  }

  // This function is used to aggregate different features into one dimension with bin ranges extracted from the
  // feature itself. It also return empty dimension name, if the feture should be filtered out.
  std::pair<std::string, std::string> SplitFeatureIntoDimensionAndBinNames(const std::string& feature) const {
    // Do not process time dimension.
    if (feature == TIME_DIMENSION_NAME) {
      return std::make_pair("", "");
    }

    // Skip `iOSAppLaunchEvent`.
    size_t found = feature.find("iOSAppLaunchEvent");
    if (found != std::string::npos) {
      return std::make_pair("", "");
    }

    // Split device info dimension by device names.
    found = feature.find("iOSDeviceInfo");
    if (found != std::string::npos) {
      const std::string bin_name = feature.substr(14);
      return std::make_pair(DEVICE_DIMENSION_NAME, bin_name);
    }

    // Default case: dimension name = feature.
    return std::make_pair(feature, "");
  }

  template <typename A>
  void serialize(A& ar) {
    ar(CEREAL_NVP(dimensions));
  }
};

struct CubeGeneratorInput {
  struct Session {
    std::string id;
    std::map<std::string, size_t> feature_count;
    template <typename A>
    void serialize(A& ar) {
      ar(CEREAL_NVP(id), CEREAL_NVP(feature_count));
    }
  };

  Space space;
  std::vector<Session> sessions;

  template <typename A>
  void serialize(A& ar) {
    ar(CEREAL_NVP(space), CEREAL_NVP(sessions));
  }
};
