/*******************************************************************************
The MIT License (MIT)

Copyright (c) 2015 Maxim Zhurovich <zhurovich@gmail.com>
Copyright (c) 2015 Dmitry "Dima" Korolev <dmitry.korolev@gmail.com>

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

#include "../Current/Bricks/strings/join.h"

const std::string TIME_DIMENSION_NAME = "Session length, seconds";
const std::string DEVICE_DIMENSION_NAME = "Device";
const std::string DEVICE_UNSPECIFIED_BIN_NAME = "Unspecified";
const std::string NONE_BIN_NAME = "None";

struct Bin {
  enum class Type : int { INVALID = -1, NOT_SET = 1, TEXT = 2, INTEGRAL = 3 };
  enum class RangeType : int { INVALID = -1, EXACT_MATCH = 1, GREATER = 2, LESS = 3, INTERVAL = 4 };

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
    assert(!stats.empty());

    const size_t N = 8;
    std::vector<size_t> ticks;

    if (stats.size() <= N) {
      // Small amount of distinct values => base bin ranges on them directly.
      for (const auto& v : stats) {
        ticks.push_back(v.first);
      }
    } else {
      // Need to find the best set of ticks.
      // Solution: entropy-based, try to break between bins as evenly as possible.
      // To run the algorithm in reasonable time:
      // 1) Pre-compute partial sums per candidate ticks.
      // 2) Collapse them until the number of candidate ticks is less than M=30.
      const size_t M = 30;
      std::map<size_t, size_t> partial_sums;
      size_t total = 0u;
      for (const auto& v : stats) {
        assert(v.second > 0);
        partial_sums[v.first] = total;
        total += v.second;
      }

      // Keep removing candidate markers with the least contribution until their number is manageable.
      while (partial_sums.size() > M) {
        std::map<size_t, size_t>::iterator a = partial_sums.begin();
        std::map<size_t, size_t>::iterator b = partial_sums.begin();
        ++b;
        size_t diff = b->second - a->second;
        std::map<size_t, size_t>::iterator removal_candidate = a;
        while (b != partial_sums.end()) {
          ++a;
          ++b;
          const size_t new_diff = ((b != partial_sums.end()) ? b->second : total) - a->second;
          if (new_diff <= diff) {
            diff = new_diff;
            removal_candidate = a;
          }
        }
        partial_sums.erase(removal_candidate);
      }

      assert(partial_sums.size() >= N);
      assert(partial_sums.size() <= M);
      std::vector<std::pair<size_t, size_t>> S(partial_sums.begin(), partial_sums.end());

      std::vector<size_t> candidate_ticks(N);
      std::pair<double, std::vector<size_t>> best_ticks;
      const double inf = 1e100;
      best_ticks.first = inf;
      std::function<void(size_t i, size_t j, double penalty)> rec;
      rec = [&rec, &S, N, total, &candidate_ticks, &best_ticks](size_t i, size_t t, double penalty) {
        if (i < S.size() && t < N) {
          size_t& j = candidate_ticks[t];
          const size_t lhs = S[i].second;
          for (j = i + 1; j <= S.size() - (N - 1u - t); ++j) {
            const size_t rhs = (j < S.size()) ? S[j].second : total;
            const double delta = rhs - lhs;
            assert(delta > 0);
            rec(j, t + 1, penalty + delta * log(delta) * sqrt(1.0 / (t + 1)));
          }
        } else if (i == S.size() && t == N) {
          best_ticks = std::min(best_ticks, std::make_pair(penalty, candidate_ticks));
        }
      };
      rec(0, 0, 0.0);
      assert(best_ticks.first != inf);

      ticks = best_ticks.second;
    }

    if (ticks.size() == 1u) {
      // Only one value => fill with exact match.
      bins.emplace_back(std::to_string(ticks[0]), ticks[0], Bin::RangeType::EXACT_MATCH);
    } else {
      // Generic case: 2+ ticks.
      for (size_t i = 0; i < ticks.size(); ++i) {
        const size_t a = i ? ticks[i - 1] + 1 : 1u;
        const size_t b = ticks[i];
        assert(b >= a);
        if (i + 1 == ticks.size()) {
          bins.emplace_back("> " + std::to_string(a - 1u), a - 1u, Bin::RangeType::GREATER);
        } else {
          if (a == b) {
            bins.emplace_back(std::to_string(a), a, Bin::RangeType::EXACT_MATCH);
          } else {
            bins.emplace_back(std::to_string(a) + " .. " + std::to_string(b), a, b, Bin::RangeType::INTERVAL);
          }
        }
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
