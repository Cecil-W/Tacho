#include "LovyanGFX.hpp"

//copied from lovyanGFX examples/HowToUse/2_user_settings
class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_SH110x _panel_instance;
    lgfx::Bus_I2C _bus_instance;

    public:
    LGFX(void);
};