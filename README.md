# simpledwrite

Simple DirectWrite Wrapper


## Minimal example

```
  SimpleDWrite dw;
  std::vector<uint8_t> buf(1024 * 1024 * 4);
  int w, h;
  dw.Render("SimpleDWrite", 32, buf.data(), (int)buf.size(), &w, &h);
```
