#pragma once

#include <d2d1_3.h>
#include <dwrite_3.h>
#include <wincodec.h>
#include <wrl.h>

#include <string>
#include <memory>
#include <vector>

using namespace Microsoft::WRL;

class OutlineTextRenderer;
class SimpleDirectWrite {
 public:
  SimpleDirectWrite();
  virtual ~SimpleDirectWrite();

  struct Color {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
  };

  struct Config {
    const void* font_data = nullptr;
    size_t font_data_size = 0;
    std::wstring locale = L"en-US";

    const void* fallback_font_data = nullptr;
    size_t fallback_font_data_size = 0;
    int fallback_font_range_min = 0;
    int fallback_font_range_max = 0;
    std::wstring fallback_locale = L"en-US";
    float fallback_font_vertical_offset = 0;
  };

 public:
  void Setup(const Config& config);
  bool CalcSize(const std::wstring& text, float size, int* width, int* height);
  const std::vector<uint8_t>& Render(const std::wstring& text, float size,
                                     const Color& color = Color(),
                                     int canvas_width = 0,
                                     int canvas_height = 0,
                                     int* out_width = nullptr,
                                     int* out_height = nullptr);
  void SaveAsBitmap(const std::wstring& path);

 private:
  Config config_;

  ComPtr<IWICImagingFactory> wicimagingfactory_;
  ComPtr<ID2D1Factory7> d2d1factory_;
  ComPtr<IDWriteFactory7> dwritefactory_;
  ComPtr<OutlineTextRenderer> outlinetextrenderer_;

  ComPtr<IDWriteFontCollection1> fontcollection_;
  ComPtr<IDWriteFontFallback> fallback_;
  wchar_t familyname_[256]{};
  wchar_t familyname_fallback_[256]{};

  int buffer_width_ = 1024;
  int buffer_height_ = 1024;
  std::vector<uint8_t> buffer_;
  ComPtr<IWICBitmap> bitmap_;
  ComPtr<ID2D1RenderTarget> rendertarget_;
};

