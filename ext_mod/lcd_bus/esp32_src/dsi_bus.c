// Copyright (c) 2024 - 2025 Kevin G. Schlosser

// MIPI-DSI bus (video mode) on top of ESP-IDF's esp_lcd MIPI DSI driver.
//
// Unlike the SPI/I80/RGB buses this bus owns no pixel transfer of its own: the
// DPI panel driver in ESP-IDF allocates screen-sized frame buffers in PSRAM
// and its DMA scans them out to the panel continuously. The memoryviews
// handed out by allocate_framebuffer() are pointed at those frame buffers when
// the bus is initialized, so LVGL renders straight into them and a flush only
// has to write the CPU cache back and, when the flush is the last one of an
// update, swap the buffer being scanned out. Panel commands (DCS) travel over
// the same link through the DBI panel IO (tx_param / rx_param).

// local includes
#include "lcd_types.h"
#include "modlcd_bus.h"
#include "dsi_bus.h"

// micropython includes
#include "mphalport.h"
#include "py/obj.h"
#include "py/runtime.h"
#include "py/objarray.h"
#include "py/binary.h"

// stdlib includes
#include <string.h>

// esp-idf includes
#include "soc/soc_caps.h"

#if SOC_MIPI_DSI_SUPPORTED
    // esp-idf includes
    #include "esp_lcd_panel_ops.h"
    #include "esp_lcd_panel_interface.h"
    #include "esp_lcd_panel_io.h"
    #include "esp_heap_caps.h"
    #include "hal/lcd_types.h"
    #include "esp_lcd_mipi_dsi.h"
    #include "esp_ldo_regulator.h"


    // Prefix of ESP-IDF's private esp_lcd_dpi_panel_t (esp_lcd/dsi/esp_lcd_panel_dpi.c),
    // just enough to reach the frame buffers the DPI panel allocated.
    #ifndef DPI_PANEL_MAX_FB_NUM
        #define DPI_PANEL_MAX_FB_NUM 3  // esp_lcd/dsi/mipi_dsi_priv.h
    #endif

    typedef struct {
        esp_lcd_panel_t base;         // Base class of generic lcd panel
        esp_lcd_dsi_bus_handle_t bus; // DSI bus handle
        uint8_t virtual_channel;      // Virtual channel ID, index from 0
        uint8_t cur_fb_index;         // Current frame buffer index
        uint8_t num_fbs;              // Number of frame buffers
        uint8_t *fbs[DPI_PANEL_MAX_FB_NUM]; // Frame buffers
    } dpi_panel_t;


    mp_lcd_err_t dsi_del(mp_obj_t obj);
    mp_lcd_err_t dsi_init(mp_obj_t obj, uint16_t width, uint16_t height, uint8_t bpp, uint32_t buffer_size, bool rgb565_byte_swap, uint8_t cmd_bits, uint8_t param_bits);
    mp_lcd_err_t dsi_get_lane_count(mp_obj_t obj, uint8_t *lane_count);
    mp_lcd_err_t dsi_tx_color(mp_obj_t obj, int lcd_cmd, void *color, size_t color_size, int x_start, int y_start, int x_end, int y_end, uint8_t rotation, bool last_update);
    mp_obj_t dsi_allocate_framebuffer(mp_obj_t obj, uint32_t size, uint32_t caps);
    mp_obj_t dsi_free_framebuffer(mp_obj_t obj, mp_obj_t buf);


    static inline bool dsi_buf_in_fb(mp_lcd_dsi_bus_obj_t *self, const uint8_t *fb, const void *buf)
    {
        const uint8_t *p = (const uint8_t *)buf;
        return fb != NULL && p != NULL && p >= fb && p < fb + self->buffer_size;
    }


    // Runs from the DPI panel's end-of-frame interrupt: the DMA has just
    // restarted its scan-out from fbs[cur_fb_index]. Once that is the buffer
    // LVGL last flushed, the other buffer is free to be drawn into.
    static bool dsi_bus_refresh_done_cb(esp_lcd_panel_handle_t panel, esp_lcd_dpi_panel_event_data_t *edata, void *user_ctx)
    {
        LCD_UNUSED(edata);

        dpi_panel_t *dpi_panel = __containerof(panel, dpi_panel_t, base);
        mp_lcd_dsi_bus_obj_t *self = (mp_lcd_dsi_bus_obj_t *)user_ctx;

        if (!self->trans_done && dsi_buf_in_fb(self, dpi_panel->fbs[dpi_panel->cur_fb_index], self->transmitting_buf)) {
            if (self->callback != mp_const_none && mp_obj_is_callable(self->callback)) {
                cb_isr(self->callback);
            }
            self->trans_done = true;
        }

        return false;
    }


    static mp_obj_t mp_lcd_dsi_bus_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args)
    {
        enum {
            ARG_bus_id,
            ARG_data_lanes,
            ARG_freq,
            ARG_virtual_channel,
            ARG_hsync_front_porch,
            ARG_hsync_back_porch,
            ARG_hsync_pulse_width,
            ARG_vsync_front_porch,
            ARG_vsync_back_porch,
            ARG_vsync_pulse_width,
            ARG_dpi_clock_freq,
            ARG_phy_ldo_channel,
            ARG_phy_ldo_voltage_mv
        };

        const mp_arg_t make_new_args[] = {
            { MP_QSTR_bus_id,             MP_ARG_INT  | MP_ARG_KW_ONLY | MP_ARG_REQUIRED       },
            { MP_QSTR_data_lanes,         MP_ARG_INT  | MP_ARG_KW_ONLY | MP_ARG_REQUIRED       },
            { MP_QSTR_freq,               MP_ARG_INT  | MP_ARG_KW_ONLY | MP_ARG_REQUIRED       },  // lane bit rate, Mbps
            { MP_QSTR_virtual_channel,    MP_ARG_INT  | MP_ARG_KW_ONLY | MP_ARG_REQUIRED       },
            { MP_QSTR_hsync_front_porch,  MP_ARG_INT  | MP_ARG_KW_ONLY, { .u_int = 0       } },
            { MP_QSTR_hsync_back_porch,   MP_ARG_INT  | MP_ARG_KW_ONLY, { .u_int = 0       } },
            { MP_QSTR_hsync_pulse_width,  MP_ARG_INT  | MP_ARG_KW_ONLY, { .u_int = 1       } },
            { MP_QSTR_vsync_front_porch,  MP_ARG_INT  | MP_ARG_KW_ONLY, { .u_int = 0       } },
            { MP_QSTR_vsync_back_porch,   MP_ARG_INT  | MP_ARG_KW_ONLY, { .u_int = 0       } },
            { MP_QSTR_vsync_pulse_width,  MP_ARG_INT  | MP_ARG_KW_ONLY, { .u_int = 1       } },
            { MP_QSTR_dpi_clock_freq,     MP_ARG_INT  | MP_ARG_KW_ONLY, { .u_int = 0       } },  // DPI pixel clock, MHz; 0 = same number as freq
            { MP_QSTR_phy_ldo_channel,    MP_ARG_INT  | MP_ARG_KW_ONLY, { .u_int = -1      } },  // internal LDO channel powering the DSI PHY (ESP32-P4 boards: 3), -1 = not managed here
            { MP_QSTR_phy_ldo_voltage_mv, MP_ARG_INT  | MP_ARG_KW_ONLY, { .u_int = 2500    } }
        };

        mp_arg_val_t args[MP_ARRAY_SIZE(make_new_args)];
        mp_arg_parse_all_kw_array(
            n_args,
            n_kw,
            all_args,
            MP_ARRAY_SIZE(make_new_args),
            make_new_args,
            args
        );

        // create new object
        mp_lcd_dsi_bus_obj_t *self = m_new_obj(mp_lcd_dsi_bus_obj_t);
        memset(self, 0, sizeof(mp_lcd_dsi_bus_obj_t));
        self->base.type = &mp_lcd_dsi_bus_type;

        self->callback = mp_const_none;

        self->bus_config.bus_id = (int)args[ARG_bus_id].u_int;
        self->bus_config.num_data_lanes = (uint8_t)args[ARG_data_lanes].u_int;
        self->bus_config.lane_bit_rate_mbps = (uint32_t)args[ARG_freq].u_int;
        self->bus_config.phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT;

        self->panel_io_config.virtual_channel = (uint8_t)args[ARG_virtual_channel].u_int;

        self->panel_config.virtual_channel = (uint8_t)args[ARG_virtual_channel].u_int;
        self->panel_config.dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT;
        self->panel_config.dpi_clock_freq_mhz = args[ARG_dpi_clock_freq].u_int > 0 ? (uint32_t)args[ARG_dpi_clock_freq].u_int : (uint32_t)args[ARG_freq].u_int;

        self->panel_config.video_timing.hsync_back_porch = (uint32_t)args[ARG_hsync_back_porch].u_int;
        self->panel_config.video_timing.hsync_pulse_width = (uint32_t)args[ARG_hsync_pulse_width].u_int;
        self->panel_config.video_timing.hsync_front_porch = (uint32_t)args[ARG_hsync_front_porch].u_int;
        self->panel_config.video_timing.vsync_back_porch = (uint32_t)args[ARG_vsync_back_porch].u_int;
        self->panel_config.video_timing.vsync_pulse_width = (uint32_t)args[ARG_vsync_pulse_width].u_int;
        self->panel_config.video_timing.vsync_front_porch = (uint32_t)args[ARG_vsync_front_porch].u_int;

        self->panel_config.num_fbs = 0;

        self->phy_ldo_channel = (int)args[ARG_phy_ldo_channel].u_int;
        self->phy_ldo_voltage_mv = (int)args[ARG_phy_ldo_voltage_mv].u_int;

        LCD_DEBUG_PRINT("bus_id=%d\n", self->bus_config.bus_id)
        LCD_DEBUG_PRINT("num_data_lanes=%d\n", self->bus_config.num_data_lanes)
        LCD_DEBUG_PRINT("lane_bit_rate_mbps=%d\n",self->bus_config.lane_bit_rate_mbps)
        LCD_DEBUG_PRINT("virtual_channel=%d\n", self->panel_io_config.virtual_channel)
        LCD_DEBUG_PRINT("dpi_clock_freq_mhz=%d\n", self->panel_config.dpi_clock_freq_mhz)
        LCD_DEBUG_PRINT("hsync_front_porch=%d\n", self->panel_config.video_timing.hsync_front_porch)
        LCD_DEBUG_PRINT("hsync_back_porch=%d\n", self->panel_config.video_timing.hsync_back_porch)
        LCD_DEBUG_PRINT("hsync_pulse_width=%d\n", self->panel_config.video_timing.hsync_pulse_width)
        LCD_DEBUG_PRINT("vsync_front_porch=%d\n", self->panel_config.video_timing.vsync_front_porch)
        LCD_DEBUG_PRINT("vsync_back_porch=%d\n", self->panel_config.video_timing.vsync_back_porch)
        LCD_DEBUG_PRINT("vsync_pulse_width=%d\n", self->panel_config.video_timing.vsync_pulse_width)
        LCD_DEBUG_PRINT("phy_ldo_channel=%d\n", self->phy_ldo_channel)

        self->panel_io_handle.get_lane_count = &dsi_get_lane_count;
        self->panel_io_handle.del = &dsi_del;
        self->panel_io_handle.tx_color = &dsi_tx_color;
        self->panel_io_handle.allocate_framebuffer = &dsi_allocate_framebuffer;
        self->panel_io_handle.free_framebuffer = &dsi_free_framebuffer;
        self->panel_io_handle.init = &dsi_init;

        return MP_OBJ_FROM_PTR(self);
    }


    mp_lcd_err_t dsi_init(mp_obj_t obj, uint16_t width, uint16_t height, uint8_t bpp, uint32_t buffer_size, bool rgb565_byte_swap, uint8_t cmd_bits, uint8_t param_bits)
    {
        LCD_DEBUG_PRINT("dsi_init(self, width=%i, height=%i, bpp=%i, buffer_size=%lu, rgb565_byte_swap=%i, cmd_bits=%i, param_bits=%i)\n", width, height, bpp, buffer_size, (uint8_t)rgb565_byte_swap, cmd_bits, param_bits)

        LCD_UNUSED(buffer_size);

        mp_lcd_dsi_bus_obj_t *self = (mp_lcd_dsi_bus_obj_t *)obj;

        if (self->panel_handle != NULL) {
            mp_raise_msg(&mp_type_ValueError, MP_ERROR_TEXT("DSIBus is already initialized"));
            return LCD_ERR_INVALID_STATE;
        }

        switch(bpp) {
            case 16:
                self->panel_config.pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565;
                break;
            case 18:
                self->panel_config.pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB666;
                break;
            case 24:
                self->panel_config.pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB888;
                break;
            default:
                mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("unsupported bits per pixel (%d)"), bpp);
                return LCD_ERR_INVALID_ARG;
        }

        // The DPI panel refreshes straight from the frame buffers, so they have
        // to hold the whole screen; partial (strip) buffers cannot work here.
        uint32_t full_frame_size = (uint32_t)width * (uint32_t)height * (uint32_t)bpp / 8;

        if (self->view1 == NULL) {
            mp_raise_msg(&mp_type_ValueError, MP_ERROR_TEXT("DSIBus frame buffers must be created with allocate_framebuffer()"));
            return LCD_ERR_INVALID_ARG;
        }

        if (self->buffer_size != full_frame_size) {
            mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("DSIBus needs screen sized frame buffers (%lu bytes, got %lu)"), full_frame_size, self->buffer_size);
            return LCD_ERR_INVALID_ARG;
        }

        // LVGL renders in the frame buffers' own byte order; nothing here to swap.
        self->rgb565_byte_swap = false;

        self->panel_config.video_timing.h_size = (uint32_t)width;
        self->panel_config.video_timing.v_size = (uint32_t)height;

        self->panel_io_config.lcd_cmd_bits = (int)cmd_bits;
        self->panel_io_config.lcd_param_bits = (int)param_bits;

        LCD_DEBUG_PRINT("lcd_cmd_bits=%d\n", self->panel_io_config.lcd_cmd_bits)
        LCD_DEBUG_PRINT("lcd_param_bits=%d\n", self->panel_io_config.lcd_param_bits)
        LCD_DEBUG_PRINT("h_size=%d\n", self->panel_config.video_timing.h_size)
        LCD_DEBUG_PRINT("v_size=%d\n", self->panel_config.video_timing.v_size)
        LCD_DEBUG_PRINT("pixel_format=%d\n", self->panel_config.pixel_format)
        LCD_DEBUG_PRINT("num_fbs=%d\n", self->panel_config.num_fbs)

        esp_err_t ret;

        if (self->phy_ldo_channel >= 0 && self->phy_ldo_handle == NULL) {
            // The MIPI DSI PHY is powered by one of the ESP32-P4's internal
            // LDOs (channel 3 on the Espressif and Waveshare boards); without
            // it the bus never comes up.
            esp_ldo_channel_config_t ldo_cfg = {
                .chan_id = self->phy_ldo_channel,
                .voltage_mv = self->phy_ldo_voltage_mv,
            };
            ret = esp_ldo_acquire_channel(&ldo_cfg, &self->phy_ldo_handle);
            if (ret != 0) {
                mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("%d(esp_ldo_acquire_channel)"), ret);
                return ret;
            }
        }

        ret = esp_lcd_new_dsi_bus(&self->bus_config, &self->bus_handle);

        if (ret != 0) {
            mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("%d(esp_lcd_new_dsi_bus)"), ret);
            return ret;
        }

        // DCS commands and parameters go through the DBI panel IO
        ret = esp_lcd_new_panel_io_dbi(self->bus_handle, &self->panel_io_config, &self->panel_io_handle.panel_io);

        if (ret != 0) {
            mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("%d(esp_lcd_new_panel_io_dbi)"), ret);
            return ret;
        }

        // Creating the DPI panel allocates its frame buffers (in PSRAM); the
        // video stream itself is not started here but by the first tx_color,
        // so the display driver can configure the panel over DCS first, the
        // order Espressif's own MIPI panel drivers use.
        ret = esp_lcd_new_panel_dpi(self->bus_handle, &self->panel_config, &self->panel_handle);

        if (ret != 0) {
            mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("%d(esp_lcd_new_panel_dpi)"), ret);
            return ret;
        }

        esp_lcd_dpi_panel_event_callbacks_t callbacks = {
            .on_refresh_done = &dsi_bus_refresh_done_cb
        };

        ret = esp_lcd_dpi_panel_register_event_callbacks(self->panel_handle, &callbacks, self);

        if (ret != 0) {
            mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("%d(esp_lcd_dpi_panel_register_event_callbacks)"), ret);
            return ret;
        }

        // Point the buffers handed out by allocate_framebuffer() at the DPI
        // panel's frame buffers and drop the interim allocations.
        dpi_panel_t *dpi_panel = __containerof((esp_lcd_panel_t *)self->panel_handle, dpi_panel_t, base);

        heap_caps_free(self->view1->items);
        self->view1->items = (void *)dpi_panel->fbs[0];
        self->view1->len = self->buffer_size;

        if (self->view2 != NULL) {
            heap_caps_free(self->view2->items);
            self->view2->items = (void *)dpi_panel->fbs[1];
            self->view2->len = self->buffer_size;
        }

        LCD_DEBUG_PRINT("fb1=%p fb2=%p\n", self->view1->items, self->view2 != NULL ? self->view2->items : NULL)

        return LCD_OK;
    }


    mp_lcd_err_t dsi_del(mp_obj_t obj)
    {
        LCD_DEBUG_PRINT("dsi_del(self)\n")

        mp_lcd_dsi_bus_obj_t *self = (mp_lcd_dsi_bus_obj_t *)obj;
        mp_lcd_err_t ret;

        if (self->panel_handle != NULL) {
            ret = esp_lcd_panel_del(self->panel_handle);
            if (ret != 0) {
                mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("%d(esp_lcd_panel_del)"), ret);
                return ret;
            }
            self->panel_handle = NULL;
            self->panel_started = false;

            // the frame buffers went with the panel
            if (self->view1 != NULL) {
                self->view1->items = NULL;
                self->view1->len = 0;
                self->view1 = NULL;
            }
            if (self->view2 != NULL) {
                self->view2->items = NULL;
                self->view2->len = 0;
                self->view2 = NULL;
            }
        }

        if (self->panel_io_handle.panel_io != NULL) {
            ret = esp_lcd_panel_io_del(self->panel_io_handle.panel_io);
            if (ret != 0) {
                mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("%d(esp_lcd_panel_io_del)"), ret);
                return ret;
            }
            self->panel_io_handle.panel_io = NULL;
        }

        if (self->bus_handle != NULL) {
            ret = esp_lcd_del_dsi_bus(self->bus_handle);
            if (ret != 0) {
                mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("%d(esp_lcd_del_dsi_bus)"), ret);
                return ret;
            }
            self->bus_handle = NULL;
        }

        if (self->phy_ldo_handle != NULL) {
            esp_ldo_release_channel(self->phy_ldo_handle);
            self->phy_ldo_handle = NULL;
        }

        return LCD_OK;
    }


    mp_lcd_err_t dsi_get_lane_count(mp_obj_t obj, uint8_t *lane_count)
    {
        mp_lcd_dsi_bus_obj_t *self = (mp_lcd_dsi_bus_obj_t *)obj;
        *lane_count = (uint8_t)self->bus_config.num_data_lanes;

        LCD_DEBUG_PRINT("dsi_get_lane_count(self)-> %d\n", (uint8_t)self->bus_config.num_data_lanes)

        return LCD_OK;
    }


    mp_obj_t dsi_free_framebuffer(mp_obj_t obj, mp_obj_t buf)
    {
        mp_lcd_dsi_bus_obj_t *self = (mp_lcd_dsi_bus_obj_t *)obj;

        if (self->panel_handle != NULL) {
            // once initialized the buffers belong to the DPI panel (see dsi_del)
            mp_raise_msg(&mp_type_ValueError, MP_ERROR_TEXT("Unable to free buffer"));
            return mp_const_none;
        }

        mp_obj_array_t *array_buf = (mp_obj_array_t *)MP_OBJ_TO_PTR(buf);
        void *item_buf = array_buf->items;

        if (array_buf == self->view1) {
            heap_caps_free(item_buf);
            array_buf->items = NULL;
            array_buf->len = 0;
            self->view1 = self->view2;
            self->view2 = NULL;
            LCD_DEBUG_PRINT("dsi_free_framebuffer(self, buf=1)\n")
        } else if (array_buf == self->view2) {
            heap_caps_free(item_buf);
            array_buf->items = NULL;
            array_buf->len = 0;
            self->view2 = NULL;
            LCD_DEBUG_PRINT("dsi_free_framebuffer(self, buf=2)\n")
        } else {
            mp_raise_msg(&mp_type_MemoryError, MP_ERROR_TEXT("No matching buffer found"));
            return mp_const_none;
        }

        self->panel_config.num_fbs = self->view1 == NULL ? 0 : (self->view2 == NULL ? 1 : 2);

        return mp_const_none;
    }


    mp_obj_t dsi_allocate_framebuffer(mp_obj_t obj, uint32_t size, uint32_t caps)
    {
        LCD_DEBUG_PRINT("dsi_allocate_framebuffer(self, size=%lu, caps=%lu)\n", size, caps)

        mp_lcd_dsi_bus_obj_t *self = (mp_lcd_dsi_bus_obj_t *)obj;

        if (self->panel_handle != NULL) {
            mp_raise_msg(&mp_type_ValueError, MP_ERROR_TEXT("DSIBus is already initialized"));
            return mp_const_none;
        }

        if (self->view1 != NULL && self->view2 != NULL) {
            mp_raise_msg(&mp_type_MemoryError, MP_ERROR_TEXT("There is a maximum of 2 frame buffers allowed"));
            return mp_const_none;
        }

        if (self->view1 != NULL && self->buffer_size != size) {
            mp_raise_msg_varg(&mp_type_MemoryError, MP_ERROR_TEXT("Frame buffer sizes do not match (%lu)"), size);
            return mp_const_none;
        }

        // A real buffer for now, so the memoryview is always valid; once the
        // bus is initialized it is repointed at the frame buffer the DPI panel
        // driver allocated itself and this one is freed again (dsi_init).
        void *buf = heap_caps_calloc(1, size, caps);

        if (buf == NULL) {
            mp_raise_msg_varg(&mp_type_MemoryError, MP_ERROR_TEXT("Not enough memory available (%lu)"), size);
            return mp_const_none;
        }

        mp_obj_array_t *view = MP_OBJ_TO_PTR(mp_obj_new_memoryview(BYTEARRAY_TYPECODE, size, buf));
        view->typecode |= 0x80; // used to indicate writable buffer

        if (self->view1 == NULL) {
            self->buffer_size = size;
            self->buffer_flags = caps;
            self->view1 = view;
            self->panel_config.num_fbs = 1;
        } else {
            self->view2 = view;
            self->panel_config.num_fbs = 2;
        }

        return MP_OBJ_FROM_PTR(view);
    }


    mp_lcd_err_t dsi_tx_color(mp_obj_t obj, int lcd_cmd, void *color, size_t color_size, int x_start, int y_start, int x_end, int y_end, uint8_t rotation, bool last_update)
    {
        LCD_DEBUG_PRINT("dsi_tx_color(self, lcd_cmd=%d, color, color_size=%d, x_start=%d, y_start=%d, x_end=%d, y_end=%d, last_update=%d)\n", lcd_cmd, color_size, x_start, y_start, x_end, y_end, (int)last_update)

        LCD_UNUSED(lcd_cmd);
        LCD_UNUSED(color_size);
        LCD_UNUSED(rotation);

        mp_lcd_dsi_bus_obj_t *self = (mp_lcd_dsi_bus_obj_t *)obj;
        esp_err_t ret;

        if (self->panel_handle == NULL) {
            mp_raise_msg(&mp_type_ValueError, MP_ERROR_TEXT("DSIBus is not initialized"));
            return LCD_ERR_INVALID_STATE;
        }

        if (!self->panel_started) {
            // First flush: the panel has been configured by now, start the
            // DPI video stream.
            ret = esp_lcd_panel_init(self->panel_handle);

            if (ret != 0) {
                mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("%d(esp_lcd_panel_init)"), ret);
                return ret;
            }
            self->panel_started = true;
        }

        self->trans_done = false;
        self->transmitting_buf = NULL;

        // LVGL renders into the frame buffer itself, so for the DPI panel this
        // is a cache write-back of the touched lines plus, if the buffer is
        // not the one being scanned out, a swap at the next frame boundary.
        // LVGL areas are inclusive, esp_lcd_panel_draw_bitmap() wants
        // exclusive end coordinates.
        ret = esp_lcd_panel_draw_bitmap(
            self->panel_handle,
            x_start,
            y_start,
            x_end + 1,
            y_end + 1,
            color
        );

        if (ret != 0) {
            mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("%d(esp_lcd_panel_draw_bitmap)"), ret);
            return ret;
        }

        if (!last_update) {
            // One of several areas of the same update: the pixels are in the
            // frame buffer, LVGL can carry on with the next area right away.
            // Only the last area waits for the panel to pick the buffer up.
            self->trans_done = true;

            if (self->callback != mp_const_none && mp_obj_is_callable(self->callback)) {
                mp_call_function_n_kw(self->callback, 0, 0, NULL);
            }
        } else {
            // Completion is reported from the refresh-done interrupt
            // (dsi_bus_refresh_done_cb); with no callback registered the
            // generic tx_color method busy-waits on trans_done.
            self->transmitting_buf = color;
        }

        return LCD_OK;
    }


    MP_DEFINE_CONST_OBJ_TYPE(
        mp_lcd_dsi_bus_type,
        MP_QSTR_DSIBus,
        MP_TYPE_FLAG_NONE,
        make_new, mp_lcd_dsi_bus_make_new,
        locals_dict, (mp_obj_dict_t *)&mp_lcd_bus_locals_dict
    );

#endif /*SOC_MIPI_DSI_SUPPORTED*/
