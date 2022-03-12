#pragma once

// simpledwrite
// https://github.com/fecf/simpledwrite

#include <memory>
#include <string>
#include <vector>

namespace simpledwrite {

// Same as DWRITE_* enums
enum class FontWeight {
  THIN = 100,
  EXTRA_LIGHT = 200,
  ULTRA_LIGHT = 200,
  LIGHT = 300,
  SEMI_LIGHT = 350,
  NORMAL = 400,
  REGULAR = 400,
  MEDIUM = 500,
  DEMI_BOLD = 600,
  SEMI_BOLD = 600,
  BOLD = 700,
  EXTRA_BOLD = 800,
  ULTRA_BOLD = 800,
  BLACK = 900,
  HEAVY = 900,
  EXTRA_BLACK = 950,
  ULTRA_BLACK = 950
};
enum class FontStretch {
  UNDEFINED = 0,
  ULTRA_CONDENSED = 1,
  EXTRA_CONDENSED = 2,
  CONDENSED = 3,
  SEMI_CONDENSED = 4,
  NORMAL = 5,
  MEDIUM = 5,
  SEMI_EXPANDED = 6,
  EXPANDED = 7,
  EXTRA_EXPANDED = 8,
  ULTRA_EXPANDED = 9
};
enum class FontStyle { NORMAL, OBLIQUE, ITALIC };
enum class WordWrapMode {
  WRAP = 0,
  NO_WRAP = 1,
  EMERGENCY_BREAK = 2,
  WHOLE_WORD = 3,
  CHARACTER = 4,
};
enum class AntialiasMode {
  PER_PRIMITIVE = 0,
  ALIASED = 1,
};
enum class TextAntialiasMode {
  DEFAULT = 0,
  CLEARTYPE = 1,
  GRAYSCALE = 2,
  ALIASED = 3,
};

struct Font {
  Font() = default;
  Font(const std::string& name, float vertical_offset = 0.0f);
  Font(const void* data, size_t data_size, float vertical_offset = 0.0f);

  std::string name;
  const void* data;
  size_t data_size;
  float vertical_offset;
};

struct FallbackFont {
  FallbackFont() = default;
  FallbackFont(const std::string& family,
      const std::vector<std::pair<uint32_t, uint32_t>>& ranges);

  std::vector<std::pair<uint32_t, uint32_t>> ranges;
  std::string family;
};

struct FontSet {
  static FontSet Default();
  FontSet();

  std::vector<Font> fonts;
  std::vector<FallbackFont> fallbacks;
  std::string locale;
};

struct Layout {
  Layout() {}
  Layout(int font_size) : font_size(font_size) {}

  // in
  int font_size = 16;  // points
  float max_width = 0.0f;
  float max_height = 0.0f;
  WordWrapMode word_wrap_mode = WordWrapMode::CHARACTER;
  FontWeight font_weight = FontWeight::NORMAL;
  FontStretch font_stretch = FontStretch::NORMAL;
  FontStyle font_style = FontStyle::NORMAL;

  // out
  int out_left = 0;
  int out_top = 0;
  int out_width = 0;
  int out_height = 0;
  int out_buffer_size = 0;
  int out_baseline = 0;
};

struct Color {
  float r = 0.0f;
  float g = 0.0f;
  float b = 0.0f;
  float a = 1.0f;
};

struct RenderParams {
  Color foreground_color = {0, 0, 0, 1};
  Color background_color = {1, 1, 1, 1};
  float outline_width = 0.0f;
  Color outline_color = {1, 1, 1, 1};
  AntialiasMode antialias_mode = AntialiasMode::PER_PRIMITIVE;
  TextAntialiasMode text_antialias_mode = TextAntialiasMode::DEFAULT;
};

class SimpleDWriteImpl;
class SimpleDWrite {
 public:
  SimpleDWrite();
  virtual ~SimpleDWrite();

  bool Init(const FontSet& fs, float dpi = 96.0f);
  bool CalcSize(const std::string& text, Layout& layout) const;
  bool Render(const std::string& text, uint8_t* buffer, int buffer_size,
      Layout& layout, const RenderParams& renderparams = RenderParams()) const;
  std::string GetLastError() const;

 private:
  mutable std::string last_error_;
  FontSet fs_;
  float dpi_;

  std::unique_ptr<SimpleDWriteImpl> impl;
};

}  // namespace simpledwrite
