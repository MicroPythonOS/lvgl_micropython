// Copyright (c) 2024 - 2025 Kevin G. Schlosser


#ifndef _ESP32_DSI_BUS_H_
    #define _ESP32_DSI_BUS_H_

    //local_includes
    #include "lcd_types.h"

    // micropython includes
    #include "mphalport.h"
    #include "py/obj.h"
    #include "py/objarray.h"

    // esp-idf includes
    #include "soc/soc_caps.h"


    #if SOC_MIPI_DSI_SUPPORTED
        // esp-idf includes
        #include "esp_lcd_panel_io.h"
        #include "esp_lcd_panel_interface.h"
        #include "esp_lcd_mipi_dsi.h"
        #include "esp_ldo_regulator.h"

        #if SOC_PPA_SUPPORTED
            #include "driver/ppa.h"
        #endif


        typedef struct _mp_lcd_dsi_bus_obj_t {
            // The first members mirror mp_lcd_bus_obj_t (lcd_types.h): the
            // generic bus methods cast to it.
            mp_obj_base_t base;

            mp_obj_t callback;

            mp_obj_array_t *view1;
            mp_obj_array_t *view2;

            uint32_t buffer_flags;

            bool trans_done;
            bool rgb565_byte_swap;

            lcd_panel_io_t panel_io_handle;

            esp_lcd_dbi_io_config_t panel_io_config;
            esp_lcd_dsi_bus_config_t bus_config;
            esp_lcd_dsi_bus_handle_t bus_handle;
            esp_lcd_panel_handle_t panel_handle;
            esp_lcd_dpi_panel_config_t panel_config;

            uint32_t buffer_size;
            void *transmitting_buf;
            bool panel_started;

            int phy_ldo_channel;
            int phy_ldo_voltage_mv;
            esp_ldo_channel_handle_t phy_ldo_handle;

            // rotation (0/90/180/270 degrees, LVGL's sense): LVGL renders the
            // rotated (logical) picture into its own buffers and every update
            // is rotated into the panel's frame buffers by the PPA
            int rotation;
            uint8_t bpp;
            uint32_t lvgl_width;
            uint32_t lvgl_height;
            uint8_t *panel_fbs[2];
            uint8_t back_fb_index;
            uint32_t fb_size_aligned;
        #if SOC_PPA_SUPPORTED
            ppa_client_handle_t ppa_client;
        #endif

        } mp_lcd_dsi_bus_obj_t;

        extern const mp_obj_type_t mp_lcd_dsi_bus_type;

    #endif /* SOC_MIPI_DSI_SUPPORTED */
#endif /* _ESP32_DSI_BUS_H_ */
