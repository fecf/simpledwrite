# simpledwrite

Simple DirectWrite Wrapper

## Minimal example

```
  SimpleDWrite dw;
  std::vector<uint8_t> buf(1024 * 1024 * 4);
  Layout layout(32);  // font size
  dw.Render("SimpleDWrite", buf.data(), (int)buf.size(), layout);
  save_png("test_minimal.png", buf.data(), layout.out_width, layout.out_height);
```

## Full example

See [demo/demo.cc](demo/demo.cc)

![test_minimal](https://user-images.githubusercontent.com/6128431/156881001-5f73b071-1c91-44c0-96be-9fda5ce7879a.png)  
![test_full](https://user-images.githubusercontent.com/6128431/156880996-2129d00a-b341-4b8c-a449-de47989ca77a.png)
