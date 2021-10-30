#include "simpledwrite.h"

#include <stdexcept>
#include <sstream>

#include <shlwapi.h>
#include <shellapi.h>
#include <comdef.h>

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

void SimpleDirectWrite::Setup(const Config& config) {
  ComPtr<IDWriteInMemoryFontFileLoader> memoryfontfileloader;
  CHECK(dwritefactory_->CreateInMemoryFontFileLoader(&memoryfontfileloader));
  CHECK(dwritefactory_->RegisterFontFileLoader(memoryfontfileloader.Get())); 

  {
    ComPtr<IDWriteFontFile> fontfile;
    CHECK(memoryfontfileloader->CreateInMemoryFontFileReference(
        dwritefactory_.Get(), config.font_data, (UINT32)config.font_data_size, NULL,
        &fontfile));
    ComPtr<IDWriteFontSetBuilder2> fontsetbuilder;
    CHECK(dwritefactory_->CreateFontSetBuilder(&fontsetbuilder));
    CHECK(fontsetbuilder->AddFontFile(fontfile.Get()));
    ComPtr<IDWriteFontSet> fontset;
    CHECK(fontsetbuilder->CreateFontSet(&fontset));
    ComPtr<IDWriteFontCollection1> fontcollection;
    CHECK(dwritefactory_->CreateFontCollectionFromFontSet(fontset.Get(),
                                                         &fontcollection));

    ComPtr<IDWriteFontFamily1> fontfamily;
    CHECK(fontcollection->GetFontFamily(0, &fontfamily));
    ComPtr<IDWriteLocalizedStrings> names;
    CHECK(fontfamily->GetFamilyNames(&names));
    CHECK(names->GetString(0, familyname_, 1024));

    UINT32 index = 0;
    BOOL exists = FALSE;
    CHECK(names->FindLocaleName(config.locale.c_str(), &index, &exists));
    if (!exists) {
      throw std::domain_error("locale name not found.");
    }
  }

  if (config.fallback_font_data && config.fallback_font_data_size) {
    ComPtr<IDWriteFontFile> fontfile_fallback;
    CHECK(memoryfontfileloader->CreateInMemoryFontFileReference(
        dwritefactory_.Get(), config.fallback_font_data,
        (UINT32)config.fallback_font_data_size, NULL, &fontfile_fallback));

    ComPtr<IDWriteFontSetBuilder2> fontsetbuilder;
    CHECK(dwritefactory_->CreateFontSetBuilder(&fontsetbuilder));
    CHECK(fontsetbuilder->AddFontFile(fontfile_fallback.Get()));
    ComPtr<IDWriteFontSet> fontset;
    CHECK(fontsetbuilder->CreateFontSet(&fontset));
    ComPtr<IDWriteFontCollection1> fontcollection;
    CHECK(dwritefactory_->CreateFontCollectionFromFontSet(fontset.Get(),
                                                         &fontcollection));

    ComPtr<IDWriteFontFamily1> fontfamily;
    CHECK(fontcollection->GetFontFamily(0, &fontfamily));
    ComPtr<IDWriteLocalizedStrings> names;
    CHECK(fontfamily->GetFamilyNames(&names));
    CHECK(names->GetString(0, familyname_fallback_, 1024));

    ComPtr<IDWriteFontFallbackBuilder> fallbackbuilder;
    CHECK(dwritefactory_->CreateFontFallbackBuilder(&fallbackbuilder));
    DWRITE_UNICODE_RANGE range;
    range.first = config.fallback_font_range_min;
    range.last = config.fallback_font_range_max;
    const WCHAR* familynames[1]{(const WCHAR*)familyname_fallback_};
    CHECK(fallbackbuilder->AddMapping(&range, 1, familynames, 1,
                                      fontcollection.Get(),
                                      config.fallback_locale.c_str()));
    CHECK(fallbackbuilder->CreateFontFallback(&fallback_));

    UINT32 index = 0;
    BOOL exists = FALSE;
    CHECK(names->FindLocaleName(config.fallback_locale.c_str(), &index, &exists));
    if (!exists) {
      throw std::domain_error("locale name not found.");
    }
  }
}

bool SimpleDirectWrite::CalcSize(const std::wstring& text, float size,
                                 int* width, int* height) {
  ComPtr<IDWriteTextFormat> format;
  CHECK(dwritefactory_->CreateTextFormat(
      familyname_, fontcollection_.Get(), DWRITE_FONT_WEIGHT_REGULAR,
      DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size,
      config_.locale.c_str(), &format));

  ComPtr<IDWriteTextFormat3> format3;
  format.As(&format3);
  if (fallback_) {
    CHECK(format3->SetFontFallback(fallback_.Get()));
  }

  ComPtr<IDWriteTextLayout> layout;
  CHECK(dwritefactory_->CreateTextLayout(text.c_str(), (UINT32)text.length(),
                                        format3.Get(), (FLOAT)buffer_width_,
                                        (FLOAT)buffer_height_, &layout));

  DWRITE_TEXT_METRICS metrics{};
  CHECK(layout->GetMetrics(&metrics));
  *width = (int)metrics.width;
  *height = (int)metrics.height;
  return true;
}

const std::vector<uint8_t>& SimpleDirectWrite::Render(
    const std::wstring& text, float size, const Color& color, int canvas_width,
    int canvas_height, int* out_width, int* out_height) {
  ComPtr<IDWriteTextFormat> format;
  CHECK(dwritefactory_->CreateTextFormat(
      familyname_, fontcollection_.Get(), DWRITE_FONT_WEIGHT_REGULAR,
      DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size,
      config_.locale.c_str(), &format));

  ComPtr<IDWriteTextFormat3> format3;
  format.As(&format3);
  if (fallback_) {
    CHECK(format3->SetFontFallback(fallback_.Get()));
  }

  ComPtr<IDWriteTextLayout> layout;
  CHECK(dwritefactory_->CreateTextLayout(text.c_str(), (UINT32)text.length(),
                                        format3.Get(), (FLOAT)buffer_width_,
                                        (FLOAT)buffer_height_, &layout));

  ComPtr<ID2D1SolidColorBrush> brush;
  CHECK(rendertarget_->CreateSolidColorBrush(
      D2D1::ColorF(color.r, color.g, color.b, color.a), &brush));

  rendertarget_->BeginDraw();
  {
    rendertarget_->Clear(D2D1::ColorF(1, 1, 1, 1));
    rendertarget_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    rendertarget_->SetTransform(D2D1::Matrix3x2F::Identity());
    layout->Draw(NULL, (IDWriteTextRenderer*)outlinetextrenderer_.Get(), 0.0f, 0.0f);
  }
  rendertarget_->EndDraw();

  return buffer_;
}

void SimpleDirectWrite::SaveAsBitmap(const std::wstring& path) {
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
  CHECK(frame->SetSize(w, h));
  GUID pixel_format;
  CHECK(bitmap_->GetPixelFormat(&pixel_format));
  CHECK(frame->SetPixelFormat(&pixel_format));
  CHECK(frame->WriteSource(bitmap_.Get(), nullptr));
  CHECK(frame->Commit());
  CHECK(encoder->Commit());
}

// Custom text renderer class
// ref. https://stackoverflow.com/questions/66872711/directwrite-direct2d-custom-text-rendering-is-hairy
class OutlineTextRenderer : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IDWriteTextRenderer> {
private:
  ComPtr<ID2D1Factory7> d2d1factory_;
  ComPtr<ID2D1RenderTarget> rendertarget_;
  ComPtr<ID2D1SolidColorBrush> outline_brush_;
  ComPtr<ID2D1SolidColorBrush> fill_brush_;

private:
  ~OutlineTextRenderer() = default;

public:
  OutlineTextRenderer(ComPtr<ID2D1Factory7> d2d1factory,
                      ComPtr<ID2D1RenderTarget> rendertarget)
      : d2d1factory_(d2d1factory), rendertarget_(rendertarget){
    CHECK(rendertarget_->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &fill_brush_));
    CHECK(rendertarget_->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f), &outline_brush_));
  };

  virtual HRESULT __stdcall IsPixelSnappingDisabled(void* clientDrawingContext,
                                                    BOOL* isDisabled) override {
    *isDisabled = FALSE;
    return S_OK;
  }
  virtual HRESULT __stdcall GetCurrentTransform(
      void* clientDrawingContext, DWRITE_MATRIX* transform) override {
    rendertarget_->GetTransform((D2D1_MATRIX_3X2_F*)transform);
    return S_OK;
  }
  virtual HRESULT __stdcall GetPixelsPerDip(void* clientDrawingContext,
                                            FLOAT* pixelsPerDip) override {
    *pixelsPerDip = 1.0f;
    return S_OK;
  }
  virtual HRESULT __stdcall DrawGlyphRun(
      void* clientDrawingContext, FLOAT baselineOriginX, FLOAT baselineOriginY,
      DWRITE_MEASURING_MODE measuringMode, DWRITE_GLYPH_RUN const* glyphRun,
      DWRITE_GLYPH_RUN_DESCRIPTION const* glyphRunDescription,
      IUnknown* clientDrawingEffect) override {
    ComPtr<ID2D1TransformedGeometry> transformedgeometry;
    ComPtr<ID2D1PathGeometry> pathgeometry;
    ComPtr<ID2D1GeometrySink> geometrysink;
    CHECK(d2d1factory_->CreatePathGeometry(&pathgeometry));
    CHECK(pathgeometry->Open(&geometrysink));
    {
      CHECK(glyphRun->fontFace->GetGlyphRunOutline(
          glyphRun->fontEmSize, glyphRun->glyphIndices, glyphRun->glyphAdvances,
          glyphRun->glyphOffsets, glyphRun->glyphCount, glyphRun->isSideways,
          glyphRun->bidiLevel % 2, geometrysink.Get()));
    }
    CHECK(geometrysink->Close());
    D2D1::Matrix3x2F transform = D2D1::Matrix3x2F(1.0f, 0.0f, 0.0f, 1.0f, baselineOriginX, baselineOriginY);
    CHECK(d2d1factory_->CreateTransformedGeometry(pathgeometry.Get(), transform, &transformedgeometry));
    rendertarget_->DrawGeometry(transformedgeometry.Get(), outline_brush_.Get(), 3.0f);
    rendertarget_->FillGeometry(transformedgeometry.Get(), fill_brush_.Get());
    return S_OK;
  }
  virtual HRESULT __stdcall DrawUnderline(
      void* clientDrawingContext, FLOAT baselineOriginX, FLOAT baselineOriginY,
      DWRITE_UNDERLINE const* underline,
      IUnknown* clientDrawingEffect) override {
    return E_NOTIMPL;
  }
  virtual HRESULT __stdcall DrawStrikethrough(
      void* clientDrawingContext, FLOAT baselineOriginX, FLOAT baselineOriginY,
      DWRITE_STRIKETHROUGH const* strikethrough,
      IUnknown* clientDrawingEffect) override {
    return E_NOTIMPL;
  }
  virtual HRESULT __stdcall DrawInlineObject(
      void* clientDrawingContext, FLOAT originX, FLOAT originY,
      IDWriteInlineObject* inlineObject, BOOL isSideways, BOOL isRightToLeft,
      IUnknown* clientDrawingEffect) override {
    return E_NOTIMPL;
  }
};

SimpleDirectWrite::SimpleDirectWrite() {
  CHECK(::CoCreateInstance(CLSID_WICImagingFactory2, nullptr,
                           CLSCTX_INPROC_SERVER, __uuidof(IWICImagingFactory2),
                           &wicimagingfactory_));
  CHECK(::D2D1CreateFactory<ID2D1Factory7>(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                           &d2d1factory_));
  CHECK(::DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                              __uuidof(IDWriteFactory7), &dwritefactory_));

  buffer_.resize(4 * buffer_width_ * buffer_height_);
  CHECK(wicimagingfactory_->CreateBitmapFromMemory(
      buffer_width_, buffer_height_, GUID_WICPixelFormat32bppPBGRA,
      4 * buffer_width_, 4 * buffer_width_ * buffer_height_,
      (BYTE*)buffer_.data(), &bitmap_));
  D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
      D2D1_RENDER_TARGET_TYPE_DEFAULT,
      D2D1::PixelFormat(),
      (FLOAT)::GetDpiForSystem(), (FLOAT)::GetDpiForSystem());
  CHECK(d2d1factory_->CreateWicBitmapRenderTarget(bitmap_.Get(), &props, &rendertarget_));

  outlinetextrenderer_ = Make<OutlineTextRenderer>(d2d1factory_, rendertarget_);
}

