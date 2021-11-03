#include "simpledwrite.h"

#include <comdef.h>
#include <shellapi.h>
#include <shlwapi.h>

#include <iostream>
#include <sstream>
#include <stdexcept>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "shlwapi.lib")

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)
#define CHECK_(hr, file, line)                                         \
  if (FAILED(hr)) {                                                    \
    _com_error err(hr);                                                \
    LPCTSTR errmsg = err.ErrorMessage();                               \
    std::stringstream ss;                                              \
    ss << "failed at " << file << "@" << line << " reason=" << errmsg; \
    throw std::runtime_error(ss.str());                                \
  }

#define CHECK(hr) CHECK_(hr, __FILE__, STR(__LINE__))

SimpleDirectWrite::~SimpleDirectWrite() = default;

bool SimpleDirectWrite::Setup(const Config& config) {
  buffer_.resize(4 * maxwidth_ * maxheight_);
  CHECK(wicimagingfactory_->CreateBitmapFromMemory(
      maxwidth_, maxheight_, GUID_WICPixelFormat32bppPBGRA, 4 * maxwidth_,
      4 * maxwidth_ * maxheight_, (BYTE*)buffer_.data(), &bitmap_));
  D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
      D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(),
      (FLOAT)::GetDpiForSystem(), (FLOAT)::GetDpiForSystem());
  CHECK(d2d1factory_->CreateWicBitmapRenderTarget(bitmap_.Get(), &props,
                                                  &rendertarget_));

  if (config.locale.empty()) {
    wchar_t locale[LOCALE_NAME_MAX_LENGTH];
    int ret = ::GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH);
    if (ret) {
      locale_ = locale;
    }
  }

  ComPtr<IDWriteInMemoryFontFileLoader> memoryfontfileloader;
  CHECK(dwritefactory_->CreateInMemoryFontFileLoader(&memoryfontfileloader));
  CHECK(dwritefactory_->RegisterFontFileLoader(memoryfontfileloader.Get()));

  ComPtr<IDWriteFontSetBuilder2> fontsetbuilder;
  CHECK(dwritefactory_->CreateFontSetBuilder(&fontsetbuilder));

  ComPtr<IDWriteFontCollection> systemfontcollection;
  CHECK(dwritefactory_->GetSystemFontCollection(&systemfontcollection));

  std::vector<Config::Font*> fontconfiglist;
  for (const Config::Font& font : config.fonts) {
    if (!font.name.empty()) {
      UINT32 index = 0;
      BOOL exists = FALSE;
      CHECK(systemfontcollection->FindFamilyName(font.name.c_str(), &index, &exists));
      if (!exists) {
        continue;
      }

      ComPtr<IDWriteFontFamily> fontfamily;
      CHECK(systemfontcollection->GetFontFamily(index, &fontfamily));
      ComPtr<IDWriteFontSetBuilder2> fontsetbuilder2;
      CHECK(dwritefactory_->CreateFontSetBuilder(&fontsetbuilder2));
      UINT32 count = fontfamily->GetFontCount();
      for (UINT32 i = 0; i < count; ++i) {
        ComPtr<IDWriteFont> font;
        CHECK(fontfamily->GetFont(i, &font));
        ComPtr<IDWriteFont3> font3;
        CHECK(font.As(&font3));

        ComPtr<IDWriteFontFaceReference> fontfacereference;
        CHECK(font3->GetFontFaceReference(&fontfacereference));

        CHECK(fontsetbuilder2->AddFontFaceReference(fontfacereference.Get()));
      }
      ComPtr<IDWriteFontSet> systemfontset;
      CHECK(fontsetbuilder2->CreateFontSet(&systemfontset));
      CHECK(fontsetbuilder->AddFontSet(systemfontset.Get()));
      fontconfiglist.insert(fontconfiglist.end(), (size_t)1,
                            (Config::Font*)&font);
    } else if (font.data != nullptr && font.size) {
      ComPtr<IDWriteFontFile> fontfile;
      CHECK(memoryfontfileloader->CreateInMemoryFontFileReference(
          dwritefactory_.Get(), font.data, (UINT32)font.size, NULL, &fontfile));
      BOOL supported = FALSE;
      DWRITE_FONT_FILE_TYPE filetype{};
      DWRITE_FONT_FACE_TYPE facetype{};
      UINT32 faces = 0;
      CHECK(fontfile->Analyze(&supported, &filetype, &facetype, &faces));
      if (supported && faces) {
        CHECK(fontsetbuilder->AddFontFile(fontfile.Get()));
      }
      fontconfiglist.insert(fontconfiglist.end(), (size_t)faces,
                            (Config::Font*)&font);
    }
  }

  CHECK(fontsetbuilder->CreateFontSet(&fontset_));
  CHECK(dwritefactory_->CreateFontCollectionFromFontSet(fontset_.Get(),
                                                        &fontcollection_));
  firstfamilyname_.clear();
  for (int i = 0; i < (int)fontconfiglist.size(); ++i) {
    ComPtr<IDWriteFontFamily1> fontfamily;
    CHECK(fontcollection_->GetFontFamily(i, &fontfamily));

    ComPtr<IDWriteLocalizedStrings> names;
    CHECK(fontfamily->GetFamilyNames(&names));
    UINT32 count = names->GetCount();

    static wchar_t familyname[1024];

    if (i == 0) {
      CHECK(names->GetString(0, familyname, 1024));
      firstfamilyname_ = familyname;
    }

    for (UINT32 j = 0; j < count; ++j) {
      CHECK(names->GetString(j, familyname, 1024));
    }

    fontfamilymap_[familyname] = fontconfiglist[i];
  }

  if (firstfamilyname_.empty()) {
    return false;
  }

  ComPtr<IDWriteFontFallbackBuilder> fallbackbuilder;
  CHECK(dwritefactory_->CreateFontFallbackBuilder(&fallbackbuilder));
  for (const Config::Fallback& fallback : config.fallbacks) {
    std::vector<DWRITE_UNICODE_RANGE> ranges;
    for (const std::pair<uint32_t, uint32_t>& pair : fallback.ranges) {
      DWRITE_UNICODE_RANGE range{};
      range.first = pair.first;
      range.last = pair.second;
      ranges.push_back(range);
    }
    const wchar_t* family = fallback.family.c_str();
    CHECK(fallbackbuilder->AddMapping(
        (const DWRITE_UNICODE_RANGE*)ranges.data(), (UINT32)ranges.size(),
        (const WCHAR**)&family, 1, fontcollection_.Get()));
  }
  CHECK(fallbackbuilder->CreateFontFallback(&fallback_));

  outlinetextrenderer_ = Make<TextRenderer>(
      d2d1factory_, rendertarget_, [=](IDWriteFontFace* ff) -> float {
        ComPtr<IDWriteFontFace5> ff5;
        CHECK(ff->QueryInterface<IDWriteFontFace5>(&ff5));

        ComPtr<IDWriteLocalizedStrings> names;
        CHECK(ff5->GetFamilyNames(&names));
        static wchar_t buf[1024];
        CHECK(names->GetString(0, buf, 1024));

        std::wstring wstr(buf);
        if (fontfamilymap_.count(wstr)) {
          float offset = fontfamilymap_.at(wstr)->vertical_offset;
          return offset;
        }
        return 0.0f;
      });

  return true;
}

bool SimpleDirectWrite::CalcSize(const std::wstring& text, float size,
                                 int* width, int* height) {
  ComPtr<IDWriteTextFormat> format;
  CHECK(dwritefactory_->CreateTextFormat(
      firstfamilyname_.c_str(), fontcollection_.Get(),
      DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_NORMAL,
      DWRITE_FONT_STRETCH_NORMAL, size, locale_.c_str(), &format));

  ComPtr<IDWriteTextFormat3> format3;
  format.As(&format3);
  if (fallback_) {
    CHECK(format3->SetFontFallback(fallback_.Get()));
  }

  ComPtr<IDWriteTextLayout> layout;
  CHECK(dwritefactory_->CreateTextLayout(text.c_str(), (UINT32)text.length(),
                                         format3.Get(), (FLOAT)maxwidth_,
                                         (FLOAT)maxheight_, &layout));

  DWRITE_TEXT_METRICS metrics{};
  CHECK(layout->GetMetrics(&metrics));
  *width = (int)metrics.width;
  *height = (int)metrics.height;
  return true;
}

const std::vector<uint8_t>& SimpleDirectWrite::Render(
    const std::wstring& text, float size, const float (&color)[4], bool outline,
    const float (&outline_color)[4], int* out_width, int* out_height) {
  ComPtr<IDWriteTextFormat> format;
  CHECK(dwritefactory_->CreateTextFormat(
      firstfamilyname_.c_str(), fontcollection_.Get(),
      DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_NORMAL,
      DWRITE_FONT_STRETCH_NORMAL, size, locale_.c_str(), &format));

  ComPtr<IDWriteTextFormat3> format3;
  format.As(&format3);
  if (fallback_) {
    CHECK(format3->SetFontFallback(fallback_.Get()));
  }

  ComPtr<IDWriteTextLayout> layout;
  CHECK(dwritefactory_->CreateTextLayout(
      text.c_str(), (UINT32)text.length(), format3.Get(),
      (FLOAT)maxwidth_, (FLOAT)maxheight_, &layout));

  DWRITE_TEXT_METRICS metrics{};
  CHECK(layout->GetMetrics(&metrics));
  if (out_width != nullptr) *out_width = (int)metrics.width;
  if (out_height != nullptr) *out_height = (int)metrics.height;

  outlinetextrenderer_.Get()->Setup(color, outline, outline_color);

  rendertarget_->BeginDraw();
  {
    rendertarget_->Clear(D2D1::ColorF(1, 1, 1, 1));
    rendertarget_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    rendertarget_->SetTransform(D2D1::Matrix3x2F::Identity());
    outlinetextrenderer_.Get()->Setup(color, outline, outline_color);
    layout->Draw(NULL, (IDWriteTextRenderer*)outlinetextrenderer_.Get(), 0.0f,
                 0.0f);
  }
  rendertarget_->EndDraw();

  return buffer_;
}

void SimpleDirectWrite::SaveAsBitmap(const std::wstring& path, int width,
                                     int height) {
  ComPtr<IStream> file;
  CHECK(::SHCreateStreamOnFileEx(
      path.c_str(), STGM_CREATE | STGM_WRITE | STGM_SHARE_EXCLUSIVE,
      FILE_ATTRIBUTE_NORMAL, TRUE, nullptr, &file));
  ComPtr<IWICBitmapEncoder> encoder;
  auto guid = GUID_ContainerFormatBmp;
  CHECK(wicimagingfactory_->CreateEncoder(guid, nullptr, &encoder));
  CHECK(encoder->Initialize(file.Get(), WICBitmapEncoderNoCache));
  ComPtr<IWICBitmapFrameEncode> frame;
  ComPtr<IPropertyBag2> properties;
  CHECK(encoder->CreateNewFrame(&frame, &properties));
  CHECK(frame->Initialize(properties.Get()));
  UINT w = 0, h = 0;
  CHECK(bitmap_->GetSize(&w, &h));
  if (!width) width = w;
  if (!height) height = h;
  CHECK(frame->SetSize(width, height));
  GUID pixel_format;
  CHECK(bitmap_->GetPixelFormat(&pixel_format));
  CHECK(frame->SetPixelFormat(&pixel_format));
  CHECK(frame->WriteSource(bitmap_.Get(), nullptr));
  CHECK(frame->Commit());
  CHECK(encoder->Commit());
}

SimpleDirectWrite::SimpleDirectWrite() {
  CHECK(::CoCreateInstance(CLSID_WICImagingFactory2, nullptr,
                           CLSCTX_INPROC_SERVER, __uuidof(IWICImagingFactory2),
                           &wicimagingfactory_));
  CHECK(::D2D1CreateFactory<ID2D1Factory7>(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                           &d2d1factory_));
  CHECK(::DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                              __uuidof(IDWriteFactory7), &dwritefactory_));
}

TextRenderer::TextRenderer(
    ComPtr<ID2D1Factory7> d2d1factory, ComPtr<ID2D1RenderTarget> rendertarget,
    std::function<float(IDWriteFontFace*)> rendercallback)
    : d2d1factory_(d2d1factory),
      rendertarget_(rendertarget),
      rendercallback_(rendercallback) {}

HRESULT __stdcall TextRenderer::IsPixelSnappingDisabled(
    void* clientDrawingContext, BOOL* isDisabled) {
  *isDisabled = FALSE;
  return S_OK;
}

HRESULT __stdcall TextRenderer::GetCurrentTransform(void* clientDrawingContext,
                                                    DWRITE_MATRIX* transform) {
  rendertarget_->GetTransform((D2D1_MATRIX_3X2_F*)transform);
  return S_OK;
}

HRESULT __stdcall TextRenderer::GetPixelsPerDip(void* clientDrawingContext,
                                                FLOAT* pixelsPerDip) {
  *pixelsPerDip = 1.0f;
  return S_OK;
}

HRESULT __stdcall TextRenderer::DrawGlyphRun(
    void* clientDrawingContext, FLOAT baselineOriginX, FLOAT baselineOriginY,
    DWRITE_MEASURING_MODE measuringMode, DWRITE_GLYPH_RUN const* glyphRun,
    DWRITE_GLYPH_RUN_DESCRIPTION const* glyphRunDescription,
    IUnknown* clientDrawingEffect) {
  ComPtr<ID2D1TransformedGeometry> transformedgeometry;
  ComPtr<ID2D1PathGeometry> pathgeometry;
  ComPtr<ID2D1GeometrySink> geometrysink;

  float vertical_offset = rendercallback_(glyphRun->fontFace);

  CHECK(d2d1factory_->CreatePathGeometry(&pathgeometry));
  CHECK(pathgeometry->Open(&geometrysink));
  {
    CHECK(glyphRun->fontFace->GetGlyphRunOutline(
        glyphRun->fontEmSize, glyphRun->glyphIndices, glyphRun->glyphAdvances,
        glyphRun->glyphOffsets, glyphRun->glyphCount, glyphRun->isSideways,
        glyphRun->bidiLevel % 2, geometrysink.Get()));
  }
  CHECK(geometrysink->Close());
  D2D1::Matrix3x2F transform =
      D2D1::Matrix3x2F(1.0f, 0.0f, 0.0f, 1.0f, baselineOriginX,
                       baselineOriginY + vertical_offset);
  CHECK(d2d1factory_->CreateTransformedGeometry(pathgeometry.Get(), transform,
                                                &transformedgeometry));
  rendertarget_->DrawGeometry(transformedgeometry.Get(), outline_brush_.Get(),
                              3.0f);
  rendertarget_->FillGeometry(transformedgeometry.Get(), fill_brush_.Get());
  return S_OK;
}

HRESULT __stdcall TextRenderer::DrawUnderline(void* clientDrawingContext,
                                              FLOAT baselineOriginX,
                                              FLOAT baselineOriginY,
                                              DWRITE_UNDERLINE const* underline,
                                              IUnknown* clientDrawingEffect) {
  return E_NOTIMPL;
}

HRESULT __stdcall TextRenderer::DrawStrikethrough(
    void* clientDrawingContext, FLOAT baselineOriginX, FLOAT baselineOriginY,
    DWRITE_STRIKETHROUGH const* strikethrough, IUnknown* clientDrawingEffect) {
  return E_NOTIMPL;
}

HRESULT __stdcall TextRenderer::DrawInlineObject(
    void* clientDrawingContext, FLOAT originX, FLOAT originY,
    IDWriteInlineObject* inlineObject, BOOL isSideways, BOOL isRightToLeft,
    IUnknown* clientDrawingEffect) {
  return E_NOTIMPL;
}

void TextRenderer::Setup(const float (&color)[4], bool outline,
                         const float (&outline_color)[4]) {
  D2D1::ColorF d2d1color(color[0], color[1], color[2], color[3]);
  D2D1::ColorF d2d1outlinecolor(outline_color[0], outline_color[1],
                                outline_color[2], outline_color[3]);
  CHECK(rendertarget_->CreateSolidColorBrush(d2d1color, &fill_brush_));
  CHECK(
      rendertarget_->CreateSolidColorBrush(d2d1outlinecolor, &outline_brush_));
}
