#pragma once

#include <d2d1_3.h>
#include <dwrite_3.h>
#include <wincodec.h>
#include <wrl.h>

#include <functional>
#include <string>
#include <memory>
#include <unordered_map>
#include <vector>

using namespace Microsoft::WRL;

// Custom text renderer class
// ref. https://stackoverflow.com/questions/66872711/directwrite-direct2d-custom-text-rendering-is-hairy
class TextRenderer
    : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IDWriteTextRenderer> {
 private:
  ComPtr<ID2D1Factory7> d2d1factory_;
  ComPtr<ID2D1RenderTarget> rendertarget_;
  std::function<float(IDWriteFontFace*)> rendercallback_;

  ComPtr<ID2D1SolidColorBrush> fill_brush_;
  ComPtr<ID2D1SolidColorBrush> outline_brush_;

 private:
  ~TextRenderer() = default;

 public:
  TextRenderer(ComPtr<ID2D1Factory7> d2d1factory,
               ComPtr<ID2D1RenderTarget> rendertarget,
               std::function<float(IDWriteFontFace*)> rendercallback);

  virtual HRESULT __stdcall IsPixelSnappingDisabled(void* clientDrawingContext,
                                                    BOOL* isDisabled) override;
  virtual HRESULT __stdcall GetCurrentTransform(
      void* clientDrawingContext, DWRITE_MATRIX* transform) override;
  virtual HRESULT __stdcall GetPixelsPerDip(void* clientDrawingContext,
                                            FLOAT* pixelsPerDip) override;
  virtual HRESULT __stdcall DrawGlyphRun(
      void* clientDrawingContext, FLOAT baselineOriginX, FLOAT baselineOriginY,
      DWRITE_MEASURING_MODE measuringMode, DWRITE_GLYPH_RUN const* glyphRun,
      DWRITE_GLYPH_RUN_DESCRIPTION const* glyphRunDescription,
      IUnknown* clientDrawingEffect) override;
  virtual HRESULT __stdcall DrawUnderline(
      void* clientDrawingContext, FLOAT baselineOriginX, FLOAT baselineOriginY,
      DWRITE_UNDERLINE const* underline,
      IUnknown* clientDrawingEffect) override;
  virtual HRESULT __stdcall DrawStrikethrough(
      void* clientDrawingContext, FLOAT baselineOriginX, FLOAT baselineOriginY,
      DWRITE_STRIKETHROUGH const* strikethrough,
      IUnknown* clientDrawingEffect) override;
  virtual HRESULT __stdcall DrawInlineObject(
      void* clientDrawingContext, FLOAT originX, FLOAT originY,
      IDWriteInlineObject* inlineObject, BOOL isSideways, BOOL isRightToLeft,
      IUnknown* clientDrawingEffect) override;

  void Setup(const float (&color)[4], bool outline,
             const float (&outline_color)[4]);
};

class SimpleDirectWrite {
 public:
  SimpleDirectWrite();
  virtual ~SimpleDirectWrite();

  struct Config {
    struct Font {
      Font(const std::wstring& name, float vertical_offset = 0.0f)
          : name(name), vertical_offset(vertical_offset) {}
      Font(const void* data, size_t size, float vertical_offset = 0.0f)
          : data(data), size(size), vertical_offset(vertical_offset) {}

      const std::wstring name;
      const void* data = nullptr;
      size_t size = 0;
      float vertical_offset = 0.0f;
    };
    std::vector<Font> fonts;

    struct Fallback {
      Fallback(const std::wstring& family,
               const std::vector<std::pair<uint32_t, uint32_t>>& ranges)
          : family(family), ranges(ranges) {}

      std::vector<std::pair<uint32_t, uint32_t>> ranges;
      std::wstring family;
    };
    std::vector<Fallback> fallbacks;

    std::wstring locale = L"";
  };

 public:
  bool Setup(const Config& config);
  bool CalcSize(const std::wstring& text, float size, int* width, int* height);

  const std::vector<uint8_t>& Render(
      const std::wstring& text, float size,
      const float (&color)[4] = {0, 0, 0, 1}, bool outline = false,
      const float (&outline_color)[4] = {1, 1, 1, 1}, int* out_width = nullptr,
      int* out_height = nullptr);
  void SaveAsBitmap(const std::wstring& path, int width, int height);

 private:
  Config config_;
  std::wstring locale_ = L"en-US";

  ComPtr<IWICImagingFactory> wicimagingfactory_;
  ComPtr<ID2D1Factory7> d2d1factory_;
  ComPtr<IDWriteFactory7> dwritefactory_;
  ComPtr<TextRenderer> outlinetextrenderer_;

  ComPtr<IDWriteFontSet> fontset_;
  ComPtr<IDWriteFontCollection1> fontcollection_;
  std::unordered_map<std::wstring, Config::Font*> fontfamilymap_;
  std::wstring firstfamilyname_;

  ComPtr<IDWriteFontFallback> fallback_;

  std::vector<uint8_t> buffer_;
  ComPtr<IWICBitmap> bitmap_;
  int maxwidth_ = 1024;
  int maxheight_ = 256;
  ComPtr<ID2D1RenderTarget> rendertarget_;
};

