#include "whas/spell/spell_library.h"

void SpellLibrary::Load() {
  m_store.EnsureDirectoryExists();
  m_spells = m_store.LoadAll();
  ++m_version;
}

const Spell *SpellLibrary::Find(const std::string &ref) const {
  if (ref.empty())
    return nullptr;
  for (const Spell &s : m_spells)
    if (RefOf(s) == ref)
      return &s;
  return nullptr;
}

std::string SpellLibrary::RefOf(const Spell &spell) const {
  return RefOf(spell.name);
}

std::string SpellLibrary::RefOf(const std::string &name) const {
  return m_store.SanitizeFilename(name);
}

bool SpellLibrary::Save(const Spell &spell, std::string &error) {
  if (!m_store.Save(spell, error))
    return false;
  Load();
  return true;
}

bool SpellLibrary::Remove(const std::string &ref, std::string &error) {
  if (!m_store.Remove(ref, error))
    return false;
  Load();
  return true;
}

bool SpellLibrary::Rename(const std::string &ref, const std::string &newName,
                          std::string &newRef, std::string &error) {
  const Spell *spell = Find(ref);
  if (!spell) {
    error = "Spell not found.";
    return false;
  }
  newRef = RefOf(newName);
  if (newRef.empty()) {
    error = "Name has no usable characters.";
    return false;
  }
  if (newRef != ref && m_store.Exists(newRef)) {
    error = "A spell with that name already exists.";
    return false;
  }
  Spell renamed = *spell;
  renamed.name = newName;
  if (!m_store.Save(renamed, error))
    return false;
  if (newRef != ref && !m_store.Remove(ref, error))
    return false;
  Load();
  return true;
}

bool SpellLibrary::Duplicate(const std::string &ref, std::string &newRef,
                             std::string &error) {
  const Spell *spell = Find(ref);
  if (!spell) {
    error = "Spell not found.";
    return false;
  }
  Spell copy = *spell;
  for (int n = 1;; ++n) {
    copy.name = spell->name + (n == 1 ? " copy" : " copy " + std::to_string(n));
    if (copy.name.size() > SPELL_NAME_MAX_LEN)
      copy.name = copy.name.substr(copy.name.size() - SPELL_NAME_MAX_LEN);
    if (!m_store.Exists(RefOf(copy)))
      break;
  }
  newRef = RefOf(copy);
  return Save(copy, error);
}
