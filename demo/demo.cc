#include <locale>
#include <iostream>
#include <string>

#include "../simpledwrite.h"
using namespace simpledwrite;

#include "iconfont.h"

#include <shlwapi.h>
#include <wincodec.h>
#include <wrl.h>
#pragma comment(lib, "shlwapi.lib")
void save_png(const std::string& name, const uint8_t* data, int w, int h) {
  using namespace Microsoft::WRL;
  ComPtr<IWICImagingFactory> factory;
  ::CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER,
      __uuidof(IWICImagingFactory2), &factory);
  ComPtr<IStream> file;
  ::SHCreateStreamOnFileEx(std::wstring(name.begin(), name.end()).c_str(),
      STGM_CREATE | STGM_WRITE | STGM_SHARE_EXCLUSIVE, FILE_ATTRIBUTE_NORMAL,
      TRUE, nullptr, &file);
  ComPtr<IWICBitmapEncoder> encoder;
  auto guid = GUID_ContainerFormatPng;
  factory->CreateEncoder(guid, nullptr, &encoder);
  encoder->Initialize(file.Get(), WICBitmapEncoderNoCache);
  ComPtr<IWICBitmapFrameEncode> frame;
  ComPtr<IPropertyBag2> properties;
  encoder->CreateNewFrame(&frame, &properties);
  frame->Initialize(properties.Get());
  frame->SetSize(w, h);
  GUID pixel_format = GUID_WICPixelFormat32bppPBGRA;
  ComPtr<IWICBitmap> bitmap;
  factory->CreateBitmapFromMemory(
      w, h, pixel_format, w * 4, w * 4 * h, (BYTE*)data, &bitmap);
  frame->SetPixelFormat(&pixel_format);
  frame->WriteSource(bitmap.Get(), nullptr);
  frame->Commit();
  encoder->Commit();
}

void test_minimal() {
  SimpleDWrite dw;
  std::vector<uint8_t> buf(1024 * 1024 * 4);
  int w, h;
  dw.Render("SimpleDWrite", 32, buf.data(), (int)buf.size(), &w, &h);
  save_png("test_minimal.png", buf.data(), w, h);
}

void test_full() {
  FontSet fontset;
  fontset.locale = "ja-JP";
  fontset.fonts.push_back(Font("Arial"));
  fontset.fonts.push_back(Font("Meiryo"));
  fontset.fonts.push_back(Font(remixicon_ttf, remixicon_ttf_len, 3.0f));
  std::vector<std::pair<uint32_t, uint32_t>> jp = {
      {0x3000, 0x303f},
      {0x3040, 0x30ff},
      {0x30a0, 0x30ff},
      {0xff00, 0xffef},
      {0x4e00, 0x9faf},
  };
  fontset.fallbacks.push_back(FallbackFont("Meiryo", jp));
  fontset.fallbacks.push_back(FallbackFont("remixicon", {{ICON_REMIX_RANGE_MIN, ICON_REMIX_RANGE_MAX}}));

  SimpleDWrite dw;
  if (!dw.Init(fontset)) {
    std::cerr << "failed SimpleDWrite::Init().";
    return;
  }

  std::string str = "SimpleDWrite こんにちは 担々麺" ICON_REMIX_GITHUB_LINE;
  int w, h, size;
  if (!dw.CalcSize(str, 32, &w, &h, &size)) {
    std::cerr << "failed SimpleDWrite::CalcSize().";
    return;
  }
  std::vector<uint8_t> buf(size);

  Layout layout;
  layout.font_style = FontStyle::NORMAL;
  layout.font_weight = FontWeight::NORMAL;
  layout.font_stretch = FontStretch::NORMAL;

  RenderParams rp;
  rp.background_color = {1.0f, 1.0f, 1.0f, 1.0f};
  rp.foreground_color = {1.0f, 0.0f, 0.0f, 1.0f};
  rp.outline_color = {1.0f, 1.0f, 0.0f, 1.0f};
  rp.outline_width = 4;

  if (!dw.Render(str, 32, buf.data(), size, layout, rp)) {
    std::cerr << "failed SimpleDWrite::Render().";
    return;
  }

  save_png("test_full.png", buf.data(), w, h);
}

int main(void) {
  // Set locale to UTF-8
  std::locale::global(std::locale(std::locale(""), new std::numpunct<char>()));

  ::CoInitialize(NULL);
  test_minimal();
  test_full();
  ::CoUninitialize();
  return 0;
}
