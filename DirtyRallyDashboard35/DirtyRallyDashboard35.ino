#include <WiFi.h>
#include "AsyncUDP.h"
#include <LovyanGFX.hpp>
#include <CYD-ESP32-3248S035R.h>
//自定义字体尺寸h170w140
#include "Gear_170.h"


CYD_ESP32_3248S035R tft;

// ================== 颜色定义 ==================
#define COLOR_BG TFT_BLACK
#define COLOR_PANEL TFT_BLACK   // 深灰蓝背景0x20E7
#define COLOR_BORDER TFT_WHITE  // 边框
#define COLOR_TEXT TFT_WHITE
#define COLOR_LABEL TFT_WHITE  // 标签
#define COLOR_GREEN TFT_GREEN
#define COLOR_RED TFT_RED
#define COLOR_YELLOW TFT_YELLOW
#define COLOR_BLUE TFT_BLUE
#define COLOR_DARKBLUE 0xc
#define COLOR_ORANGE 0xFD20
#define COLOR_CYAN TFT_CYAN
#define COLOR_TC 0x5ff
#define COLOR_ABS 0xff09
#define COLOR_BB TFT_RED
#define COLOR_MAP TFT_GREEN
#define COLOR_FUEL TFT_WHITE

// ================== 显示用的数据变量 ==================
float speed = 0;  // 速度 km/h
int gear = 1;
int rpm_percent = 0;  // 转速条 0~100
int engine_rpm = 0;
const float rpm_min_ratio = 0.9;  // 转速条最低点按怠速乘以此比例

int brake_FL = 150, brake_FR = 155, brake_RL = 120, brake_RR = 125;
int susp_FL = -18, susp_FR = -18, susp_RL = -18, susp_RR = -18;

long totalms = 0;  // 毫秒时间
int hour = 0, m = 0, s = 0, ms = 0;

float dist_cur = 0.0;
float dist_total = 0.0;
float distProgress = 0.0;  // 赛段完成百分比 0.00~100.00

float fuel = 18.0;
int laps = 1;
int tc = 0, abs_val = 0;
float bb = 72.2;
int map_val = 1;

// 屏幕刷新计时
unsigned long lastUpdate = 0;
const unsigned long UPDATE_INTERVAL_MS = 30;

// 每秒串口打印计时
unsigned long lastPrintMs = 0;
const unsigned long PRINT_INTERVAL_MS = 1000;

// ===== Wi-Fi 信息 =====
//使用SmartConfig 不需要定义了

// ===== DiRT Rally 2.0 遥测配置 =====
const uint16_t DIRT_PORT = 20778;
const size_t PACKET_SIZE = 264;  // 66 * 4 bytes
int UDP_TIMEOUT = 0;
const int UDP_TIMEOUT_MAX = 5;
char buf_localIP[19];

uint8_t latestPacket[PACKET_SIZE];
volatile bool newPacketReady = false;

// ===== DiRT Rally 2.0 数据包 =====
typedef struct __attribute__((packed)) {
  float total_time;
  float lap_time;
  float lap_distance;
  float total_distance;
  float position_x, position_y, position_z;
  float speed;
  float velocity_x, velocity_y, velocity_z;
  float left_dir_x, left_dir_y, left_dir_z;
  float forward_dir_x, forward_dir_y, forward_dir_z;
  float suspension_position_rl, suspension_position_rr;
  float suspension_position_fl, suspension_position_fr;
  float suspension_velocity_rl, suspension_velocity_rr;
  float suspension_velocity_fl, suspension_velocity_fr;
  float wheel_patch_speed_bl, wheel_patch_speed_br;
  float wheel_patch_speed_fl, wheel_patch_speed_fr;
  float throttle_input, steering_input, brake_input, clutch_input;
  float gear;
  float gforce_lateral, gforce_longitudinal;
  float lap;
  float engine_rate;
  float native_sli_support;
  float race_position;
  float kers_level, kers_level_max, drs;
  float traction_control, abs;
  float fuel_in_tank, fuel_capacity;
  float in_pits, race_sector;
  float sector_time_1, sector_time_2;
  float brake_temp_rl, brake_temp_rr, brake_temp_fl, brake_temp_fr;
  float tyre_pressure_rl, tyre_pressure_rr;
  float tyre_pressure_fl, tyre_pressure_fr;
  float laps_completed, total_laps, track_length;
  float last_lap_time;
  float max_rpm;
  float idle_rpm;
  float max_gears;
} DirtPacket;
static_assert(sizeof(DirtPacket) == 264, "DirtPacket must be 264 bytes");
DirtPacket gDirtPkt;

AsyncUDP udp;


void setup() {
  Serial.begin(115200);

  // 屏幕初始化
  tft.init();
  tft.setRotation(1);  // 480 x 320
                       // 定义并应用已有的校准数据
  uint16_t TouchCalibData[8] = { 3788, 3887, 3794, 148, 292, 3877, 261, 139 };
  tft.setTouchCalibrate(TouchCalibData);
  tft.setBrightness(255);
  tft.fillScreen(COLOR_BG);
  drawStaticUI();
  delay(100);
  tft.initLED();  // 熄灭背板 RGB 灯

  // Wi-Fi 先尝试自动连接，失败则进入 SmartConfig
  if (!AutoConfig()) {
    startSmartConfig();
  }
  // 防休眠提高udp接收速率 在 Wi-Fi 初始化并连接 AP 之前或之后调用
  WiFi.setSleep(false);

  // 显示 IP 地址（居中，覆盖在档位上,字体小不影响档位阅读）
  snprintf(buf_localIP, sizeof(buf_localIP), "IP:%s", WiFi.localIP().toString().c_str());
  tft.setTextSize(1);
  tft.setTextDatum(middle_center);
  //tft.fillRect(192, 100, 96, 30, COLOR_DARKBLUE);//不需要填充颜色 不影响IP地址显示 可以注释掉
  tft.setFont(&fonts::Font2);
  tft.setTextColor(COLOR_TEXT, COLOR_DARKBLUE);
  tft.drawString(buf_localIP, 240, 115);

  // UDP 监听
  if (udp.listen(DIRT_PORT)) {
    Serial.printf("UDP listening on port %u\n", DIRT_PORT);
    udp.onPacket([](AsyncUDPPacket packet) {
      if (packet.length() == PACKET_SIZE) {
        memcpy(latestPacket, packet.data(), PACKET_SIZE);
        newPacketReady = true;
      }
    });
  } else {
    Serial.println("UDP listen failed!");
  }
}


void loop() {
  unsigned long now = millis();

  // 每 1 秒检查一次
  if (now - lastPrintMs >= PRINT_INTERVAL_MS) {
    lastPrintMs = now;
    if (newPacketReady == false) {
      if (UDP_TIMEOUT < UDP_TIMEOUT_MAX) {
        UDP_TIMEOUT = UDP_TIMEOUT + 1;
      }
      if (UDP_TIMEOUT == UDP_TIMEOUT_MAX) {
        Serial.println("Waiting for DiRT Rally 2.0 data...");
        // 显示 IP
        snprintf(buf_localIP, sizeof(buf_localIP), "IP:%s", WiFi.localIP().toString().c_str());
        tft.setTextSize(1);
        tft.setTextDatum(middle_center);
        //tft.fillRect(192, 100, 96, 30, COLOR_DARKBLUE);//IP地址显示前不需要填充背景 可以注释掉
        tft.setFont(&fonts::Font2);
        tft.setTextColor(COLOR_TEXT, COLOR_DARKBLUE);
        tft.drawString(buf_localIP, 240, 115);  //覆盖在档位上是可以的 Font2的字体尺寸小 不影响档位阅读
      }
    }
  }

  // 每 30ms 刷新屏幕
  if (now - lastUpdate >= UPDATE_INTERVAL_MS) {
    lastUpdate = now;
    if (newPacketReady) {
      UDP_TIMEOUT = 0;
      newPacketReady = false;
      parseAndPrint(latestPacket);
    }
  }
}


// ===== 解析并显示 =====
void parseAndPrint(const uint8_t *data) {
  memcpy(&gDirtPkt, data, PACKET_SIZE);

  speed = gDirtPkt.speed * 3.6f;
  engine_rpm = (int)(gDirtPkt.engine_rate * 60.0f / (2.0f * PI));
  gear = (int)gDirtPkt.gear;

  if (gDirtPkt.max_rpm > 0 && gDirtPkt.idle_rpm > 0)
    rpm_percent = (int)((gDirtPkt.engine_rate - gDirtPkt.idle_rpm * rpm_min_ratio)
                        / (gDirtPkt.max_rpm - gDirtPkt.idle_rpm * rpm_min_ratio) * 100);
  else
    rpm_percent = 0;
  rpm_percent = constrain(rpm_percent, 0, 100);

  brake_FL = constrain((int)gDirtPkt.brake_temp_fl, -40, 990);
  brake_FR = constrain((int)gDirtPkt.brake_temp_fr, -40, 990);
  brake_RL = constrain((int)gDirtPkt.brake_temp_rl, -40, 990);
  brake_RR = constrain((int)gDirtPkt.brake_temp_rr, -40, 990);

  susp_FL = (int)gDirtPkt.suspension_position_fl;
  susp_FR = (int)gDirtPkt.suspension_position_fr;
  susp_RL = (int)gDirtPkt.suspension_position_rl;
  susp_RR = (int)gDirtPkt.suspension_position_rr;

  totalms = (long)(gDirtPkt.lap_time * 1000.0f + 0.5f);
  hour = totalms / 3600000L;
  m = (totalms / 60000L) % 60;
  s = (totalms / 1000L) % 60;
  ms = totalms % 1000;

  dist_cur = gDirtPkt.lap_distance;
  dist_total = gDirtPkt.track_length;
  distProgress = gDirtPkt.total_distance * 100.0f;

  updateUI();
}


// ================== 绘制静态界面 ==================
void drawStaticUI() {
  tft.setTextSize(1);

  // -------- 1. 左上面板：刹车温度 (0~150, 0~148) --------
  //x方向最多到170会与档位字符干涉 留12间距 170-12=158
  tft.fillRoundRect(0, 0, 158, 148, 4, COLOR_PANEL);
  tft.drawRoundRect(0, 0, 158, 148, 4, COLOR_BORDER);

  tft.setTextDatum(top_center);
  tft.setFont(&fonts::Font2);
  tft.setTextColor(COLOR_LABEL, COLOR_PANEL);
  tft.drawString("BRK TMP", 79, 4);

  tft.setTextDatum(top_left);
  tft.drawString("FL", 4, 24);
  tft.drawString("FR", 83, 24);
  tft.drawString("RL", 4, 84);
  tft.drawString("RR", 83, 84);

  tft.drawRect(44, 24, 24, 48, COLOR_LABEL);
  tft.drawRect(123, 24, 24, 48, COLOR_LABEL);
  tft.drawRect(44, 84, 24, 48, COLOR_LABEL);
  tft.drawRect(123, 84, 24, 48, COLOR_LABEL);

  // -------- 2. 左下面板：计时 (0~158, 155~222) --------
  tft.fillRoundRect(0, 155, 158, 67, 4, COLOR_PANEL);
  tft.drawRoundRect(0, 155, 158, 67, 4, COLOR_BORDER);

  tft.setTextDatum(top_center);
  tft.setTextColor(COLOR_LABEL, COLOR_PANEL);
  tft.drawString("TIME", 79, 157);

  // -------- 3. 右上面板：悬挂 (321~479, 0~148) --------
  tft.fillRoundRect(321, 0, 158, 148, 4, COLOR_PANEL);
  tft.drawRoundRect(321, 0, 158, 148, 4, COLOR_BORDER);

  tft.setTextDatum(top_center);
  tft.setTextColor(COLOR_LABEL, COLOR_PANEL);
  tft.drawString("SUSP", 400, 4);

  tft.setTextDatum(top_left);
  tft.drawString("FL", 325, 24);
  tft.drawRect(365, 24, 24, 48, COLOR_LABEL);
  tft.drawString("FR", 404, 24);
  tft.drawRect(444, 24, 24, 48, COLOR_LABEL);

  tft.drawString("RL", 325, 84);
  tft.drawRect(365, 84, 24, 48, COLOR_LABEL);
  tft.drawString("RR", 404, 84);
  tft.drawRect(444, 84, 24, 48, COLOR_LABEL);

  // -------- 4. 右下面板：里程 (352~478, 155~222) --------
  tft.fillRoundRect(321, 155, 158, 67, 4, COLOR_PANEL);
  tft.drawRoundRect(321, 155, 158, 67, 4, COLOR_BORDER);

  tft.setTextDatum(top_center);
  tft.setTextColor(COLOR_LABEL, COLOR_PANEL);
  tft.drawString("SESSION", 400, 157);

  // -------- 5. 转速条背景 (全宽480) --------
  tft.fillRect(1, 230, 478, 12, COLOR_PANEL);
  tft.drawRect(0, 229, 480, 14, COLOR_BORDER);

  // -------- 6. 底部信息面板 (全宽 480) --------
  tft.fillRoundRect(0, 248, 480, 72, 4, COLOR_PANEL);
  tft.drawRoundRect(0, 248, 480, 72, 4, COLOR_BORDER);

  tft.setFont(&fonts::DejaVu18);
  tft.setTextDatum(top_center);
  tft.setTextColor(COLOR_TC, COLOR_PANEL);
  tft.drawString("TC", 40, 254);
  tft.setTextColor(COLOR_ABS, COLOR_PANEL);
  tft.drawString("ABS", 120, 254);
  tft.setTextColor(COLOR_BB, COLOR_PANEL);
  tft.drawString("BB", 200, 254);
  tft.setTextColor(COLOR_MAP, COLOR_PANEL);
  tft.drawString("MAP", 280, 254);
  tft.setTextColor(COLOR_FUEL, COLOR_PANEL);
  tft.drawString("FUEL", 360, 254);
  tft.setTextColor(COLOR_TEXT, COLOR_PANEL);
  tft.drawString("LAPS", 440, 254);

  // -------- 7. 速度单位 (静态) --------
  tft.setFont(&fonts::Font0);
  tft.setTextDatum(middle_center);
  tft.setTextColor(COLOR_LABEL, COLOR_BG);
  tft.drawString("km/h", 240, 222);
}


// ================== 刷新动态界面 ==================
void updateUI() {
  char buf[24];

  // -------- 1. 刹车温度 --------
  tft.setTextSize(1);
  tft.setTextDatum(top_left);
  tft.setFont(&fonts::Font2);

  auto drawBrake = [](int x, int y, int barX, int barY, int temp) {
    // 数值区仅清除 x~x+34，避免擦掉右侧条
    tft.fillRect(x, y, 34, 18, COLOR_PANEL);
    //tft.fillRect(barX + 1, barY + 1, 22, 46, COLOR_PANEL);//新颜色块的尺寸相同 直接刷新覆盖即可 可以注释掉

    uint16_t textColor = COLOR_TEXT;
    uint16_t barColor = 0x5159;
    if (temp > 400) {
      textColor = 0x8935;
      barColor = 0x8935;
    } else if (temp > 300) {
      textColor = 0xe1c6;
      barColor = 0xe1c6;
    } else if (temp > 200) {
      textColor = 0xfc40;
      barColor = 0xfc40;
    } else if (temp > 120) {
      textColor = 0xd623;
      barColor = 0xd623;
    } else if (temp > 80) {
      barColor = 0x1db4;
    }

    tft.setTextColor(textColor, COLOR_PANEL);
    char b[10];
    sprintf(b, "%03d", temp);
    tft.drawString(b, x, y);

    tft.fillRect(barX + 1, barY + 1, 22, 46, barColor);
  };

  drawBrake(6, 48, 44, 24, brake_FL);
  drawBrake(85, 48, 123, 24, brake_FR);
  drawBrake(6, 108, 44, 84, brake_RL);
  drawBrake(85, 108, 123, 84, brake_RR);

  // -------- 2. 计时 --------
  tft.setFont(&fonts::DejaVu18);
  tft.setTextDatum(middle_center);
  tft.setTextColor(COLOR_CYAN, COLOR_PANEL);
  //tft.fillRect(4, 175, 122, 24, COLOR_PANEL);//计时字符的尺寸宽度相同 直接刷新覆盖即可 可以注释掉
  sprintf(buf, "%02d:%02d.%03d", m, s, ms);
  tft.setTextSize(1);
  tft.drawString(buf, 79, 187);
  tft.setTextSize(1);

  // -------- 3. 悬挂 --------
  tft.setTextDatum(top_left);
  tft.setFont(&fonts::Font2);
  auto drawSusp = [](int valX, int valY, int barX, int barY, int val) {
    tft.fillRect(valX, valY, 38, 18, COLOR_PANEL);
    tft.setTextColor(COLOR_TEXT, COLOR_PANEL);
    char b[10];
    sprintf(b, "%04d", val);
    tft.drawString(b, valX, valY);

    //tft.fillRect(barX + 1, barY + 1, 22, 46, COLOR_PANEL);//新颜色块的总尺寸相同 直接刷新覆盖即可 可以注释掉

    uint16_t barColor=0xc81f;
    if (val > 150) barColor = COLOR_RED;
    else if (val > 120) barColor = 0xfb20;
    else if (val > 90) barColor = 0xfe60;
    else if (val > 60) barColor = 0xffe0;
    else if (val > 30) barColor = 0xcfe0;
    else if (val > 0) barColor = 0x67e0;
    else if (val > -30) barColor = COLOR_GREEN;
    else if (val > -60) barColor = 0x7f3;
    else if (val > -90) barColor = 0x7ff;
    else if (val > -120) barColor = 0x33f;
    else if (val > -150) barColor = 0x1f;
    else if (val > -180) barColor = 0x981f;
    else barColor=0xc81f;
   

    int centerY = barY + 24;
    if (val > 0) {
      int h = constrain(map(val, 0, 190, 0, 23), 0, 23);
      tft.fillRect(barX + 1, centerY - h, 22, h, barColor);
      tft.fillRect(barX + 1, centerY - 23, 22, 23 - h, COLOR_PANEL);
      tft.fillRect(barX + 1, centerY, 22, 23, COLOR_PANEL);
    } else if (val < 0) {
      int h = constrain(map(abs(val), 0, 190, 0, 23), 0, 23);
      tft.fillRect(barX + 1, centerY, 22, h, barColor);
      tft.fillRect(barX + 1, centerY + h, 22, 23 - h, COLOR_PANEL);
      tft.fillRect(barX + 1, centerY - 23, 22, 23, COLOR_PANEL);
    }
  };

  drawSusp(327, 48, 365, 24, susp_FL);
  drawSusp(406, 48, 444, 24, susp_FR);
  drawSusp(327, 108, 365, 84, susp_RL);
  drawSusp(406, 108, 444, 84, susp_RR);


  // -------- 4. 里程百分比 --------
  tft.setFont(&fonts::DejaVu18);
  tft.setTextDatum(top_left);
  tft.setTextColor(TFT_WHITE, COLOR_PANEL);
  tft.fillRect(329, 173, 149, 18, COLOR_PANEL);
  sprintf(buf, "%.2f%/%.2fkm", dist_cur / 1000.0, dist_total / 1000.0);
  tft.drawString(buf, 329, 173);
  tft.setTextColor(COLOR_GREEN, COLOR_PANEL);
  sprintf(buf, "%06.2f%%", distProgress);
  tft.drawString(buf, 329, 200);
  int prograss_i = (int)(distProgress * 1.40f);
  tft.fillRect(329, 191, prograss_i, 4, COLOR_GREEN);
  tft.fillRect(329 + prograss_i, 191, 140 - prograss_i, 4, 0x18c);


  // -------- 5. 档位（放大到 170px 填满中央）--------
  //tft.fillRect(170, 0, 140, 170, COLOR_BG);       // 字符总宽度都是140 刷新时会覆盖旧的 不需要清档位区 可以注释掉
  tft.setTextDatum(BC_DATUM);
  tft.loadFont(Gear_170_data, lgfx::IFont::font_type_t::ft_lvgl);
  tft.setTextSize(1);  // h170px w140px

  if (gear == 0) {
    tft.setTextColor(COLOR_GREEN, COLOR_BG);
    tft.drawString("N", 240, 170);
  } else if (gear == -1) {
    tft.setTextColor(TFT_ORANGE, COLOR_BG);
    tft.drawString("R", 240, 170);
  } else if (gear > 0 && gear < 10) {
    tft.setTextColor(COLOR_YELLOW, COLOR_BG);
    sprintf(buf, "%d", gear);
    tft.drawString(buf, 240, 170);
  }

  tft.unloadFont();
  tft.setTextSize(1);

  // -------- 6. RPM 数值 --------
  tft.fillRect(170, 172, 140, 18, COLOR_BG);
  tft.setFont(&fonts::Font0);
  tft.setTextDatum(middle_center);
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  sprintf(buf, "%d RPM", engine_rpm);
  tft.drawString(buf, 240, 181);

  // -------- 7. 速度 --------
  tft.fillRect(170, 189, 140, 26, COLOR_BG);
  tft.setFont(&fonts::Font4);
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  sprintf(buf, "%.0f", speed);
  tft.drawString(buf, 240, 202);

  // -------- 8. 转速条（全宽 478）--------
  int w = (rpm_percent * 478) / 100;
  //tft.fillRect( 1, 230, 478, 12, COLOR_PANEL);//已改进填充代码 填充总尺寸相同 不需要预先刷背景色
  if (w > 0) {
    if (rpm_percent <= 60) {
      tft.fillRect(1, 230, w, 12, COLOR_GREEN);
      tft.fillRect(1 + w, 230, 478 - w, 12, COLOR_PANEL);  //补齐背景色
    } else if (rpm_percent <= 85) {
      tft.fillRect(1, 230, 286, 12, COLOR_GREEN);
      tft.fillRect(1 + 286, 230, w - 286, 12, COLOR_YELLOW);
      tft.fillRect(1 + w, 230, 478 - w, 12, COLOR_PANEL);
    } else {
      tft.fillRect(1, 230, 286, 12, COLOR_GREEN);
      tft.fillRect(1 + 286, 230, 406, 12, COLOR_YELLOW);
      tft.fillRect(1 + 406, 230, w - 406, 12, COLOR_RED);
      tft.fillRect(1 + w, 230, 478 - w, 12, COLOR_PANEL);
    }
  }

  // -------- 9. 底部信息 --------
  tft.setFont(&fonts::DejaVu18);//Font2字体太细 已换更粗的DejaVu18
  tft.setTextDatum(top_center);

  tft.fillRect(10, 282, 60, 24, COLOR_PANEL);
  tft.setTextColor(COLOR_TC, COLOR_PANEL);
  sprintf(buf, "%d", tc);
  tft.drawString(buf, 40, 282);

  tft.fillRect(90, 282, 60, 24, COLOR_PANEL);
  tft.setTextColor(COLOR_ABS, COLOR_PANEL);
  sprintf(buf, "%d", abs_val);
  tft.drawString(buf, 120, 282);

  tft.fillRect(170, 282, 60, 24, COLOR_PANEL);
  tft.setTextColor(COLOR_BB, COLOR_PANEL);
  sprintf(buf, "%.1f", bb);
  tft.drawString(buf, 200, 282);

  tft.fillRect(250, 282, 60, 24, COLOR_PANEL);
  tft.setTextColor(COLOR_MAP, COLOR_PANEL);
  sprintf(buf, "%d", map_val);
  tft.drawString(buf, 280, 282);

  tft.fillRect(330, 282, 60, 24, COLOR_PANEL);
  tft.setTextColor(COLOR_TEXT, COLOR_PANEL);
  sprintf(buf, "%.0f", fuel);
  tft.drawString(buf, 360, 282);

  tft.fillRect(410, 282, 60, 24, COLOR_PANEL);
  sprintf(buf, "%d", laps);
  tft.drawString(buf, 440, 282);
}




// 尝试自动连接（最多等待 10 秒）
bool AutoConfig() {
  WiFi.mode(WIFI_STA);
  WiFi.begin();  // 使用 NVS 中保存的凭证
  Serial.print("Connecting to WiFi");
  for (int i = 0; i < 20; i++) {  // 20 * 500ms = 10s 超时
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nWiFi connected. IP: " + WiFi.localIP().toString());
      return true;
    }
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nAuto connect timeout.");
  return false;
}

// 启动 SmartConfig 等待手机 App 配网
void startSmartConfig() {
  WiFi.mode(WIFI_STA);  // SmartConfig 需要在 STA 模式下运行
  Serial.println("\nWaiting for SmartConfig...");
  WiFi.beginSmartConfig();  // 启动 SmartConfig

  // 等待手机 App 发送配网信息（可加入超时，这里等待直到成功）
  while (!WiFi.smartConfigDone()) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nSmartConfig received.");
  Serial.printf("SSID: %s\n", WiFi.SSID().c_str());

  // 等待连接到目标 AP
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected. IP: " + WiFi.localIP().toString());
}