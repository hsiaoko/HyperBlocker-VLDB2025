#include "hyperdeduce/builtin/similarity.h"

#include <algorithm>
#include <cctype>
#include <vector>

namespace hyperdeduce {

double Jaro(const std::string &a, const std::string &b) {
  const std::size_t la = a.size();
  const std::size_t lb = b.size();
  if (la == 0 && lb == 0) {
    return 1.0;
  }
  if (la == 0 || lb == 0) {
    return 0.0;
  }

  const std::size_t max_len = std::max(la, lb);
  const std::size_t window = max_len / 2 > 0 ? max_len / 2 - 1 : 0;

  std::vector<bool> a_match(la, false);
  std::vector<bool> b_match(lb, false);
  std::size_t matches = 0;
  for (std::size_t i = 0; i < la; ++i) {
    const std::size_t start = i > window ? i - window : 0;
    const std::size_t end = std::min(i + window + 1, lb);
    for (std::size_t j = start; j < end; ++j) {
      if (b_match[j] || a[i] != b[j]) {
        continue;
      }
      a_match[i] = true;
      b_match[j] = true;
      ++matches;
      break;
    }
  }
  if (matches == 0) {
    return 0.0;
  }

  std::size_t transpositions = 0;
  std::size_t k = 0;
  for (std::size_t i = 0; i < la; ++i) {
    if (!a_match[i]) {
      continue;
    }
    while (!b_match[k]) {
      ++k;
    }
    if (a[i] != b[k]) {
      ++transpositions;
    }
    ++k;
  }
  transpositions /= 2;

  const double m = static_cast<double>(matches);
  return (m / la + m / lb + (m - transpositions) / m) / 3.0;
}

double JaroWinkler(const std::string &a, const std::string &b,
                   double prefix_weight) {
  const double jaro = Jaro(a, b);
  const std::size_t limit = std::min<std::size_t>(4, std::min(a.size(), b.size()));
  std::size_t prefix = 0;
  while (prefix < limit && a[prefix] == b[prefix]) {
    ++prefix;
  }
  const double result = jaro + prefix * prefix_weight * (1.0 - jaro);
  return result > 1.0 ? 1.0 : result;
}

double Jaccard(const std::string &a, const std::string &b) {
  bool set_a[256] = {false};
  bool set_b[256] = {false};
  for (unsigned char c : a) {
    set_a[std::tolower(c)] = true;
  }
  for (unsigned char c : b) {
    set_b[std::tolower(c)] = true;
  }
  std::size_t intersection = 0;
  std::size_t union_size = 0;
  for (int i = 0; i < 256; ++i) {
    if (set_a[i] && set_b[i]) {
      ++intersection;
    }
    if (set_a[i] || set_b[i]) {
      ++union_size;
    }
  }
  if (union_size == 0) {
    return 1.0;
  }
  return static_cast<double>(intersection) / static_cast<double>(union_size);
}

int LevenshteinDistance(const std::string &a, const std::string &b) {
  const std::size_t la = a.size();
  const std::size_t lb = b.size();
  if (la == 0) {
    return static_cast<int>(lb);
  }
  if (lb == 0) {
    return static_cast<int>(la);
  }
  std::vector<int> previous(lb + 1);
  std::vector<int> current(lb + 1);
  for (std::size_t j = 0; j <= lb; ++j) {
    previous[j] = static_cast<int>(j);
  }
  for (std::size_t i = 1; i <= la; ++i) {
    current[0] = static_cast<int>(i);
    for (std::size_t j = 1; j <= lb; ++j) {
      const int cost = a[i - 1] == b[j - 1] ? 0 : 1;
      current[j] = std::min({previous[j] + 1, current[j - 1] + 1,
                             previous[j - 1] + cost});
    }
    previous.swap(current);
  }
  return previous[lb];
}

double LevenshteinRatio(const std::string &a, const std::string &b) {
  const std::size_t max_len = std::max(a.size(), b.size());
  if (max_len == 0) {
    return 1.0;
  }
  const int distance = LevenshteinDistance(a, b);
  return 1.0 - static_cast<double>(distance) / static_cast<double>(max_len);
}

} // namespace hyperdeduce