#pragma once

#include "whas/spell/spell_types.h"
#include <string>
#include <vector>

class SvgLibrary {
public:
  void LoadFromDirectories(const std::string &signsDir,
                           const std::string &sigilsDir);

  const std::vector<SvgAsset> &GetAssets() const { return m_assets; }

  const SvgAsset *FindById(const std::string &id) const;
  std::vector<const SvgAsset *> GetByKind(GlyphKind kind) const;

private:
  bool LoadSvgFile(const std::string &path, GlyphKind kind);

  std::vector<SvgAsset> m_assets;
};
