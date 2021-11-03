# simpledwrite

Simple DirectWrite wrapper

## Usage

```
    SimpleDirectWrite::Config config;
    config.locale = L"ja-JP";
    config.fonts.push_back(SimpleDirectWrite::Config::Font(L"Arial"));
    config.fonts.push_back(SimpleDirectWrite::Config::Font(L"Meiryo"));
    config.fallbacks.push_back(SimpleDirectWrite::Config::Fallback(L"Meiryo", jp));

    SimpleDirectWrite dw;
    dw.Setup(config);
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
    std::wstring wide = converter.from_bytes("hello world こんにちは")
    float fill[4] = {1, 1, 1, 1};
    float outline[4] = {0, 0, 0, 1};

    int ow, oh;
    std::vector<uint8_t> buf = dw.Render(wide.c_str(), 32.0f, fill, true, outline, &ow, &oh);

    std::ofstream ofs("test.bin", std::ios::out | std::ios::binary);
    ofs.write((const char*)buf.data(), buf.size());
    ofs.close();

    dw.SaveAsBitmap(L"test.bmp");
```