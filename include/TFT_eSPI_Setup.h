// Конфігурація TFT_eSPI для LilyGO TTGO T-Display V1.1
// (еквівалент Setup25_TTGO_T_Display з репозиторію TFT_eSPI)
// Підключається через build_flags: -D USER_SETUP_LOADED=1 -include include/TFT_eSPI_Setup.h
#pragma once

// Плата без тачскріна — глушимо #warning про TOUCH_CS усередині TFT_eSPI
#define DISABLE_ALL_LIBRARY_WARNINGS

#define ST7789_DRIVER

#define TFT_WIDTH  135
#define TFT_HEIGHT 240

#define CGRAM_OFFSET      // зсув пам'яті контролера для панелі 135x240

#define TFT_MISO -1
#define TFT_MOSI 19
#define TFT_SCLK 18
#define TFT_CS    5
#define TFT_DC   16
#define TFT_RST  23

#define TFT_BL            4
#define TFT_BACKLIGHT_ON  HIGH

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF

#define SMOOTH_FONT

#define SPI_FREQUENCY       40000000
#define SPI_READ_FREQUENCY   6000000
