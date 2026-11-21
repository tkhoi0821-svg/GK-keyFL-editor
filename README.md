# GK KeyFL Editor

Editor 2D viết bằng C thuần (lõi không cần thư viện; cửa sổ dùng SDL3). Là phần "tải riêng" của hub [GK KeyFL](https://github.com/tkhoi0821-svg): hub đọc GitHub Releases của kho này.

**Tính năng 0.1.0:** nhiều lớp (ẩn/hiện, thêm/xoá), bút, tẩy, đổ màu, đường thẳng, hình chữ nhật, chấm màu, hoàn tác/làm lại (32 bước, theo lớp), thu phóng/di chuyển, lưu/mở `.gkproj`, xuất PNG, 4key tuỳ chọn, nhúng media (`--embed`).

## Build
```
make                 # cần SDL3 dev (pkg-config sdl3)  -> ./gk-keyfl-editor
make headless test   # không cần thư viện, chạy kiểm thử lõi + định dạng
```
## Dùng
`./gk-keyfl-editor [tệp.gkproj]` (mặc định `untitled.gkproj` ở thư mục hiện tại; tệp chưa có thì tạo mới khi lưu).

| Phím | Việc | Phím | Việc |
|---|---|---|---|
| P / E / F / L / R / I | bút / tẩy / đổ / đường / chữ nhật / chấm màu | Ctrl+Z, Ctrl+Y | hoàn tác / làm lại |
| `[` `]` | cỡ nét | Ctrl+S / O / N / E | lưu / mở / mới / xuất PNG |
| 0 | vừa khung | lăn chuột / chuột phải kéo | thu phóng / di chuyển |

Chấm cố ý: thêm/xoá lớp xoá lịch sử hoàn tác. Xem [FORMAT.md](FORMAT.md) cho định dạng tệp.

## Phát hành cho hub
Tag = phiên bản (`v0.1.0`). Workflow `.github/workflows/release.yml` build bản Linux và đính vào release với tên `GK-keyFL-editor-linux-x86_64` (có chữ `linux`, hub nhận ra). Windows/Android: chưa có.

## Trạng thái
Đã chạy thật (headless): lõi vẽ, hoàn tác, lưu/mở, 4key, giữ chunk lạ, xuất PNG (đã kiểm CRC/zlib), vẽ giao diện ra ảnh. **Chưa biên dịch/chạy với SDL3 thật** (`run_sdl`), chưa có font Unicode (nhãn tiếng Anh chữ hoa).
