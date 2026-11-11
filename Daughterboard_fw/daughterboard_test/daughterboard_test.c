#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/i2c.h"
#include "hardware/uart.h"

#define I2C1_PORT i2c1
#define I2C1_SDA 18
#define I2C1_SCL 19
#define OLED_ADDR 0x3C
#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define LED_PIN 25
#define ADC0_GPIO 26
#define ADC1_GPIO 27
#define ADC_REFERENCE_MV 3300
#define ADC_MAX_VALUE 4095
#define SENSE_RESISTOR_OHMS 1000

#define UART_ID uart1
#define BAUD_RATE 115200
#define UART_TX_PIN 4
#define UART_RX_PIN 5

static uint8_t framebuffer[OLED_WIDTH * OLED_HEIGHT / 8];

static const uint8_t digit_font[10][5] = {
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, // 0
    {0x00, 0x42, 0x7F, 0x40, 0x00}, // 1
    {0x42, 0x61, 0x51, 0x49, 0x46}, // 2
    {0x21, 0x41, 0x45, 0x4B, 0x31}, // 3
    {0x18, 0x14, 0x12, 0x7F, 0x10}, // 4
    {0x27, 0x45, 0x45, 0x45, 0x39}, // 5
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, // 6
    {0x01, 0x71, 0x09, 0x05, 0x03}, // 7
    {0x36, 0x49, 0x49, 0x49, 0x36}, // 8
    {0x06, 0x49, 0x49, 0x29, 0x1E}, // 9
};

static const uint8_t symbol_font[6][5] = {
    {0x08, 0x08, 0x08, 0x08, 0x08}, // -
    {0x00, 0x00, 0x60, 0x00, 0x00}, // .
    {0x08, 0x08, 0x3E, 0x08, 0x08}, // +
    {0x07, 0x1C, 0x60, 0x1C, 0x07}, // V
    {0x7C, 0x04, 0x18, 0x04, 0x78}, // m
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, // A
};

static bool oled_command(uint8_t command)
{
    uint8_t buffer[] = {0x00, command};
    return i2c_write_blocking(I2C1_PORT, OLED_ADDR, buffer, sizeof(buffer), false) == (int)sizeof(buffer);
}

static bool oled_init(void)
{
    static const uint8_t init_commands[] = {
        0xAE,       // Display off
        0xD5, 0x80, // Clock divide
        0xA8, 0x3F, // Multiplex ratio: 64 rows
        0xD3, 0x00, // Display offset
        0x40,       // Start line
        0x8D, 0x14, // Enable charge pump
        0x20, 0x00, // Horizontal addressing mode
        0xA1,       // Segment remap
        0xC8,       // COM scan direction
        0xDA, 0x12, // COM pin configuration
        0x81, 0xCF, // Contrast
        0xD9, 0xF1, // Pre-charge period
        0xDB, 0x40, // VCOMH deselect level
        0xA4,       // Display follows RAM
        0xA6,       // Normal display
        0xAF        // Display on
    };

    for (size_t i = 0; i < sizeof(init_commands); i++) {
        if (!oled_command(init_commands[i])) {
            return false;
        }
    }
    return true;
}

static void draw_pixel(uint8_t x, uint8_t y)
{
    if (x < OLED_WIDTH && y < OLED_HEIGHT) {
        framebuffer[(y / 8) * OLED_WIDTH + x] |= (uint8_t)(1U << (y % 8));
    }
}

static const uint8_t *get_glyph(char character)
{
    if (character >= '0' && character <= '9') {
        return digit_font[character - '0'];
    }

    uint8_t symbol = character == '-' ? 0
                     : character == '.' ? 1
                     : character == '+' ? 2
                     : character == 'V' ? 3
                     : character == 'm' ? 4
                                        : 5;
    return symbol_font[symbol];
}

static void draw_measurement(int32_t value_ten_thousandths, const char *unit, uint8_t y)
{
    uint32_t magnitude = (uint32_t)(value_ten_thousandths < 0
                                        ? -value_ten_thousandths
                                        : value_ten_thousandths);
    char text[12];
    snprintf(text, sizeof(text), "%c%lu.%04lu%s",
             value_ten_thousandths < 0 ? '-' : '+',
             (unsigned long)(magnitude / 10000),
             (unsigned long)(magnitude % 10000),
             unit);
    size_t text_length = 0;
    while (text[text_length] != '\0') {
        text_length++;
    }
    uint8_t x = (uint8_t)((OLED_WIDTH - (text_length * 12 - 2)) / 2);

    for (size_t character = 0; text[character] != '\0'; character++) {
        const uint8_t *glyph = get_glyph(text[character]);
        for (uint8_t column = 0; column < 5; column++) {
            for (uint8_t row = 0; row < 7; row++) {
                if (glyph[column] & (1U << row)) {
                    for (uint8_t dx = 0; dx < 2; dx++) {
                        for (uint8_t dy = 0; dy < 2; dy++) {
                            draw_pixel(x + (uint8_t)(character * 12) + column * 2 + dx,
                                       y + row * 2 + dy);
                        }
                    }
                }
            }
        }
    }
}

static bool oled_show_framebuffer(void)
{
    uint8_t buffer[OLED_WIDTH + 1];
    const uint8_t column_commands[] = {0x21, 0x00, OLED_WIDTH - 1};
    const uint8_t page_commands[] = {0x22, 0x00, OLED_HEIGHT / 8 - 1};

    for (size_t i = 0; i < sizeof(column_commands); i++) {
        if (!oled_command(column_commands[i])) {
            return false;
        }
    }
    for (size_t i = 0; i < sizeof(page_commands); i++) {
        if (!oled_command(page_commands[i])) {
            return false;
        }
    }

    buffer[0] = 0x40;
    for (size_t offset = 0; offset < sizeof(framebuffer); offset += OLED_WIDTH) {
        for (size_t i = 0; i < OLED_WIDTH; i++) {
            buffer[i + 1] = framebuffer[offset + i];
        }
        if (i2c_write_blocking(I2C1_PORT, OLED_ADDR, buffer, sizeof(buffer), false) != (int)sizeof(buffer)) {
            return false;
        }
    }
    return true;
}

int main(void)
{
    stdio_init_all();

    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

    i2c_init(I2C1_PORT, 400 * 1000);
    gpio_set_function(I2C1_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C1_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C1_SDA);
    gpio_pull_up(I2C1_SCL);

    uart_init(UART_ID, BAUD_RATE);
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
    uart_puts(UART_ID, "ADC voltage difference display started\n");

    adc_init();
    adc_gpio_init(ADC0_GPIO);
    adc_gpio_init(ADC1_GPIO);

    if (!oled_init()) {
        printf("OLED initialization failed; check I2C wiring and address 0x%02X\n", OLED_ADDR);
        while (true) {
            gpio_xor_mask(1u << LED_PIN);
            sleep_ms(250);
        }
    }

    while (true) {
        adc_select_input(0);
        sleep_us(5);
        uint16_t adc0_value = adc_read();

        adc_select_input(1);
        sleep_us(5);
        uint16_t adc1_value = adc_read();

        int32_t adc_difference = (int32_t)adc0_value - (int32_t)adc1_value;
        int32_t scaled_difference = adc_difference * ADC_REFERENCE_MV * 10;
        int32_t difference_ten_thousandths_volts =
            (scaled_difference + (scaled_difference >= 0 ? ADC_MAX_VALUE / 2 : -ADC_MAX_VALUE / 2)) /
            ADC_MAX_VALUE;

        for (size_t i = 0; i < sizeof(framebuffer); i++) {
            framebuffer[i] = 0;
        }
        int32_t current_ten_thousandths_milliamps =
            difference_ten_thousandths_volts * 1000 / SENSE_RESISTOR_OHMS;
        draw_measurement(difference_ten_thousandths_volts, "V", 0);
        draw_measurement(current_ten_thousandths_milliamps, "mA", 18);

        if (!oled_show_framebuffer()) {
            printf("OLED update failed; check I2C wiring and address 0x%02X\n", OLED_ADDR);
            while (true) {
                gpio_xor_mask(1u << LED_PIN);
                sleep_ms(250);
            }
        }
        gpio_xor_mask(1u << LED_PIN);
        sleep_ms(1000);
    }
}
