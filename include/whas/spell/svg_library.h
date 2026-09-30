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
  // Segments, view size and center of one SVG file
  static bool ParseShape(const std::string &path, SvgAsset &asset);

  std::vector<SvgAsset> m_assets;
};
