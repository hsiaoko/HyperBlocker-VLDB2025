#ifndef HYPERDEDUCE_CATALOG_DICTIONARY_H_
#define HYPERDEDUCE_CATALOG_DICTIONARY_H_

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "hyperdeduce/base/types.h"

namespace hyperdeduce {

// Interns strings to dense integer ids so string columns join as integers.
// Interning happens during setup (loading and plan constant resolution). Lookup
// is read-only and safe to call from many evaluation threads at once. Do not
// intern while evaluation threads are looking up ids.
class Dictionary {
public:
  // Returns the id for text, assigning a fresh id the first time it is seen.
  SymbolId Intern(const std::string &text);

  // Returns the text for an id previously returned by Intern.
  const std::string &Lookup(SymbolId id) const;

  std::size_t size() const { return to_string_.size(); }

private:
  std::unordered_map<std::string, SymbolId> to_id_;
  std::vector<std::string> to_string_;
};

} // namespace hyperdeduce

#endif // HYPERDEDUCE_CATALOG_DICTIONARY_H_