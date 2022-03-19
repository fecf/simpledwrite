#include "simpledwrite.h"

#include <combaseapi.h>
#include <comdef.h>
#include <d2d1_3.h>
#include <dwrite_3.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <wincodec.h>
#include <winrt/base.h>
#include <wrl.h>

#include <functional>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "shlwapi.lib")

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)
#define CHECK_(hr, file, line)                                         \
  if (FAILED(hr)) {                                                    \
     std::string errmsg = std::system_category().message(hr);           \
     std::stringstream ss;                                              \
     ss << "failed at " << file << "@" << line << " reason=" << errmsg; \
     throw std::runtime_error(ss.str());                                \
  }
#define CHECK(hr) CHECK_(hr, __FILE__, STR(__LINE__))

using namespace Microsoft::WRL;

namespace simpledwrite {

constexpr int kMaxLayoutSize = 16384;

inline std::string utf16_to_utf8(const std::wstring& wstr) {
  winrt::hstring hstr(wstr);
  return winrt::to_string(hstr);
}

inline std::wstring utf8_to_utf16(const std::string& str) {
  winrt::hstring hstr = winrt::to_hstring(str);
  return (std::wstring)hstr;
}

// ref.
// https://stackoverflow.com/questions/66872711/directwrite-direct2d-custom-text-rendering-is-hairy
class TextRenderer
    : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IDWriteTextRenderer> {
 public:
  TextRenderer(ComPtr<ID2D1Factory7> d2d1factory,
      std::function<float(IDWriteFontFace*)> rendercallback)
      : d2d1factory_(d2d1factory),
        rendercallback_(rendercallback),
        outline_width_(0) {
    D2D1_STROKE_STYLE_PROPERTIES strokeprops = D2D1::StrokeStyleProperties();
    strokeprops.lineJoin = D2D1_LINE_JOIN_ROUND;
    d2d1factory_->CreateStrokeStyle(&strokeprops, NULL, 0, &strokestyle_);
  }
  virtual ~TextRenderer() = default;

 protected:
  virtual HRESULT __stdcall IsPixelSnappingDisabled(
      void* clientDrawingContext, BOOL* isDisabled) override {
    *isDisabled = FALSE;
    return S_OK;
  }
  virtual HRESULT __stdcall GetCurrentTransform(
      void* clientDrawingContext, DWRITE_MATRIX* transform) override {
    rendertarget_->GetTransform((D2D1_MATRIX_3X2_F*)transform);
    return S_OK;
  }
  virtual HRESULT __stdcall GetPixelsPerDip(
      void* clientDrawingContext, FLOAT* pixelsPerDip) override {
    FLOAT dpi_x, dpi_y;
    rendertarget_->GetDpi(&dpi_x, &dpi_y);
    *pixelsPerDip = dpi_y / 96.0f;
    return S_OK;
  }
  virtual HRESULT __stdcall DrawGlyphRun(void* clientDrawingContext,
      FLOAT baselineOriginX, FLOAT baselineOriginY,
      DWRITE_MEASURING_MODE measuringMode, DWRITE_GLYPH_RUN const* glyphRun,
      DWRITE_GLYPH_RUN_DESCRIPTION const* glyphRunDescription,
      IUnknown* clientDrawingEffect) override {
    ComPtr<ID2D1PathGeometry> pathgeometry;
    CHECK(d2d1factory_->CreatePathGeometry(&pathgeometry));
    ComPtr<ID2D1GeometrySink> geometrysink;
    CHECK(pathgeometry->Open(&geometrysink));
    CHECK(glyphRun->fontFace->GetGlyphRunOutline(glyphRun->fontEmSize,
        glyphRun->glyphIndices, glyphRun->glyphAdvances, glyphRun->glyphOffsets,
        glyphRun->glyphCount, glyphRun->isSideways, glyphRun->bidiLevel % 2,
        geometrysink.Get()));
    CHECK(geometrysink->Close());

    float vertical_offset = rendercallback_(glyphRun->fontFace);

    {
      D2D1::ColorF d2d1color(
          fill_color_.r, fill_color_.g, fill_color_.b, fill_color_.a);
      CHECK(rendertarget_->CreateSolidColorBrush(d2d1color, &fill_brush_));
    }
    {
      D2D1::ColorF d2d1color(outline_color_.r, outline_color_.g,
          outline_color_.b, outline_color_.a);
      CHECK(rendertarget_->CreateSolidColorBrush(d2d1color, &outline_brush_));
    }

    D2D1::Matrix3x2F transform = D2D1::Matrix3x2F(1.0f, 0.0f, 0.0f, 1.0f,
        baselineOriginX, baselineOriginY + vertical_offset);
    ComPtr<ID2D1TransformedGeometry> transformedgeometry;
    CHECK(d2d1factory_->CreateTransformedGeometry(
        pathgeometry.Get(), transform, &transformedgeometry));
    if (outline_width_) {
      rendertarget_->DrawGeometry(transformedgeometry.Get(),
          outline_brush_.Get(), static_cast<FLOAT>(outline_width_),
          strokestyle_.Get());
    }
    rendertarget_->FillGeometry(transformedgeometry.Get(), fill_brush_.Get());

    return S_OK;
  }
  virtual HRESULT __stdcall DrawUnderline(void* clientDrawingContext,
      FLOAT baselineOriginX, FLOAT baselineOriginY,
      DWRITE_UNDERLINE const* underline,
      IUnknown* clientDrawingEffect) override {
    return E_NOTIMPL;
  }
  virtual HRESULT __stdcall DrawStrikethrough(void* clientDrawingContext,
      FLOAT baselineOriginX, FLOAT baselineOriginY,
      DWRITE_STRIKETHROUGH const* strikethrough,
      IUnknown* clientDrawingEffect) override {
    return E_NOTIMPL;
  }
  virtual HRESULT __stdcall DrawInlineObject(void* clientDrawingContext,
      FLOAT originX, FLOAT originY, IDWriteInlineObject* inlineObject,
      BOOL isSideways, BOOL isRightToLeft,
      IUnknown* clientDrawingEffect) override {
    return E_NOTIMPL;
  }

 public:
  void SetRenderTarget(ComPtr<ID2D1RenderTarget> rendertarget) {
    rendertarget_ = rendertarget;
  }
  void SetOutline(float width, Color color) {
    outline_width_ = width;
    outline_color_ = color;
  };
  void SetFill(Color color) { fill_color_ = color; };

 private:
  ComPtr<ID2D1Factory7> d2d1factory_;
  ComPtr<ID2D1RenderTarget> rendertarget_;
  std::function<float(IDWriteFontFace*)> rendercallback_;

  Color fill_color_;
  float outline_width_ = 0.0f;
  Color outline_color_;
  ComPtr<ID2D1SolidColorBrush> fill_brush_;
  ComPtr<ID2D1SolidColorBrush> outline_brush_;
  ComPtr<ID2D1StrokeStyle> strokestyle_;
};

class SimpleDWriteImpl {
 public:
  SimpleDWriteImpl() {
    CHECK(::D2D1CreateFactory<ID2D1Factory7>(
        D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2d1factory));
    CHECK(::DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory7), &dwritefactory));
    CHECK(::CoCreateInstance(CLSID_WICImagingFactory2, nullptr,
        CLSCTX_INPROC_SERVER, __uuidof(IWICImagingFactory2),
        &wicimagingfactory));
    textrenderer =
        Make<TextRenderer>(d2d1factory, [=](IDWriteFontFace* ff) -> float {
          ComPtr<IDWriteFontFace5> ff5;
          CHECK(ff->QueryInterface<IDWriteFontFace5>(&ff5));
          ComPtr<IDWriteLocalizedStrings> names;
          CHECK(ff5->GetFamilyNames(&names));
          static wchar_t buf[1024];
          CHECK(names->GetString(0, buf, 1024));
          std::wstring wstr(buf);
          if (fontfamilymap.count(wstr)) {
            float offset = fontfamilymap.at(wstr)->vertical_offset;
            return offset;
          }
          return 0.0f;
        });
  }
  virtual ~SimpleDWriteImpl() = default;

  bool calcSize(ComPtr<IDWriteTextLayout> textlayout, Layout& layout) {
    DWRITE_TEXT_METRICS text_metrics{};
    CHECK(textlayout->GetMetrics(&text_metrics));
    const size_t required_size = (int)(text_metrics.width + 0.5f) * 4 *
                                 (int)(text_metrics.height + 0.5f);
    layout.out_buffer_size = (int)required_size;

    DWRITE_OVERHANG_METRICS overhang_metrics{};
    CHECK(textlayout->GetOverhangMetrics(&overhang_metrics));
    layout.out_width = (int)(text_metrics.width + 0.5f);
    layout.out_height = (int)(text_metrics.height + 0.5f);
    layout.out_padding_top = (int)(-std::min(0.0f, overhang_metrics.top));
    layout.out_padding_left = (int)(-std::min(0.0f, overhang_metrics.left));
    layout.out_padding_right = layout.out_width - (int)(layout.max_width + overhang_metrics.right);
    layout.out_padding_bottom = layout.out_height - (int)(layout.max_height + overhang_metrics.bottom);

    DWRITE_LINE_METRICS line_metrics{};
    UINT line_count{};
    CHECK(textlayout->GetLineMetrics(&line_metrics, UINT32_MAX, &line_count));
    layout.out_baseline = (int)(line_metrics.baseline - overhang_metrics.top + 0.5f);

    return true;
  }

  ComPtr<IDWriteTextFormat> createTextFormat(const Layout& layout, const FontSet& fs, float dpi) {
    ComPtr<IDWriteTextFormat> textformat;
    const float dip = layout.font_size / (dpi / 96.0f);
    CHECK(dwritefactory->CreateTextFormat(firstfamilyname.c_str(),
        fontcollection.Get(), (DWRITE_FONT_WEIGHT)layout.font_weight,
        (DWRITE_FONT_STYLE)layout.font_style,
        (DWRITE_FONT_STRETCH)layout.font_stretch, dip,
        utf8_to_utf16(fs.locale).c_str(), &textformat));
    if (fallback) {
      ComPtr<IDWriteTextFormat3> textformat3;
      textformat.As(&textformat3);
      CHECK(textformat3->SetFontFallback(fallback.Get()));
    }
    return textformat;
  }

  ComPtr<IDWriteTextLayout> createTextLayout(ComPtr<IDWriteTextFormat> textformat,
      const Layout& layout, const std::string& text) {
    std::wstring wtext = utf8_to_utf16(text);
    ComPtr<IDWriteTextLayout> textlayout;
    CHECK(dwritefactory->CreateTextLayout(wtext.c_str(), (UINT32)wtext.length(),
        textformat.Get(), (FLOAT)layout.max_width, (FLOAT)layout.max_height,
        &textlayout));
    textlayout->SetWordWrapping((DWRITE_WORD_WRAPPING)layout.word_wrap_mode);
    return textlayout;
  }

  ComPtr<ID2D1Factory7> d2d1factory;
  ComPtr<IDWriteFactory7> dwritefactory;
  ComPtr<IWICImagingFactory2> wicimagingfactory;
  ComPtr<IDWriteFontSet> fontset;
  ComPtr<IDWriteFontCollection1> fontcollection;
  ComPtr<IDWriteFontFallback> fallback;
  ComPtr<TextRenderer> textrenderer;
  ComPtr<IWICBitmap> wicbitmap;
  std::unordered_map<std::wstring, Font*> fontfamilymap;
  std::wstring firstfamilyname;
};

Font::Font(const std::string& name, float vertical_offset)
    : name(name), data(), data_size(), vertical_offset(vertical_offset) {}

Font::Font(const void* data, size_t data_size, float vertical_offset)
    : name(),
      data(data),
      data_size(data_size),
      vertical_offset(vertical_offset) {}

FallbackFont::FallbackFont(const std::string& family,
    const std::vector<std::pair<uint32_t, uint32_t>>& ranges)
    : family(family), ranges(ranges) {}

FontSet FontSet::Default() {
  static FontSet fontset;
  static std::once_flag once;
  std::call_once(once, [&] {
    wchar_t locale[LOCALE_NAME_MAX_LENGTH];
    int ret = ::GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH);
    fontset.locale = utf16_to_utf8(locale);
  });
  return fontset;
}

FontSet::FontSet() {}

SimpleDWrite::SimpleDWrite()
    : dpi_((float)::GetDpiForSystem()), impl(new SimpleDWriteImpl()) {}

SimpleDWrite::~SimpleDWrite() {}

bool SimpleDWrite::Init(const FontSet& fs, float dpi) {
  fs_ = fs;
  dpi_ = dpi;

  try {
    if (fs.locale.empty()) {
      wchar_t locale[LOCALE_NAME_MAX_LENGTH];
      int ret = ::GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH);
      if (ret) {
        fs_.locale = utf16_to_utf8(locale);
      }
    }

    if (fs_.fonts.empty()) {
      // ref. https://stackoverflow.com/questions/41505151/how-to-draw-text-with-the-default-ui-font-in-directwrite
      NONCLIENTMETRICSW ncm{sizeof(ncm)};
      BOOL ret = ::SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, ncm.cbSize, &ncm, 0);
      if (ret == FALSE) {
        last_error_ = "failed ::SystemParametersInfoW().";
        return false;
      }

      ComPtr<IDWriteGdiInterop> gdiinterop;
      CHECK(impl->dwritefactory->GetGdiInterop(&gdiinterop));
      ComPtr<IDWriteFont> sysfont;
      CHECK(gdiinterop->CreateFontFromLOGFONT(&ncm.lfMessageFont, &sysfont));

      ComPtr<IDWriteFontFamily> family;
      CHECK(sysfont->GetFontFamily(&family));
      ComPtr<IDWriteLocalizedStrings> familyname;
      CHECK(family->GetFamilyNames(&familyname));
      thread_local wchar_t buf[1024]{};
      CHECK(familyname->GetString(0, buf, 1024));
      fs_.fonts.push_back(Font(utf16_to_utf8(buf)));
    }

    ComPtr<IDWriteFactory7> factory = impl->dwritefactory;
    ComPtr<IDWriteInMemoryFontFileLoader> memoryfontfileloader;
    CHECK(factory->CreateInMemoryFontFileLoader(&memoryfontfileloader));
    CHECK(factory->RegisterFontFileLoader(memoryfontfileloader.Get()));
    ComPtr<IDWriteFontSetBuilder2> fontsetbuilder;
    CHECK(factory->CreateFontSetBuilder(&fontsetbuilder));
    ComPtr<IDWriteFontCollection> systemfontcollection;
    CHECK(factory->GetSystemFontCollection(&systemfontcollection));

    std::vector<Font*> fontconfiglist;
    for (const Font& font : fs_.fonts) {
      if (font.data != nullptr && font.data_size) {
        ComPtr<IDWriteFontFile> fontfile;
        CHECK(memoryfontfileloader->CreateInMemoryFontFileReference(
            factory.Get(), font.data, (UINT32)font.data_size, NULL,
            &fontfile));
        BOOL supported = FALSE;
        DWRITE_FONT_FILE_TYPE filetype{};
        DWRITE_FONT_FACE_TYPE facetype{};
        UINT32 faces = 0;
        CHECK(fontfile->Analyze(&supported, &filetype, &facetype, &faces));
        if (supported && faces) {
          CHECK(fontsetbuilder->AddFontFile(fontfile.Get()));
        }
        fontconfiglist.insert(
            fontconfiglist.end(), (size_t)faces, (Font*)&font);
      } else {
        if (font.name.empty()) {
          last_error_ = "Font::name is empty.";
          return false;
        }

        UINT32 index = 0;
        BOOL exists = FALSE;
        CHECK(systemfontcollection->FindFamilyName(
            utf8_to_utf16(font.name).c_str(), &index, &exists));
        if (!exists) {
          continue;
        }

        ComPtr<IDWriteFontFamily> fontfamily;
        CHECK(systemfontcollection->GetFontFamily(index, &fontfamily));
        ComPtr<IDWriteFontSetBuilder2> fontsetbuilder2;
        CHECK(factory->CreateFontSetBuilder(&fontsetbuilder2));
        UINT32 count = fontfamily->GetFontCount();
        for (UINT32 i = 0; i < count; ++i) {
          ComPtr<IDWriteFont> font;
          CHECK(fontfamily->GetFont(i, &font));
          ComPtr<IDWriteFont3> font3;
          CHECK(font.As(&font3));
          ComPtr<IDWriteFontFaceReference> fontfacereference;
          CHECK(font3->GetFontFaceReference(&fontfacereference));
          CHECK(
              fontsetbuilder2->AddFontFaceReference(fontfacereference.Get()));
        }
        ComPtr<IDWriteFontSet> systemfontset;
        CHECK(fontsetbuilder2->CreateFontSet(&systemfontset));
        CHECK(fontsetbuilder->AddFontSet(systemfontset.Get()));
        fontconfiglist.insert(fontconfiglist.end(), (size_t)1, (Font*)&font);
      }
    }

    CHECK(fontsetbuilder->CreateFontSet(&impl->fontset));
    CHECK(factory->CreateFontCollectionFromFontSet(
        impl->fontset.Get(), &impl->fontcollection));
    impl->firstfamilyname.clear();
    for (int i = 0; i < (int)fontconfiglist.size(); ++i) {
      ComPtr<IDWriteFontFamily1> fontfamily;
      CHECK(impl->fontcollection->GetFontFamily(i, &fontfamily));
      ComPtr<IDWriteLocalizedStrings> names;
      CHECK(fontfamily->GetFamilyNames(&names));
      UINT32 count = names->GetCount();
      thread_local wchar_t familyname[1024];
      if (i == 0) {
        CHECK(names->GetString(0, familyname, 1024));
        impl->firstfamilyname = familyname;
      }
      for (UINT32 j = 0; j < count; ++j) {
        CHECK(names->GetString(j, familyname, 1024));
      }
      impl->fontfamilymap[familyname] = fontconfiglist[i];
    }
    if (impl->firstfamilyname.empty()) {
      last_error_ = "font not found.";
      return false;
    }

    ComPtr<IDWriteFontFallbackBuilder> fallbackbuilder;
    CHECK(factory->CreateFontFallbackBuilder(&fallbackbuilder));
    for (const FallbackFont& fallback : fs_.fallbacks) {
      std::vector<DWRITE_UNICODE_RANGE> ranges;
      for (const std::pair<uint32_t, uint32_t>& pair : fallback.ranges) {
        DWRITE_UNICODE_RANGE range{};
        range.first = pair.first;
        range.last = pair.second;
        ranges.push_back(range);
      }
      const std::wstring wfamily = utf8_to_utf16(fallback.family);
      const wchar_t* wfamilyptr = wfamily.c_str();
      CHECK(fallbackbuilder->AddMapping(
          (const DWRITE_UNICODE_RANGE*)ranges.data(), (UINT32)ranges.size(),
          (const WCHAR**)&wfamilyptr, 1, impl->fontcollection.Get()));
    }
    CHECK(fallbackbuilder->CreateFontFallback(&impl->fallback));
    return true;
  } catch (std::exception& ex) {
    last_error_ = ex.what();
    return false;
  }
  return false;
}

bool SimpleDWrite::CalcSize(const std::string& text, Layout& layout) const {
  try {
    ComPtr<IDWriteTextFormat> textformat = impl->createTextFormat(layout, fs_, dpi_);
    ComPtr<IDWriteTextLayout> textlayout = impl->createTextLayout(textformat, layout, text);
    if (!impl->calcSize(textlayout, layout)) {
      return false;
    }
    return true;
  } catch (std::exception& ex) {
    last_error_ = ex.what();
    return false;
  }
}

bool SimpleDWrite::Render(const std::string& text, uint8_t* buffer,
    int buffer_size, Layout& layout, const RenderParams& renderparams) const {
  try {
    ComPtr<IDWriteTextFormat> textformat = impl->createTextFormat(layout, fs_, dpi_);
    ComPtr<IDWriteTextLayout> textlayout = impl->createTextLayout(textformat, layout, text);
    if (!impl->calcSize(textlayout, layout)) {
      return false;
    }

    if (layout.out_buffer_size > buffer_size) {
      last_error_ = "not enough buffer size.";
      return false;
    }

    ComPtr<IWICBitmap> bitmap;
    CHECK(impl->wicimagingfactory->CreateBitmapFromMemory(
        (UINT)(layout.out_width + 0.5f), (UINT)(layout.out_height + 0.5f),
        GUID_WICPixelFormat32bppPBGRA, (UINT)(layout.out_width + 0.5f) * 4,
        (UINT)(layout.out_width + 0.5f) * 4 * (UINT)(layout.out_height + 0.5f),
        buffer, &bitmap));

    ComPtr<ID2D1RenderTarget> rendertarget;
    D2D1_RENDER_TARGET_PROPERTIES props =
        D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(
                DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            (FLOAT)dpi_, (FLOAT)dpi_);
    CHECK(impl->d2d1factory->CreateWicBitmapRenderTarget(
        bitmap.Get(), &props, &rendertarget));
    impl->textrenderer->SetRenderTarget(rendertarget);

    rendertarget->BeginDraw();
    rendertarget->Clear(D2D1::ColorF(renderparams.background_color.r,
        renderparams.background_color.g, renderparams.background_color.b,
        renderparams.background_color.a));
    rendertarget->SetTextAntialiasMode(static_cast<D2D1_TEXT_ANTIALIAS_MODE>(
        renderparams.text_antialias_mode));
    rendertarget->SetAntialiasMode(
        static_cast<D2D1_ANTIALIAS_MODE>(renderparams.antialias_mode));
    rendertarget->SetTransform(D2D1::Matrix3x2F::Identity());
    ComPtr<ID2D1SolidColorBrush> brush;

    rendertarget->CreateSolidColorBrush(
        D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &brush);
    impl->textrenderer->SetFill(renderparams.foreground_color);
    impl->textrenderer->SetOutline(
        renderparams.outline_width, renderparams.outline_color);
    textlayout->Draw(
        NULL, (IDWriteTextRenderer*)impl->textrenderer.Get(), 0.0f, 0.0f);
    rendertarget->EndDraw();

    WICRect rect{};
    rect.X = 0;
    rect.Y = 0;
    rect.Width = (INT)layout.out_width;
    rect.Height = (INT)layout.out_height;
    const int stride = ((INT)(layout.out_width + 0.5f)) * 4;
    if (rect.Width * 4 * rect.Height > buffer_size) {
      last_error_ = "not enough buffer size.";
      return false;
    }
    CHECK(bitmap->CopyPixels(&rect, rect.Width * 4 /* dst stride */,
        static_cast<UINT>(buffer_size), buffer));
    return true;
  } catch (std::exception& ex) {
    last_error_ = ex.what();
    return false;
  }
}

std::string SimpleDWrite::GetLastError() const { return std::string(); }

}  // namespace simpledwrite
