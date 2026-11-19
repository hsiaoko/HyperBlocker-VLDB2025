#include "hyperdeduce/catalog/dictionary.h"

namespace hyperdeduce {

SymbolId Dictionary::Intern(const std::string &text) {
  auto it = to_id_.find(text);
  if (it != to_id_.end()) {
    return it->second;
  }
  SymbolId id = to_string_.size();
  to_string_.push_back(text);
  to_id_.emplace(text, id);
  return id;
}

const std::string &Dictionary::Lookup(SymbolId id) const {
  return to_string_.at(id);
}

} // namespace hyperdeduce