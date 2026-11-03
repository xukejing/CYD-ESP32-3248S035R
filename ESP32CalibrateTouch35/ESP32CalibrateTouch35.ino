#include <CYD-ESP32-3248S035R.h>

CYD_ESP32_3248S035R tft;

void setup() {
  Serial.begin(115200);
  delay(500);

  tft.init();
  tft.setRotation(0); 
    CYD_ESP32_3248S035R::initLED();
  tft.setBrightness(128);
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(1);
  tft.setTextDatum(middle_center);
   tft.setFont(&fonts::Font2);
  tft.setTextColor(TFT_WHITE, TFT_BLUE);
  tft.drawString("Hello", 240, 115);
  delay(1000);
  // --- 触摸校准开始 ---
  uint16_t calData[8]; // 用于存储校准数据的数组
  Serial.println("Please tap the arrows/points as instructed...");
  // 参数: 校准数据数组, 前景色, 背景色, 标记大小
  tft.calibrateTouch(calData, TFT_WHITE, TFT_BLACK, 15);
  Serial.println("Calibration done!");
  Serial.println(calData[0]);
  Serial.println(calData[1]);
  Serial.println(calData[2]);
  Serial.println(calData[3]);
  Serial.println(calData[4]);
  Serial.println(calData[5]);
  Serial.println(calData[6]);
  Serial.println(calData[7]);
  // --- 触摸校准结束 ---
    uint16_t TouchCalibData[8]; // 用于存储校准数据的数组
TouchCalibData[0]=3788;
TouchCalibData[1]=3887;
TouchCalibData[2]=3794;
TouchCalibData[3]=148;
TouchCalibData[4]=292;
TouchCalibData[5]=3877;
TouchCalibData[6]=261;
TouchCalibData[7]=139;



 // Serial.println("Ready.");
}

void loop() {

}
