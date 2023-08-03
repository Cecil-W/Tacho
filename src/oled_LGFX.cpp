#include "oled_LGFX.h"

LGFX::LGFX(void) {
    // configuring the bus
    auto bus_cfg = _bus_instance.config();
    //defaults, taken from lovyanGFX examples/HowToUse/2_user_settings
    bus_cfg.i2c_port    = 0;
    bus_cfg.freq_write  = 400000;
    bus_cfg.freq_read   = 400000;
    bus_cfg.pin_sda     = 26; // GPIO pins 14 & 12 seem to be the default i2c pins on the esp32
    bus_cfg.pin_scl     = 25;
    bus_cfg.i2c_addr    = 0x3C;

    _bus_instance.config(bus_cfg);
    _panel_instance.setBus(&_bus_instance);


    // configuring the panel
    auto panel_cfg = _panel_instance.config();

    panel_cfg.pin_cs           =    -1;  // CSが接続されているピン番号   (-1 = disable)
    panel_cfg.pin_rst          =    -1;  // RSTが接続されているピン番号  (-1 = disable)
    panel_cfg.pin_busy         =    -1;  // BUSYが接続されているピン番号 (-1 = disable)

    // ※ 以下の設定値はパネル毎に一般的な初期値が設定されていますので、不明な項目はコメントアウトして試してみてください。

    panel_cfg.panel_width      =   128;  // 実際に表示可能な幅
    panel_cfg.panel_height     =   64;  // 実際に表示可能な高さ
    panel_cfg.offset_x         =     0;  // パネルのX方向オフセット量
    panel_cfg.offset_y         =     0;  // パネルのY方向オフセット量
    panel_cfg.offset_rotation  =     0;  // 回転方向の値のオフセット 0~7 (4~7は上下反転)
    panel_cfg.dummy_read_pixel =     8;  // ピクセル読出し前のダミーリードのビット数
    panel_cfg.dummy_read_bits  =     1;  // ピクセル以外のデータ読出し前のダミーリードのビット数
    panel_cfg.readable         =  true;  // データ読出しが可能な場合 trueに設定
    panel_cfg.invert           = false;  // パネルの明暗が反転してしまう場合 trueに設定
    panel_cfg.rgb_order        = false;  // パネルの赤と青が入れ替わってしまう場合 trueに設定
    panel_cfg.dlen_16bit       = false;  // 16bitパラレルやSPIでデータ長を16bit単位で送信するパネルの場合 trueに設定
    panel_cfg.bus_shared       =  false;  // SDカードとバスを共有している場合 trueに設定(drawJpgFile等でバス制御を行います)

    // 以下はST7735やILI9163のようにピクセル数が可変のドライバで表示がずれる場合にのみ設定してください。
    //    panel_cfg.memory_width     =   240;  // ドライバICがサポートしている最大の幅
    //    panel_cfg.memory_height    =   320;  // ドライバICがサポートしている最大の高さ

    _panel_instance.config(panel_cfg);

    setPanel(&_panel_instance);
}
