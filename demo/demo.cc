#include "../simpledwrite.h"

#include "iconfont.h"

#include <cassert>
#include <iostream>
#include <fstream>

#include <locale>
#include <codecvt>
#include <string>

#pragma warning(disable: 4996)  // codecvt

int main(void) {
  try {
    ::CoInitialize(NULL);

    std::ifstream ifs("c:\\windows\\fonts\\MSGothic.ttc", std::ios::in | std::ios::binary);
    ifs.seekg(0, ifs.end);
    int length = (int)ifs.tellg();
    ifs.seekg(0, ifs.beg);
    std::vector<char> font_data(length);
    ifs.read((char*)font_data.data(), length);
    if (!ifs) {
      std::cerr << "can't load font file." << std::endl;
      return 1;
    }

    SimpleDirectWrite dw;

    SimpleDirectWrite::Config config;
    config.font_data = font_data.data();
    config.font_data_size = font_data.size();
    config.fallback_font_data = remixicon_ttf;
    config.fallback_font_data_size = remixicon_ttf_len;
    config.fallback_font_range_min = ICON_REMIX_RANGE_MIN;
    config.fallback_font_range_max = ICON_REMIX_RANGE_MAX;
    config.fallback_font_vertical_offset = 3;
    dw.Setup(config);

    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
    std::wstring wide = converter.from_bytes("abcde ああああ 日本語 " ICON_REMIX_FOLDER_FILL);
    dw.Render(wide.c_str(), 32.0f);
    dw.SaveAsBitmap(L"test.bmp");

  } catch (std::exception& ex) {
    std::cerr << ex.what() << std::endl;
  }
 
  ::CoUninitialize();

  return 0;
}