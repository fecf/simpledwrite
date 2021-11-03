#include <cassert>
#include <codecvt>
#include <fstream>
#include <iostream>
#include <locale>
#include <string>

#include "../simpledwrite.h"
#include "iconfont.h"

#pragma warning(disable : 4996)  // codecvt

int main(void) {
  try {
    ::CoInitialize(NULL);

    // NONCLIENTMETRICSW ncm{sizeof(ncm)};
    // ::SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, 0, &ncm, 0);
    // std::wstring systemfont = ncm.lfMessageFont.lfFaceName;

    SimpleDirectWrite::Config config;
    config.locale = L"ja-JP";
    config.fonts.push_back(SimpleDirectWrite::Config::Font(L"Arial"));
    config.fonts.push_back(SimpleDirectWrite::Config::Font(L"Meiryo"));
    config.fonts.push_back(SimpleDirectWrite::Config::Font(
        remixicon_ttf, remixicon_ttf_len, 3.0f));
    std::vector<std::pair<uint32_t, uint32_t>> jp = {
        {0x3000, 0x303f}, {0x3040, 0x30ff}, {0x30a0, 0x30ff},
        {0xff00, 0xffef}, {0x4e00, 0x9faf},
    };
    config.fallbacks.push_back(SimpleDirectWrite::Config::Fallback(L"Meiryo", jp));
    config.fallbacks.push_back(SimpleDirectWrite::Config::Fallback(
        L"remixicon", {{ICON_REMIX_RANGE_MIN, ICON_REMIX_RANGE_MAX}}));

    SimpleDirectWrite dw;
    dw.Setup(config);

    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
    std::wstring wide =
        converter.from_bytes("abcde あいうえお 骨肉刃" ICON_REMIX_FOLDER_FILL ICON_REMIX_ARROW_DOWN_FILL);
    float fill[4] = {1, 1, 1, 1};
    float outline[4] = {0, 0, 0, 1};
    int ow, oh;
    std::vector<uint8_t> buf = dw.Render(wide.c_str(), 32.0f, fill, true, outline, &ow, &oh);

    std::ofstream ofs("test.bin", std::ios::out | std::ios::binary);
    ofs.write((const char*)buf.data(), buf.size());
    ofs.close();

    dw.SaveAsBitmap(L"test.bmp");
  } catch (std::exception& ex) {
    std::cerr << ex.what() << std::endl;
  }

  ::CoUninitialize();

  return 0;
}