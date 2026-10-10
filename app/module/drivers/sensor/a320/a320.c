#define DT_DRV_COMPAT avago_a320

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/__assert.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

#include <stdlib.h>

#include "a320.h"

LOG_MODULE_REGISTER(A320, CONFIG_SENSOR_LOG_LEVEL);

static int a320_read_reg(const struct device *dev, uint8_t reg_addr) {
    const struct a320_config *cfg = dev->config;
    uint8_t value = 0;

    if (!i2c_reg_read_byte_dt(&cfg->bus, reg_addr, &value)) {
        return value;
    }

    LOG_ERR("failed to read 0x%x register", reg_addr);
    return -1;
}

// The sensor's own finger-navigation features (register 0x60, OFN_Engine)
// are all off after power-up, and this driver never used to switch any on.
// One of them is assert/de-assert: the sensor stops reporting motion once
// its shutter value shows the finger is leaving the surface, which is what
// stops the pointer jumping as a thumb is lifted. The datasheet's power-up
// sequence writes 0xE4 here (engine, speed switching, assert/de-assert,
// finger presence detect).
//
// Assert/de-assert works from fixed shutter thresholds, and on some
// trackpads those thresholds reject a bare finger altogether: the trackpad
// only responds through something more reflective. So the register is not
// fixed at build time. a320_set_ofn_engine() says what it should hold, from
// the keymap by way of trackpad_lift.c, and this driver keeps the sensor in
// step with that.
//
// The value is written even when it is 0. The sensor is not reset when the
// keyboard firmware restarts or is reflashed, so it can still hold what an
// earlier firmware wrote. The register is read back to confirm, retried
// soon if that fails, and checked again every so often in case the sensor
// has reset itself.
#define A320_ENGINE_RETRY_POLLS 16
#define A320_ENGINE_RECHECK_POLLS 1024

static atomic_t engine_wanted = ATOMIC_INIT(0);

void a320_set_ofn_engine(uint8_t value) { atomic_set(&engine_wanted, value); }

static int a320_write_reg(const struct device *dev, uint8_t reg_addr, uint8_t value) {
    const struct a320_config *cfg = dev->config;

    return i2c_reg_write_byte_dt(&cfg->bus, reg_addr, value);
}

static bool a320_apply_engine(const struct device *dev, uint8_t value) {
    if (a320_read_reg(dev, OFN_Engine) == value) {
        return true;
    }
    if (a320_write_reg(dev, OFN_Engine, value) != 0) {
        return false;
    }
    return a320_read_reg(dev, OFN_Engine) == value;
}

static void a320_sync_engine(const struct device *dev) {
    static uint32_t polls;
    static int engine_applied = -1; // not known until the first poll

    const int wanted = atomic_get(&engine_wanted);
    const uint32_t every =
        (wanted != engine_applied) ? A320_ENGINE_RETRY_POLLS : A320_ENGINE_RECHECK_POLLS;

    if ((polls++ % every) != 0) {
        return;
    }
    if (a320_apply_engine(dev, wanted)) {
        engine_applied = wanted;
    } else {
        engine_applied = -1;
        LOG_ERR("failed to set OFN_Engine");
    }
}

#if IS_ENABLED(CONFIG_INPUT_A320_DIAG)
// Diagnostics for tuning lift detection: what the sensor sees (surface
// quality, shutter, pixel levels) alongside the motion it reports. Lines are
// logged when there is motion, when the readings shift, and twice a second
// otherwise. Read them over USB logging (CONFIG_ZMK_USB_LOGGING).
static void a320_diag_dump_registers(const struct device *dev) {
    static const uint8_t regs[] = {
        Product_ID,      Revision_ID,     Inverse_Product_ID, Inverse_Revision_ID,
        Configuration_Bits, LED_Control,  IO_Mode,            Motion_Control,
        Shutter_Max_Hi,  Shutter_Max_Lo,  OFN_Engine,         0x61,
        OFN_Resolution,  OFN_Speed_Control, OFN_AD_CTRL,      OFN_AD_ATH_HIGH,
        OFN_AD_DTH_HIGH, OFN_AD_ATH_LOW,  OFN_AD_DTH_LOW,     OFN_Quantize_CTRL,
        OFN_XYQ_THRESH,  OFN_FPD_CTRL,    OFN_Orientation_CTRL,
    };

    for (int i = 0; i < ARRAY_SIZE(regs); i++) {
        LOG_INF("tpreg 0x%02x = 0x%02x", regs[i], a320_read_reg(dev, regs[i]) & 0xff);
    }
}

static void a320_diag_sample(const struct device *dev, uint8_t motion, int8_t dx, int8_t dy) {
    static uint32_t polls;
    static uint32_t last_log_ms;
    static int last_squal = -1;
    static int last_shutter = -1;

    // The register dump is repeated now and then so that it is not missed if
    // the log is opened late.
    if ((polls++ % 3000) == 0) {
        a320_diag_dump_registers(dev);
    }

    const int squal = a320_read_reg(dev, SQUAL) & 0xff;
    const int shutter =
        ((a320_read_reg(dev, Shutter_Upper) & 0xff) << 8) | (a320_read_reg(dev, Shutter_Lower) & 0xff);
    const int pix_max = a320_read_reg(dev, Maximum_Pixel) & 0xff;
    const int pix_avg = a320_read_reg(dev, Pixel_Sun) & 0xff;
    const int pix_min = a320_read_reg(dev, Minimum_Pixel) & 0xff;

    const uint32_t now = k_uptime_get_32();
    const bool moved = (motion & BIT_MOTION_MOT) != 0;
    const bool shifted = abs(squal - last_squal) > 3 || abs(shutter - last_shutter) > 16;

    if (moved || shifted || (now - last_log_ms) >= 500) {
        LOG_INF("tp t=%u mot=%02x dx=%d dy=%d sq=%d sh=%d px=%d/%d/%d", now, motion, dx, dy,
                squal, shutter, pix_max, pix_avg, pix_min);
        last_log_ms = now;
        last_squal = squal;
        last_shutter = shutter;
    }
}
#endif

static int a320_sample_fetch(const struct device *dev, enum sensor_channel chan) { return 0; }

static int a320_channel_get(const struct device *dev, enum sensor_channel chan,
                            struct sensor_value *val) {
    a320_sync_engine(dev);

    const uint8_t ifmotion = a320_read_reg(dev, Motion);
    const uint8_t ovflow = a320_read_reg(dev, Motion);
    if ((ifmotion & BIT_MOTION_MOT) && !(ovflow & BIT_MOTION_OVF)) {
        val->val1 = a320_read_reg(dev, Delta_X);
        val->val2 = a320_read_reg(dev, Delta_Y);
        LOG_DBG("you get the x value : %d", val->val1);
        LOG_DBG("you get the y value : %d", val->val2);
    } else{
        val->val1 = 0;
        val->val2 = 0;}
#if IS_ENABLED(CONFIG_INPUT_A320_DIAG)
    a320_diag_sample(dev, ifmotion, (int8_t)val->val1, (int8_t)val->val2);
#endif
    return -1;
}

static const struct sensor_driver_api a320_driver_api = {
    .sample_fetch = a320_sample_fetch,
    .channel_get = a320_channel_get,
};

static int a320_init(const struct device *dev) {

    const struct a320_config *cfg = dev->config;
    if (!device_is_ready(cfg->bus.bus)) {
        LOG_ERR("I2C bus %s is not ready!", cfg->bus.bus->name);
        return -EINVAL;
    }
    a320_read_reg(dev, Motion);
    a320_read_reg(dev, Motion);
    a320_read_reg(dev, Delta_X);
    a320_read_reg(dev, Delta_Y);
    LOG_DBG("A320 Init done, Ready to read data.");

    return 0;
}

#define A320_DEFINE(inst)                                                                          \
    struct a320_data a3200_data_##inst;                                                            \
    static const struct a320_config a3200_cfg_##inst = {.bus = I2C_DT_SPEC_INST_GET(inst)};        \
    DEVICE_DT_INST_DEFINE(inst, a320_init, NULL, &a3200_data_##inst, &a3200_cfg_##inst,            \
                          POST_KERNEL, 60, &a320_driver_api);

DT_INST_FOREACH_STATUS_OKAY(A320_DEFINE)
