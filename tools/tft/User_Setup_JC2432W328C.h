// JC2432W328C factory capacitive User_Setup.h, maxpill/JC2432W328 @9712396.
// Pins/BGR/inversion OFF: factory demo; 40 MHz conservatively below its 55 MHz.
// Force-included for ALL translation units; never replace shared .deps config.
#pragma once
#define USER_SETUP_LOADED
#define ST7789_DRIVER
#define TFT_WIDTH 240
#define TFT_HEIGHT 320
#define TFT_INVERSION_OFF
#define TFT_RGB_ORDER TFT_BGR
#define TFT_MOSI 13
#define TFT_MISO 12
#define TFT_SCLK 14
#define TFT_CS 15
#define TFT_DC 2
#define TFT_RST -1
#define USE_HSPI_PORT
#define TFT_SPI_MODE SPI_MODE3
// Backlight27 belongs to display.cpp LEDC; no TFT_BL and no resistive TOUCH_CS.
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT
#define SPI_FREQUENCY 40000000
#define SPI_READ_FREQUENCY 20000000
