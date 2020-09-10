/*--------------------------------------------------------------------
Copyright 2020 fukuen

Boot selector is free software: you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation, either version 3 of the
License, or (at your option) any later version.

This software is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with This software.  If not, see
<http://www.gnu.org/licenses/>.
--------------------------------------------------------------------*/

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <Wire.h>
#include <stdio.h>
#include "fpioa.h"
#include "utility/Button.h"
#include "w25qxx.h"

TFT_eSPI lcd;

#ifdef M5STICKV
#define AXP192_ADDR 0x34
#define PIN_SDA 29
#define PIN_SCL 28
#endif

#define WIDTH   280
#define HEIGHT  240
#define XOFFSET 40
#define YOFFSET 60

#define DEBOUNCE_MS 10
Button BtnA = Button(36, true, DEBOUNCE_MS);
Button BtnB = Button(37, true, DEBOUNCE_MS);
#define BOOT_CONFIG_ADDR        0x00004000  // main boot config sector in flash at 52K
#define BOOT_CONFIG_ITEMS       8           // number of handled config entries

int posCursor = 0;
int posLaunch = 0;
bool btnLongPressed = false;
uint8_t buff[4096];

typedef struct {
  uint32_t entry_id;
  uint32_t app_start;
  uint32_t app_size;
  uint32_t app_crc;
  char app_name[16];
} app_entry_t;

app_entry_t app_entry[8];

void spcDump2(char *id, int rc, uint8_t *data, int len) {
    int i;
    printf("[%s] = %d\n",id,rc);
    for(i=0;i<len;i++) {
      printf("%0x ",data[i]);
      if ( (i % 10) == 9) printf("\n");
    }
    printf("\n");
}

/*
 * Get uint32_t value from Flash
 * 8-bit Flash pionter is used,
 */
//-----------------------------------------
static uint32_t flash2uint32(uint32_t addr) {
    uint32_t val = buff[addr] << 24;
    val += buff[addr+1] << 16;
    val += buff[addr+2] << 8;
    val += buff[addr+3];
    return val;
}

bool checkKboot() {
  w25qxx_init(3, 0);

  uint8_t manuf_id = 0;
  uint8_t device_id = 0;
  w25qxx_read_id(&manuf_id, &device_id);
  printf("id: %u %u\n", manuf_id, device_id);

  int rc = w25qxx_read_data(0x1000, buff, 0x20, W25QXX_STANDARD);
//  spcDump2("dump", rc, buff, 32);

  if (buff[0] == 0x00 && buff[9] == 0x4b && buff[10] == 0x4b
    && buff[11] == 0x62 && buff[12] == 0x6f && buff[13] == 0x6f
    && buff[14] == 0x74) {
    return true;
  }
  return false;
}

void readEntry() {
  int rc = w25qxx_read_data(BOOT_CONFIG_ADDR, buff, BOOT_CONFIG_ITEMS * 0x20, W25QXX_STANDARD);
//  spcDump2("dump", rc, buff, BOOT_CONFIG_ITEMS * 0x20);
  for (int i = 0; i < BOOT_CONFIG_ITEMS; i++) {
    app_entry[i].entry_id = flash2uint32(i * 0x20);
    app_entry[i].app_start = flash2uint32(i * 0x20 + 4);
    app_entry[i].app_size = flash2uint32(i * 0x20 + 8);
    app_entry[i].app_crc = flash2uint32(i * 0x20 + 12);
    for (int j = 0; j < 16; j++) {
      app_entry[i].app_name[j] = buff[i * 0x20 + 16 + j];
    }
  }
}

void writeActive(int index, int active) {
  int rc = w25qxx_read_data(BOOT_CONFIG_ADDR + index * 0x20 + 3, buff, 1, W25QXX_STANDARD);
  if (active == 0) {
    buff[0] = buff[0] & 0xfe;
  } else {
    buff[0] = buff[0] | 1;
  }
  rc = w25qxx_write_data(BOOT_CONFIG_ADDR + index * 0x20 + 3, buff, 1);
}

void drawMenu() {
  lcd.fillScreen(TFT_BLACK);
  lcd.fillRect(0 + XOFFSET, YOFFSET, WIDTH, 8 + 4, TFT_CYAN);
  lcd.setCursor(1 + XOFFSET, 1 + YOFFSET);
  lcd.setTextSize(1);
  lcd.setTextColor(TFT_BLACK);
  lcd.println("Boot Selector v0.1");
  lcd.setCursor(XOFFSET, 8 + YOFFSET);
  lcd.println("");
  lcd.setTextColor(TFT_WHITE);
  for (int i = 0; i < BOOT_CONFIG_ITEMS; i++) {
    lcd.setCursor(XOFFSET, (i + 2) * 8 + YOFFSET);
    lcd.print(" ");
    if ((app_entry[i].entry_id & 1) == 1) {
      lcd.setTextColor(TFT_RED);
      lcd.print("*");
    } else {
      lcd.print(" ");
    }
    lcd.setTextColor(TFT_WHITE);
    lcd.print(i + 1);
    lcd.print(".");
    lcd.print(app_entry[i].app_name);
    lcd.println("");
  }
  lcd.println("");
  lcd.println("");
  lcd.setCursor(XOFFSET, 12 * 8 + YOFFSET);
  lcd.println("BtnA: Toggle active");
  lcd.setCursor(XOFFSET, 13 * 8 + YOFFSET);
  lcd.println("BtnB: Move cursor");
  lcd.setCursor(XOFFSET, 14 * 8 + YOFFSET);
  lcd.setTextColor(TFT_GREENYELLOW);
  lcd.print("Copyright 2020 fukuen");
}

void drawCursor() {
  lcd.fillRect(0 + XOFFSET, 8 * 2 + YOFFSET, 5, 8 * 9, TFT_BLACK);
  lcd.setCursor(XOFFSET, (posCursor + 2) * 8 + YOFFSET);
  lcd.setTextColor(TFT_YELLOW);
  lcd.setTextSize(1);
  lcd.print(">");
}

void toggleActive(int index) {
  if ((app_entry[index].entry_id & 1) == 1) {
    app_entry[index].entry_id = app_entry[index].entry_id & 0xfffffffe;
    writeActive(index, 0);
    lcd.setCursor(6 + XOFFSET, (index + 2) * 8 + YOFFSET);
    lcd.setTextColor(TFT_RED);
    lcd.setTextSize(1);
    lcd.print(" ");
    lcd.fillRect(6 + XOFFSET, (index + 2) * 8 + YOFFSET, 5, 8, TFT_BLACK);
  } else {
    app_entry[index].entry_id = app_entry[index].entry_id | 1;
    writeActive(index, 1);
    lcd.setCursor(6 + XOFFSET, (index + 2) * 8 + YOFFSET);
    lcd.setTextColor(TFT_RED);
    lcd.setTextSize(1);
    lcd.print("*");
  }
}

bool axp192_init() {
  Serial.printf("AXP192 init.\n");
  sysctl_set_power_mode(SYSCTL_POWER_BANK3,SYSCTL_POWER_V33);

  Wire.begin((uint8_t) PIN_SDA, (uint8_t) PIN_SCL, 400000);
  Wire.beginTransmission(AXP192_ADDR);
  int err = Wire.endTransmission();
  if (err) {
    Serial.printf("Power management ic not found.\n");
    return false;
  }
  Serial.printf("AXP192 found.\n");

  // Clear the interrupts
  Wire.beginTransmission(AXP192_ADDR);
  Wire.write(0x46);
  Wire.write(0xFF);
  Wire.endTransmission();
  Wire.beginTransmission(AXP192_ADDR);
  Wire.write(0x23);
  Wire.write(0x08); //K210_VCore(DCDC2) set to 0.9V
  Wire.endTransmission();
  Wire.beginTransmission(AXP192_ADDR);
  Wire.write(0x33);
  Wire.write(0xC1); //190mA Charging Current
  Wire.endTransmission();
  Wire.beginTransmission(AXP192_ADDR);
  Wire.write(0x36);
  Wire.write(0x6C); //4s shutdown
  Wire.endTransmission();
  Wire.beginTransmission(AXP192_ADDR);
  Wire.write(0x91);
  Wire.write(0xF0); //LCD Backlight: GPIO0 3.3V
  Wire.endTransmission();
  Wire.beginTransmission(AXP192_ADDR);
  Wire.write(0x90);
  Wire.write(0x02); //GPIO LDO mode
  Wire.endTransmission();
  Wire.beginTransmission(AXP192_ADDR);
  Wire.write(0x28);
  Wire.write(0xF0); //VDD2.8V net: LDO2 3.3V,  VDD 1.5V net: LDO3 1.8V
  Wire.endTransmission();
  Wire.beginTransmission(AXP192_ADDR);
  Wire.write(0x27);
  Wire.write(0x2C); //VDD1.8V net:  DC-DC3 1.8V
  Wire.endTransmission();
  Wire.beginTransmission(AXP192_ADDR);
  Wire.write(0x12);
  Wire.write(0xFF); //open all power and EXTEN
  Wire.endTransmission();
  Wire.beginTransmission(AXP192_ADDR);
  Wire.write(0x23);
  Wire.write(0x08); //VDD 0.9v net: DC-DC2 0.9V
  Wire.endTransmission();
  Wire.beginTransmission(AXP192_ADDR);
  Wire.write(0x31);
  Wire.write(0x03); //Cutoff voltage 3.2V
  Wire.endTransmission();
  Wire.beginTransmission(AXP192_ADDR);
  Wire.write(0x39);
  Wire.write(0xFC); //Turnoff Temp Protect (Sensor not exist!)
  Wire.endTransmission();

  fpioa_set_function(23, (fpioa_function_t)(FUNC_GPIOHS0 + 26));
  gpiohs_set_drive_mode(26, GPIO_DM_OUTPUT);
  gpiohs_set_pin(26, GPIO_PV_HIGH); //Disable VBUS As Input, BAT->5V Boost->VBUS->Charing Cycle

  msleep(20);
  return true;
}

void setup() {
  pll_init();
  uarths_init();
  plic_init();
  axp192_init();

  lcd.begin();
  lcd.setRotation(1);
  lcd.setTextFont(0);
  printf("start\n");

  checkKboot();
  readEntry();

  drawMenu();
  drawCursor();
}

void loop() {
  BtnA.read();
  BtnB.read();
  if (BtnA.wasPressed()) {
    toggleActive(posCursor);
  }
  if (BtnB.wasPressed()) {
      posCursor++;
      if (posCursor > BOOT_CONFIG_ITEMS - 1) {
        posCursor = 0;
      }
      drawCursor();
  }
}