#include <libopencm3/stm32/spi.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/rcc.h>

#include <string.h>

#include "pins.h"
#include "bl.h"

#define SPIx SPI2
#define SPI_AF GPIO_AF5
#define SPI_CLOCK RCC_SPI2

#define PIN_DISPLAY_CS PB_12
#define PIN_DISPLAY_SCK PB_13
#define PIN_DISPLAY_MOSI PB_15
#define PIN_DISPLAY_MISO PB_14
#define PIN_DISPLAY_BL PA_4
#define PIN_DISPLAY_DC PC_5
#define PIN_DISPLAY_RST PC_4
#define DISPLAY_WIDTH 160
#define DISPLAY_HEIGHT 128
#define DISPLAY_CFG0 0x00000080
#define DISPLAY_CFG1 0x000603
#define DISPLAY_CFG2 22

#define CFG(x) (x)

#define ST7735_NOP 0x00
#define ST7735_SWRESET 0x01
#define ST7735_RDDID 0x04
#define ST7735_RDDST 0x09

#define ST7735_SLPIN 0x10
#define ST7735_SLPOUT 0x11
#define ST7735_PTLON 0x12
#define ST7735_NORON 0x13

#define ST7735_INVOFF 0x20
#define ST7735_INVON 0x21
#define ST7735_DISPOFF 0x28
#define ST7735_DISPON 0x29
#define ST7735_CASET 0x2A
#define ST7735_RASET 0x2B
#define ST7735_RAMWR 0x2C
#define ST7735_RAMRD 0x2E

#define ST7735_PTLAR 0x30
#define ST7735_COLMOD 0x3A
#define ST7735_MADCTL 0x36

#define ST7735_FRMCTR1 0xB1
#define ST7735_FRMCTR2 0xB2
#define ST7735_FRMCTR3 0xB3
#define ST7735_INVCTR 0xB4
#define ST7735_DISSET5 0xB6

#define ST7735_PWCTR1 0xC0
#define ST7735_PWCTR2 0xC1
#define ST7735_PWCTR3 0xC2
#define ST7735_PWCTR4 0xC3
#define ST7735_PWCTR5 0xC4
#define ST7735_VMCTR1 0xC5

#define ST7735_RDID1 0xDA
#define ST7735_RDID2 0xDB
#define ST7735_RDID3 0xDC
#define ST7735_RDID4 0xDD

#define ST7735_PWCTR6 0xFC

#define ST7735_GMCTRP1 0xE0
#define ST7735_GMCTRN1 0xE1

void panic() {
    for(;;);
}

uint32_t pinport(int pin) {
    switch (pin >> 4) {
    case 0:
        return GPIOA;
    case 1:
        return GPIOB;
    case 2:
        return GPIOC;
    default:
        panic();
        return 0;
    }
}

uint16_t pinmask(int pin) {
    return 1 << (pin & 0xf);
}

void setup_pin(int pin, int mode) {
    uint32_t port = pinport(pin);
    uint32_t mask = pinmask(pin);
    gpio_mode_setup(port, mode, GPIO_PUPD_NONE, mask);
    if (pin != PIN_DISPLAY_MISO)
        gpio_set_output_options(port, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, mask);
    if (mode == GPIO_MODE_AF)
        gpio_set_af(port, SPI_AF, mask);
}

void pin_set(int pin, int v) {
    if (v) {
        gpio_set(pinport(pin), pinmask(pin));
    } else {
        gpio_clear(pinport(pin), pinmask(pin));
    }
}

#define DELAY 0x80

// clang-format off
static const uint8_t initCmds[] = {
    ST7735_SWRESET,   DELAY,  //  1: Software reset, 0 args, w/delay
      120,                    //     150 ms delay
    ST7735_SLPOUT ,   DELAY,  //  2: Out of sleep mode, 0 args, w/delay
      120,                    //     500 ms delay
    ST7735_INVOFF , 0      ,  // 13: Don't invert display, no args, no delay
    ST7735_COLMOD , 1      ,  // 15: set color mode, 1 arg, no delay:
      0x05,                  //     16-bit color
    ST7735_GMCTRP1, 16      , //  1: Magical unicorn dust, 16 args, no delay:
      0x02, 0x1c, 0x07, 0x12,
      0x37, 0x32, 0x29, 0x2d,
      0x29, 0x25, 0x2B, 0x39,
      0x00, 0x01, 0x03, 0x10,
    ST7735_GMCTRN1, 16      , //  2: Sparkles and rainbows, 16 args, no delay:
      0x03, 0x1d, 0x07, 0x06,
      0x2E, 0x2C, 0x29, 0x2D,
      0x2E, 0x2E, 0x37, 0x3F,
      0x00, 0x00, 0x02, 0x10,
    ST7735_NORON  ,    DELAY, //  3: Normal display on, no args, w/delay
      10,                     //     10 ms delay
    ST7735_DISPON ,    DELAY, //  4: Main screen turn on, no args w/delay
      10,
    0, 0 // END
};
// clang-format on

static uint8_t cmdBuf[20];

static void transfer(uint8_t *buf, int len) {
    while (len--) {
        spi_send(SPIx, *buf++);
    }
}

#define SET_DC(v) pin_set(CFG(PIN_DISPLAY_DC), v)
#define SET_CS(v) pin_set(CFG(PIN_DISPLAY_CS), v)

static void sendCmd(uint8_t *buf, int len) {
    // make sure cmd isn't on stack
    if (buf != cmdBuf)
        memcpy(cmdBuf, buf, len);
    buf = cmdBuf;

    SET_DC(0);
    SET_CS(0);

    transfer(buf, 1);

    SET_DC(1);

    len--;
    buf++;
    if (len > 0)
        transfer(buf, len);

    SET_CS(1);
}

static void sendCmdSeq(const uint8_t *buf) {
    while (*buf) {
        cmdBuf[0] = *buf++;
        int v = *buf++;
        int len = v & ~DELAY;
        // note that we have to copy to RAM
        memcpy(cmdBuf + 1, buf, len);
        sendCmd(cmdBuf, len + 1);
        buf += len;
        if (v & DELAY) {
            delay(*buf++);
        }
    }
}

static uint32_t palXOR;

static void setAddrWindow(int x, int y, int w, int h)
{
    uint8_t cmd0[] = {ST7735_RASET, 0, (uint8_t)x, 0, (uint8_t)(x + w - 1)};
    uint8_t cmd1[] = {ST7735_CASET, 0, (uint8_t)y, 0, (uint8_t)(y + h - 1)};
    sendCmd(cmd1, sizeof(cmd1));
    sendCmd(cmd0, sizeof(cmd0));
}

static void configure(uint8_t madctl, uint32_t frmctr1) {
    uint8_t cmd0[] = {ST7735_MADCTL, madctl};
    uint8_t cmd1[] = {ST7735_FRMCTR1, (uint8_t)(frmctr1 >> 16), (uint8_t)(frmctr1 >> 8), (uint8_t)frmctr1};
    sendCmd(cmd0, sizeof(cmd0));
    sendCmd(cmd1, cmd1[3] == 0xff ? 3 : 4);
}

void draw_stripes() {
    cmdBuf[0] = ST7735_RAMWR;
    sendCmd(cmdBuf, 1);

    SET_DC(1);
    SET_CS(0);

    for (int i = 0; i < DISPLAY_WIDTH; ++i) {
        uint16_t color = i * 2;
        for (int j = 0; j < DISPLAY_HEIGHT; ++j) {
            spi_send(SPIx, color >> 8);
            spi_send(SPIx, color & 0xff);
        }
    }

    SET_CS(1);
}

void screen_init() {
    rcc_periph_clock_enable(RCC_GPIOA);
    rcc_periph_clock_enable(RCC_GPIOB);
    rcc_periph_clock_enable(RCC_GPIOC);

    rcc_periph_clock_enable(SPI_CLOCK);

    setup_pin(CFG(PIN_DISPLAY_SCK), GPIO_MODE_AF);
    setup_pin(CFG(PIN_DISPLAY_MISO), GPIO_MODE_AF);
    setup_pin(CFG(PIN_DISPLAY_MOSI), GPIO_MODE_AF);
    setup_pin(CFG(PIN_DISPLAY_BL), GPIO_MODE_OUTPUT);
    setup_pin(CFG(PIN_DISPLAY_DC), GPIO_MODE_OUTPUT);
    setup_pin(CFG(PIN_DISPLAY_RST), GPIO_MODE_OUTPUT);
    setup_pin(CFG(PIN_DISPLAY_CS), GPIO_MODE_OUTPUT);

    spi_init_master(SPIx, SPI_CR1_BAUDRATE_FPCLK_DIV_8, 0, 0, 0, 0);

    SET_CS(1);
    SET_DC(1);

    delay(10); // TODO check if delay needed
    sendCmdSeq(initCmds);

    if (CFG(PIN_DISPLAY_RST) != -1) {
        pin_set(CFG(PIN_DISPLAY_RST), 0);
        delay(20);
        pin_set(CFG(PIN_DISPLAY_RST), 1);
        delay(20);
    }

    if (CFG(PIN_DISPLAY_BL) != -1) {
        pin_set(CFG(PIN_DISPLAY_BL), 1);
    }

    uint32_t cfg0 = CFG(DISPLAY_CFG0);
    uint32_t cfg2 = CFG(DISPLAY_CFG2);
    uint32_t frmctr1 = CFG(DISPLAY_CFG1);
    palXOR = (cfg0 & 0x1000000) ? 0xffffff : 0x000000;
    uint32_t madctl = cfg0 & 0xff;
    uint32_t offX = (cfg0 >> 8) & 0xff;
    uint32_t offY = (cfg0 >> 16) & 0xff;
    uint32_t freq = (cfg2 & 0xff);

    DMESG("configure screen: FRMCTR1=%p MADCTL=%p SPI at %dMHz", frmctr1, madctl, freq);
    configure(madctl, frmctr1);
    setAddrWindow(offX, offY, CFG(DISPLAY_WIDTH), CFG(DISPLAY_HEIGHT));

    draw_stripes();
}
