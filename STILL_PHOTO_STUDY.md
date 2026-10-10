# Nghiên Cứu & Tối Ưu Hóa Magic Lantern cho Canon 5D Mark II (Chuyên Chụp Ảnh Tĩnh)

Tài liệu kỹ thuật định hướng nghiên cứu, tinh giản và phát triển tính năng cho **Canon EOS 5D Mark II (5D2)** dựa trên nền tảng **Magic Lantern (ML)**, loại bỏ toàn bộ các thành phần video để tối ưu hóa bộ nhớ, thời lượng pin và hiệu năng chụp ảnh tĩnh (Still Photography).

---

## 1. Kiến Trúc & Cấu Trúc Mã Nguồn

```
E:\15. Other\MagicLantern5D2\
├── platform/
│   └── 5D2.212/                 # Thư mục cấu hình & target chính cho 5D Mark II (Firmware 2.1.2)
│       ├── Makefile             # Cấu hình nạp địa chỉ bộ nhớ (AUTOEXEC_BASE=0x800000, ARMv5TE)
│       ├── consts.h             # Địa chỉ phần cứng, thanh ghi DIGIC 4, phím bấm, VRAM buffer
│       ├── features.h           # Bật/tắt các macro tính năng (có thể override src/all_features.h)
│       ├── modules.included     # Danh sách các module .mo được đóng gói vào build 5D2
│       ├── stubs.S              # Bảng địa chỉ hàm gọi nội bộ của Canon DryOS (DryOS function stubs)
│       └── ML-SETUP.FIR         # File update firmware để bật BOOTFLAG lần đầu
├── src/                         # Mã nguồn Core (C / ASM)
│   ├── all_features.h           # Danh mục toàn bộ cờ tính năng (Feature flags)
│   ├── shoot.c                  # Core chụp ảnh: Trap Focus, Motion Detect, HDR Bracketing, Bulb Timer
│   ├── lens.c                   # Điều khiển động cơ thấu kính: Focus Stacking, Rack Focus, Khẩu độ
│   ├── raw.c                    # Xử lý buffer dữ liệu thô (RAW frame buffer) từ cảm biến
│   └── dryos.h / tasks.c        # Hook vào hệ thống quản lý tác vụ RTOS của DryOS
└── modules/                     # Các module nạp động (.mo) chạy trên thẻ CF
    ├── dual_iso/                # Nâng dải tần nhạy sáng (Dynamic Range) lên ~14 stops
    ├── ettr/                    # Tự động phơi sáng chuẩn Expose To The Right (không cháy highlight)
    ├── silent/                  # Silent Picture: Chụp DNG trực tiếp từ LiveView sensor không dùng màn trập cơ
    ├── dot_tune/                # Tự động cân chỉnh vi sai điểm nét ống kính (AF Microadjustment)
    ├── adv_int/                 # Bộ đếm ngắt quãng nâng cao (Advanced Intervalometer)
    └── lua/                     # Động cơ kịch bản Lua điều khiển máy ảnh tự động
```

---

## 2. Các Tính Năng Trọng Tâm Cho Chụp Ảnh Tĩnh (Still Photography)

### 2.1. Chất Lượng Ảnh & Dải Sáng (Dynamic Range)
1. **`dual_iso` (Dual ISO)**:
   * **Cơ chế**: Can thiệp vào bộ khuếch đại tín hiệu analog của cảm biến DIGIC 4, đọc xen kẽ 2 dòng ISO khác nhau (ví dụ: dòng chẵn ISO 100, dòng lẻ ISO 1600).
   * **Hiệu quả**: Nâng Dynamic Range từ ~11 stops lên **~14.5 stops** trong một file ảnh RAW `.CR2` duy nhất, không cần chụp ghép nhiều tấm như HDR truyền thống (không bị bóng ma chuyển động).
2. **`ettr` (Auto ETTR - Expose To The Right)**:
   * **Cơ chế**: Đo sáng dựa trực tiếp trên biểu đồ Histogram RAW (không phải Histogram JPEG đã qua xử lý của máy) và tự tính toán thời gian phơi sáng/ISO tối đa để đẩy thông tin sang phải dải sáng mà không bị bão hòa (clipping) kênh màu.
   * **Hiệu quả**: Giảm thiểu tối đa nhiễu hạt (noise) ở vùng shadow, tối ưu chất lượng cảm biến Full-frame.

### 2.2. Bảo Vệ Cơ Học & Chống Rung Tuyệt Đối
1. **`silent` (Silent Picture)**:
   * **Cơ chế**: Đọc trực tiếp frame buffer từ cảm biến LiveView qua bộ điều khiển DMA (EDMAC) và xuất thẳng ra file ảnh tĩnh DNG hoặc Full-res Silent Picture.
   * **Hiệu quả**: Hoàn toàn **không dùng màn trập cơ học** (không tốn shot count, không phát ra tiếng động, không rung vi chấn do màn trập/gương lật). Rất thích hợp cho Timelapse ảnh tĩnh hàng vạn khung hình hoặc chụp thiên văn.
2. **`FEATURE_MLU` (Mirror Lock-Up nâng cao)**:
   * Tự động khóa gương lật trước khi xả màn trập với thời gian trễ có thể tùy biến mà không cần gạt qua các menu rườm rà của Canon.

### 2.3. Lấy Nét Macro & Tự Động Hóa Chụp
1. **`FEATURE_FOCUS_STACKING` (`src/lens.c`)**:
   * Tự động bước vi sai động cơ lấy nét (USM/STM motor) của ống kính qua giao tiếp Canon EF bus để chụp một chuỗi ảnh với các mặt phẳng lấy nét liên tục, phục vụ ghép nét trường sâu (macro/phong cảnh) không cần thước trượt cơ học.
2. **`FEATURE_TRAP_FOCUS` (`src/shoot.c`)**:
   * Máy tự động nhả màn trập ngay tức khắc khi chủ thể di chuyển lọt vào điểm lấy nét đã thiết lập (rất hữu ích khi dùng ống kính lấy nét tay Manual Focus).
3. **`FEATURE_MOTION_DETECT` & `FEATURE_AUDIO_REMOTE_SHOT`**:
   * Chụp bẫy ảnh tự động khi phát hiện thay đổi điểm ảnh trong khung hình LiveView hoặc khi có âm thanh lớn qua micro.
4. **`dot_tune`**:
   * Tự động thực hiện thuật toán kiểm tra xác suất lấy nét ở các mức vi chỉnh AF khác nhau để tìm ra giá trị AF Microadjustment tối ưu nhất cho từng chiếc ống kính lắp trên 5D2.

---

## 3. Kế Hoạch Tinh Giản (Loại Bỏ Hoàn Toàn Tính Năng Video)

5D2 có dung lượng SDRAM hạn chế (~256MB chia sẻ giữa DryOS, buffer LiveView và ML). Việc loại bỏ các module và thread xử lý video giúp:
- Giải phóng đáng kể RAM cho bộ đệm chụp ảnh tĩnh và Silent Picture.
- Giảm số lượng background task, tối ưu thời lượng pin.
- Giảm độ trễ khởi động máy.

### 3.1. Danh sách Module Video cần loại bỏ (`platform/5D2.212/modules.included`)
Sửa `modules.included` loại bỏ các dòng sau:
```diff
- mlv_lite
- mlv_play
- mlv_snd
- crop_rec
```

Giữ lại danh sách chuyên dụng cho chụp ảnh tĩnh:
```
adv_int
autoexpo
deflick
dot_tune
dual_iso
edmac
ettr
file_man
img_name
lua
pic_view
selftest
silent
```

### 3.2. Cấu hình cờ Macro tắt Video trong `platform/5D2.212/features.h`
Thêm các directive vô hiệu hóa phần quay phim:
```c
// Vô hiệu hóa tính năng quay phim
#undef FEATURE_NITRATE
#undef FEATURE_MOVIE_LOGGING
#undef FEATURE_MOVIE_RESTART
#undef FEATURE_REC_NOTIFY
#undef FEATURE_MOVIE_REC_KEY
#undef FEATURE_FPS_OVERRIDE
#undef FEATURE_FPS_RAMPING
#undef FEATURE_HDR_VIDEO
#undef FEATURE_SHUTTER_LOCK
#undef FEATURE_GRADUAL_EXPOSURE
#undef FEATURE_IMAGE_EFFECTS
```

---

## 4. Hướng Dẫn Biên Dịch & Thử Nghiệm

### 4.1. Chuẩn Bị Toolchain
* **Trình biên dịch ARM**: `arm-none-eabi-gcc` (phiên bản khuyến nghị: GCC 5.4.1 hoặc các bản 9.x/10.x hỗ trợ cờ kiến trúc `armv5te`).
* **Các công cụ build phụ trợ**: `make`, `python3`, `perl`.

### 4.2. Quy Trình Build
1. Biên dịch toàn bộ core và platform cho 5D2:
   ```bash
   cd platform/5D2.212
   make
   ```
2. Kết quả đầu ra:
   * `autoexec.bin`: File nhị phân nạp vào thẻ nhớ CF.
   * Thư mục `modules/`: Chứa các file `.mo` (đặt vào `ML/MODULES/` trên thẻ nhớ).

### 4.3. Kiểm Thử An Toàn Bằng QEMU (Không Sợ Hỏng Máy Thật)
* Dùng repository `qemu-eos` để giả lập vi xử lý DIGIC 4 của 5D2.
* Nạp bản dump ROM của 5D2 (ROM0 / ROM1) vào QEMU để debug toàn bộ luồng boot và thực thi lệnh của `autoexec.bin` trước khi sao chép vào thẻ CF vật lý.
