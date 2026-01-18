#include <Wire.h>
#include <U8g2lib.h>
#include <Adafruit_INA219.h>
#include <math.h>

// ===== I2C PINNIT (HW-364A) =====
#define I2C_SDA 14   // D5 (GPIO14)
#define I2C_SCL 12   // D6 (GPIO12)

// ===== OLED =====
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
// Jos joskus tarvitsee: U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// ===== INA219 =====
Adafruit_INA219 ina219;
bool inaOK = false;

// ===== Energia (Wh) =====
double energyWh = 0.0;
unsigned long lastMs = 0;

// ===== UI update =====
const unsigned long UI_MS = 250;   // 4 Hz
unsigned long lastUiMs = 0;

// ===== Smoothaus (EMA) =====
float v_f = 0.0f;
float i_f = 0.0f;

const float ALPHA_V = 0.20f;  // jännite: kohtuunopea
const float ALPHA_I = 0.08f;  // virta: smoothimpi

static float deadband(float x, float eps) {
  return (x > -eps && x < eps) ? 0.0f : x;
}

static float ema(float prev, float x, float alpha) {
  return prev + alpha * (x - prev);
}

static float quantize(float x, float step) {
  // step esim 0.1 => 1 desimaali; 0.01 => 2 desimaalia
  if (step <= 0) return x;
  return roundf(x / step) * step;
}

void drawUI(float v_disp, float i_disp, float p_disp, double wh, bool ok) {
  u8g2.clearBuffer();

  // ===== OTSIKKO (keltainen alue) =====
  // Parkside bold, PD charger pienempi samaan riviin
  u8g2.setFont(u8g2_font_6x13B_tr);
  u8g2.drawStr(0, 12, "Parkside");
  int x = u8g2.getStrWidth("PARKSIDE") + 4;

  u8g2.setFont(u8g2_font_6x12_tr);
  u8g2.drawStr(x, 12, "PD charger");

  if (!ok) {
    u8g2.drawStr(0, 34, "INA219 NOT FOUND");
    u8g2.drawStr(0, 48, "Check wiring/addr");
    u8g2.sendBuffer();
    return;
  }

  char buf[24];

  // ===== DATA (sininen alue) =====
  // V ja I isolla
  u8g2.setFont(u8g2_font_logisoso16_tr);

  snprintf(buf, sizeof(buf), "V %.1f", v_disp);
  u8g2.drawStr(0, 42, buf);

  snprintf(buf, sizeof(buf), "I %.2f", i_disp);
  u8g2.drawStr(72, 42, buf);

  // P ja E pienellä
  u8g2.setFont(u8g2_font_6x12_tr);

  snprintf(buf, sizeof(buf), "P %.1f W", p_disp);
  u8g2.drawStr(0, 62, buf);

  snprintf(buf, sizeof(buf), "E %.2f Wh", wh);
  u8g2.drawStr(64, 62, buf);

  u8g2.sendBuffer();
}

void setup() {
  Serial.begin(115200);
  delay(200);

  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000);

  u8g2.begin();

  inaOK = ina219.begin();
  if (inaOK) {
    // Jos mittaus alkaa seota isommilla virroilla -> kommentoi pois
    ina219.setCalibration_32V_2A();
  }

  energyWh = 0.0;
  lastMs = millis();
  lastUiMs = 0;

  // init filtterit järkevästi (ettei startissa hypi nollasta)
  v_f = 0.0f;
  i_f = 0.0f;
}

void loop() {
  unsigned long now = millis();

  float v_raw = 0.0f;
  float i_raw = 0.0f;

  if (inaOK) {
    // Lue INA219: bus + shunt => loadV, virta shuntista
    float busV      = ina219.getBusVoltage_V();
    float shuntmV   = ina219.getShuntVoltage_mV();
    float currentmA = ina219.getCurrent_mA();

    v_raw = busV + shuntmV / 1000.0f;
    i_raw = currentmA / 1000.0f;

    // pienet offsetit pois
    i_raw = deadband(i_raw, 0.01f);

    // EMA smoothaus
    v_f = ema(v_f, v_raw, ALPHA_V);
    i_f = ema(i_f, i_raw, ALPHA_I);

    // Teho suodatetuista
    float p_f = v_f * i_f;
    if (p_f < 0.0f) p_f = 0.0f;

    // Wh integraatio suodatetulla teholla
    unsigned long dt = now - lastMs;
    lastMs = now;
    energyWh += (double)p_f * (double)dt / 3600000.0;

    // Näyttöarvot pyöristetään (tappaa “heittelyn”)
    float v_disp = quantize(v_f, 0.1f);   // 0.1 V
    float i_disp = quantize(i_f, 0.02f);  // 0.01 A
    float p_disp = quantize(p_f, 0.1f);   // 0.1 W

    // UI päivitys
    if (now - lastUiMs >= UI_MS) {
      lastUiMs = now;
      drawUI(v_disp, i_disp, p_disp, energyWh, true);

      // Serial debug (voit poistaa)
      Serial.print("Vraw="); Serial.print(v_raw, 2);
      Serial.print(" Iraw="); Serial.print(i_raw, 3);
      Serial.print(" | Vf="); Serial.print(v_f, 2);
      Serial.print(" If="); Serial.print(i_f, 3);
      Serial.print(" Pf="); Serial.print(p_f, 2);
      Serial.print(" Wh="); Serial.println(energyWh, 4);
    }
  } else {
    // sensoria ei löydy
    lastMs = now;
    if (now - lastUiMs >= UI_MS) {
      lastUiMs = now;
      drawUI(0, 0, 0, energyWh, false);
    }
  }
}
