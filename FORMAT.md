# Định dạng .gkproj v1

Tất cả số nguyên little-endian. Một tệp = tiêu đề + dãy chunk, đọc tuần tự.

```
"GKPJ" | ver u8 (=1) | flags u8 (bit0 = có 4key) | 2 byte dự trữ | w u32 | h u32
[nếu bit0: salt u32 | check u32]
chunk*: type[4] | len u32 | payload[len]
```

- `META`: văn bản phiên bản phần mềm tạo ra tệp.
- `LAYR`: `vis u8 | name[32] | RLE`, RLE là các cặp `count u8 (1..255) | pixel u32 ARGB` cho w*h điểm ảnh. Thứ tự lớp: dưới lên trên.
- **Mọi chunk khác được giữ nguyên khi mở/lưu lại** (không cần sửa tệp hay trình đọc cũ). Dùng cho dữ liệu keyfl, nét bút, 3D, v.v.
  Nhúng ảnh/video: `gk-keyfl-editor --embed in.gkproj MEDI file.mp4 out.gkproj` (type 4 ký tự tuỳ chọn, ví dụ `MP4_`, `PNG_`).
- Tệp **tự chứa**: ảnh, mp4, vector nằm hẳn trong tệp, không tham chiếu ngoài.

## 4key (mật khẩu 4 ký tự, tuỳ chọn)
Bật bằng `GK_4KEY=abcd` hoặc `--key abcd`. Phần payload của mọi chunk được XOR với dòng khoá sinh từ (4key, salt ngẫu nhiên); `check` giúp báo sai khoá.
**Đây là khoá nhẹ chống mở nhầm, KHÔNG phải mã hoá mạnh**; không dùng để bảo vệ dữ liệu nhạy cảm. Có thể nâng lên mã hoá thật ở phiên bản sau.
