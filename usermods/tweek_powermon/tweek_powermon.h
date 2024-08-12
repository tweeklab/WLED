#pragma once

#include "wled.h"
#include <Wire.h>

// Status flags
#define FAN_STATUS_FAN_PWM_CLOSED_LOOP (1 << 0)
#define FAN_STATUS_FAN_ROTOR_LOCKED (1 << 1)
#define FAN_STATUS_BME280_UNRELIABLE (1 << 1)

#define POWER_BOARD_I2C_ADDRESS 0x50
#define POWER_BOARD_DEVICE_ID 0x45
#define FAN_BOARD_I2C_ADDRESS 0x56
#define FAN_BOARD_DEVICE_ID 0xba

#define NUM_FAN_CURVE_SLOTS 5

typedef union
{
  uint16_t value;
  uint8_t b1;
  uint8_t b2;
} short_i2c_data_t;

// class name. Use something descriptive and leave the ": public Usermod" part :)
class TweekPowerMon : public Usermod
{

private:
  // Private class members. You can declare variables and functions only accessible to your usermod here
  bool initDone = false;
  unsigned long lastLoopTime = 0;
  unsigned long lastDevicePokeTime = 0;

  // Reportable status
  float vinValue = 0.0;
  uint16_t fan_rpm = 0;
  float temp = 0.0;
  float dewpoint = 0.0;
  uint8_t pgoodValue = 0;
  uint8_t enValue = 0;
  bool power_board_found = false;
  bool fan_board_found = false;
  bool power_board_data_good = false;
  bool fan_rpm_data_good = false;
  bool fan_rotor_locked = false;
  bool fan_temp_data_good = false;
  bool fan_dewpoint_data_good = false;

  // Configs
  bool enabled = true;
  bool cutoff_on_no_temp_data = true;
  uint8_t cutoff_temp = 100;
  std::vector<uint8_t> fancurve_temps = {30, 40, 50, 60, 70};
  std::vector<uint8_t> fancurve_speeds = {20, 40, 70, 90, 100};

  static const char _name[];
  static const char _enabled[];
  static const char _cutoff_on_no_temp[];
  static const char _cutoff_temp[];

public:
  // methods called by WLED (can be inlined as they are called only once but if you call them explicitly define them out of class)

  bool check_power_board()
  {
    byte id_byte = 0;
    // Read ID byte and confirm it's correct
    Wire.beginTransmission(POWER_BOARD_I2C_ADDRESS);
    Wire.write(0x00);
    if (Wire.endTransmission() != 0)
    {
      return false;
    }
    Wire.requestFrom(POWER_BOARD_I2C_ADDRESS, 1);
    if (Wire.available() == 1)
    {
      id_byte = Wire.read();
    }
    if (id_byte != POWER_BOARD_DEVICE_ID)
    {
      return false;
    }
    return true;
  }

  bool check_fan_board()
  {
    byte id_byte = 0;
    // Read ID byte and confirm it's correct
    Wire.beginTransmission(FAN_BOARD_I2C_ADDRESS);
    Wire.write(0x00);
    if (Wire.endTransmission() != 0)
    {
      DEBUG_PRINTLN(F("TweekPowerMon check_fan_board: endTransmission failed"));
      return false;
    }
    Wire.requestFrom(FAN_BOARD_I2C_ADDRESS, 1);
    if (Wire.available() == 1)
    {
      id_byte = Wire.read();
    }
    if (id_byte != FAN_BOARD_DEVICE_ID)
    {
      DEBUG_PRINTLN(F("TweekPowerMon check_fan_board: read failed"));
      return false;
    }
    return true;
  }

  bool configure_fan_board(const std::vector<uint8_t> &temps, const std::vector<uint8_t> &speeds)
  {
    Wire.beginTransmission(FAN_BOARD_I2C_ADDRESS);
    Wire.write(0x12);
    for (uint8_t temp : temps)
    {
      Wire.write(temp);
    }
    if (Wire.endTransmission() != 0)
    {
      DEBUG_PRINTLN(F("TweekPowerMon check_fan_board: endTransmission 1 failed"));
      return false;
    }

    Wire.beginTransmission(FAN_BOARD_I2C_ADDRESS);
    Wire.write(0x13);
    for (uint8_t speed : speeds)
    {
      Wire.write(speed);
    }
    if (Wire.endTransmission() != 0)
    {
      DEBUG_PRINTLN(F("TweekPowerMon check_fan_board: endTransmission 2 failed"));
      return false;
    }

    return true;
  }

  bool query_power_board()
  {
    if (!power_board_found)
    {
      return false;
    }
    Wire.beginTransmission(POWER_BOARD_I2C_ADDRESS);
    if (Wire.write((uint8_t)0xff) != 1)
    {
      return false;
    }
    else if (Wire.endTransmission() != 0)
    {
      return false;
    }
    else
    {
      Wire.requestFrom(POWER_BOARD_I2C_ADDRESS, 4);
      if (Wire.available() == 4)
      {
        byte byte1 = Wire.read();
        byte byte2 = Wire.read();
        vinValue = (float)byte1 + (float)byte2 / 100;
        pgoodValue = Wire.read();
        enValue = Wire.read();
      }
      else
      {
        return false;
      }
    }
    return true;
  }

  bool query_fan_board_status_data(uint8_t *status)
  {
    if (!fan_board_found)
    {
      return false;
    }
    // Get temperature
    Wire.beginTransmission(FAN_BOARD_I2C_ADDRESS);
    if (Wire.write((uint8_t)0x10) != 1)
    {
      return false;
    }
    if (Wire.endTransmission() != 0)
    {
      return false;
    }
    else
    {
      Wire.requestFrom(FAN_BOARD_I2C_ADDRESS, 1);
      if (Wire.available() == 1)
      {
        *status = Wire.read();
      }
      else
      {
        return false;
      }
    }
    return true;
  }

  bool query_fan_rpm_data()
  {
    if (!fan_board_found)
    {
      return false;
    }

    // Get fan speed
    Wire.beginTransmission(FAN_BOARD_I2C_ADDRESS);
    if (Wire.write((uint8_t)0x23) != 1)
    {
      return false;
    }
    if (Wire.endTransmission() != 0)
    {
      return false;
    }
    else
    {
      Wire.requestFrom(FAN_BOARD_I2C_ADDRESS, 2);
      if (Wire.available() == 2)
      {
        fan_rpm = Wire.read();
        fan_rpm = (fan_rpm << 8) | Wire.read();
      }
      else
      {
        return false;
      }
    }
    return true;
  }

  bool query_fan_temp_data()
  {
    if (!fan_board_found)
    {
      return false;
    }

    // Get temperature
    Wire.beginTransmission(FAN_BOARD_I2C_ADDRESS);
    if (Wire.write((uint8_t)0x20) != 1)
    {
      return false;
    }
    if (Wire.endTransmission() != 0)
    {
      return false;
    }
    else
    {
      Wire.requestFrom(FAN_BOARD_I2C_ADDRESS, 2);
      if (Wire.available() == 2)
      {
        uint16_t temp_tmp;
        temp_tmp = Wire.read();
        temp_tmp = (temp_tmp << 8) | Wire.read();
        temp = ((float)temp_tmp) / 100;
      }
      else
      {
        return false;
      }
    }
    return true;
  }

  bool query_fan_dewpoint_data()
  {
    if (!fan_board_found)
    {
      return false;
    }

    // Get dewpoint
    Wire.beginTransmission(FAN_BOARD_I2C_ADDRESS);
    if (Wire.write((uint8_t)0x22) != 1)
    {
      return false;
    }
    if (Wire.endTransmission() != 0)
    {
      return false;
    }
    else
    {
      Wire.requestFrom(FAN_BOARD_I2C_ADDRESS, 2);
      if (Wire.available() == 2)
      {
        uint16_t dewpoint_tmp;
        dewpoint_tmp = Wire.read();
        dewpoint_tmp = (dewpoint_tmp << 8) | Wire.read();
        dewpoint = ((float)dewpoint_tmp) / 100;
      }
      else
      {
        return false;
      }
    }
    return true;
  }

  /*
   * setup() is called once at boot. WiFi is not yet connected at this point.
   * readFromConfig() is called prior to setup()
   * You can use it to initialize variables, sensors or similar.
   */
  void setup()
  {
    if (i2c_scl < 0 || i2c_sda < 0)
    {
      enabled = false;
      return;
    }

    if (!enabled)
    {
      return;
    }

    power_board_found = check_power_board();
    if (!power_board_found)
      DEBUG_PRINTLN(F("TweekPowerMon check_power_board() = false!"));
    fan_board_found = check_fan_board();
    if (!fan_board_found)
      DEBUG_PRINTLN(F("TweekPowerMon check_fan_board() = false!"));
    fan_board_found = configure_fan_board(fancurve_temps, fancurve_speeds);
    if (!fan_board_found)
      DEBUG_PRINTLN(F("TweekPowerMon configure_fan_board() = false!"));

    initDone = true;
  }

  /*
   * connected() is called every time the WiFi is (re)connected
   * Use it to initialize network interfaces
   */
  void connected()
  {
    // Serial.println("Connected to WiFi!");
  }

  /*
   * loop() is called continuously. Here you can check for events, read sensors, etc.
   *
   * Tips:
   * 1. You can use "if (WLED_CONNECTED)" to check for a successful network connection.
   *    Additionally, "if (WLED_MQTT_CONNECTED)" is available to check for a connection to an MQTT broker.
   *
   * 2. Try to avoid using the delay() function. NEVER use delays longer than 10 milliseconds.
   *    Instead, use a timer check as shown here.
   */
  void loop()
  {
    uint8_t status;
    // do your magic here
    if (!enabled)
    {
      // Prevent weirdness in the scenario where we start enabled,
      // disable, millis() rolls over, and then re-enable.
      lastLoopTime = 0;
      lastDevicePokeTime = 0;
      return;
    }
    if (millis() - lastLoopTime > 3000)
    {
      power_board_data_good = query_power_board();

      if (!query_fan_board_status_data(&status))
      {
        fan_rpm_data_good = false;
        fan_temp_data_good = false;
      }
      else
      {
        // Temp
        if (status & FAN_STATUS_BME280_UNRELIABLE)
        {
          fan_temp_data_good = false;
        }
        else
        {
          fan_temp_data_good = query_fan_temp_data();
          fan_dewpoint_data_good = query_fan_dewpoint_data();
        }

        // Fan
        fan_rpm_data_good = true;
        if (status & FAN_STATUS_FAN_ROTOR_LOCKED)
        {
          fan_rotor_locked = true;
        }
        else
        {
          fan_rotor_locked = false;
          fan_rpm_data_good = query_fan_rpm_data();
        }

        if (!fan_temp_data_good && cutoff_on_no_temp_data)
        {
          if (bri)
          {
            toggleOnOff();
            stateUpdated(CALL_MODE_DIRECT_CHANGE);
          }
        }

        if (fan_temp_data_good && (temp >= cutoff_temp))
        {
          if (bri)
          {
            toggleOnOff();
            stateUpdated(CALL_MODE_DIRECT_CHANGE);
          }
        }
      }

      lastLoopTime = millis();

      // Check if we have an offline device and re-poke everything
      if (millis() - lastDevicePokeTime > 10000) {
        if (!power_board_found || !fan_board_found) {
          DEBUG_PRINTLN(F("TweekPowerMon Bad device state, re-running setup..."));
          setup();
        }
      }
    }
  }

  /*
   * addToJsonInfo() can be used to add custom entries to the /json/info part of the JSON API.
   * Creating an "u" object allows you to add custom key/value pairs to the Info section of the WLED web UI.
   * Below it is shown how this could be used for e.g. a light sensor
   */
  void addToJsonInfo(JsonObject &root)
  {
    if (!enabled)
    {
      return;
    }
    // if "u" object does not exist yet wee need to create it
    JsonObject user = root["u"];
    if (user.isNull())
      user = root.createNestedObject("u");
    // These show up in the JSON API
    JsonObject sensor = root[F("sensor")];
    if (sensor.isNull())
      sensor = root.createNestedObject(F("sensor"));

    JsonArray vin_text = user.createNestedArray(FPSTR("Vin"));
    JsonObject vin_text_sensor = sensor.createNestedObject(FPSTR("led_voltage"));
    if (power_board_data_good)
    {
      // Vin
      vin_text_sensor["status"] = "ok";
      vin_text_sensor["value"] = vinValue;
      vin_text_sensor["unit"] = "V";
      vin_text.add(vinValue);
      vin_text.add(FPSTR("V"));

      JsonArray pgood_text;
      // PG1
      pgood_text = user.createNestedArray(FPSTR("Output 1 State"));
      if (!(enValue & 1 << 0))
      {
        pgood_text.add(FPSTR("OFF"));
      }
      else if (!(pgoodValue & 1 << 0))
      {
        pgood_text.add(FPSTR("FAIL"));
      }
      else
      {
        pgood_text.add(FPSTR("OK"));
      }
      // PG2
      pgood_text = user.createNestedArray(FPSTR("Output 2 State"));
      if (!(enValue & 1 << 1))
      {
        pgood_text.add(FPSTR("OFF"));
      }
      else if (!(pgoodValue & 1 << 1))
      {
        pgood_text.add(FPSTR("FAIL"));
      }
      else
      {
        pgood_text.add(FPSTR("OK"));
      }
      // PG3
      pgood_text = user.createNestedArray(FPSTR("Output 3 State"));
      if (!(enValue & 1 << 2))
      {
        pgood_text.add(FPSTR("OFF"));
      }
      else if (!(pgoodValue & 1 << 2))
      {
        pgood_text.add(FPSTR("FAIL"));
      }
      else
      {
        pgood_text.add(FPSTR("OK"));
      }
      // PG4
      pgood_text = user.createNestedArray(FPSTR("Output 4 State"));
      if (!(enValue & 1 << 3))
      {
        pgood_text.add(FPSTR("OFF"));
      }
      else if (!(pgoodValue & 1 << 3))
      {
        pgood_text.add(FPSTR("FAIL"));
      }
      else
      {
        pgood_text.add(FPSTR("OK"));
      }
    }
    else
    {
      vin_text.add(FPSTR("No Data!"));
      vin_text_sensor["status"] = "read_fail";
    }

    JsonArray fan_rpm_text;
    JsonObject fan_rpm_sensor;
    JsonArray temp_text;
    JsonObject temp_sensor;
    JsonArray dewpoint_text;
    JsonObject dewpoint_sensor;
    fan_rpm_text = user.createNestedArray(FPSTR("Fan Speed"));
    fan_rpm_sensor = sensor.createNestedObject(FPSTR("fan_speed"));
    temp_text = user.createNestedArray(FPSTR("Temperature"));
    temp_sensor = sensor.createNestedObject(FPSTR("temperature"));
    dewpoint_text = user.createNestedArray(FPSTR("Dewpoint"));
    dewpoint_sensor = sensor.createNestedObject(FPSTR("dewpoint"));
    if (fan_rpm_data_good)
    {
      // Fan RPM
      if (fan_rotor_locked)
      {
        fan_rpm_text.add(FPSTR("LOCKED"));
        fan_rpm_sensor["status"] = FPSTR("rotor_locked");
      }
      else
      {
        fan_rpm_text.add(fan_rpm);
        fan_rpm_text.add(FPSTR(" RPM"));
        fan_rpm_sensor["status"] = FPSTR("ok");
        fan_rpm_sensor["value"] = fan_rpm;
        fan_rpm_sensor["unit"] = FPSTR("RPM");
      }
    }
    else
    {
      fan_rpm_text.add(FPSTR("No Data!"));
      fan_rpm_sensor["status"] = FPSTR("read_error");
    }

    if (fan_temp_data_good)
    {
      // Temperature
      temp_text.add(temp);
      temp_text.add(FPSTR("°C"));
      temp_sensor["status"] = "ok";
      temp_sensor["value"] = temp;
      temp_sensor["unit"] = FPSTR("C");
    }
    else
    {
      temp_text.add(FPSTR("No Data!"));
      temp_sensor["status"] = "read_error";
    }

    if (fan_dewpoint_data_good)
    {
      // Dewpoint
      dewpoint_text.add(dewpoint);
      dewpoint_text.add(FPSTR("°C"));
      dewpoint_sensor["status"] = "ok";
      dewpoint_sensor["value"] = dewpoint;
      dewpoint_sensor["unit"] = FPSTR("C");
    }
    else
    {
      dewpoint_text.add(FPSTR("No Data!"));
      dewpoint_sensor["status"] = "read_error";
    }
  }

  /**
   * addToConfig() (called from set.cpp) stores persistent properties to cfg.json
   */
  void addToConfig(JsonObject &root)
  {
    JsonObject top = root.createNestedObject(FPSTR(_name)); // usermodname

    top[FPSTR(_enabled)] = enabled;
    top[FPSTR(_cutoff_on_no_temp)] = cutoff_on_no_temp_data;
    top[FPSTR(_cutoff_temp)] = cutoff_temp;

    JsonObject fancurve_ = top.createNestedObject("fancurve");
    char fancurve_label[8];
    uint8_t prev_value = 0;
    for (int j = 0; j < NUM_FAN_CURVE_SLOTS; j++)
    {
      snprintf(fancurve_label, sizeof(fancurve_label), "temp %d", j);
      if (fancurve_temps[j] >= prev_value)
      {
        fancurve_[fancurve_label] = fancurve_temps[j];
      }
      else
      {
        fancurve_[fancurve_label] = prev_value;
      }
      prev_value = fancurve_[fancurve_label];
    }
    prev_value = 0;
    for (int j = 0; j < NUM_FAN_CURVE_SLOTS; j++)
    {
      snprintf(fancurve_label, sizeof(fancurve_label), "speed %d", j);
      if (fancurve_speeds[j] >= prev_value)
      {
        fancurve_[fancurve_label] = fancurve_speeds[j];
      }
      else
      {
        fancurve_[fancurve_label] = prev_value;
      }
      prev_value = fancurve_[fancurve_label];
    }
    DEBUG_PRINTLN(F("TweekPowerMon config saved."));
  }

  /**
   * readFromConfig() is called before setup() to populate properties from values stored in cfg.json
   *
   * The function should return true if configuration was successfully loaded or false if there was no configuration.
   */
  bool readFromConfig(JsonObject &root)
  {
    JsonObject top = root[FPSTR(_name)];
    DEBUG_PRINTLN(F("TweekPowerMon reading config"));
    if (top.isNull())
    {
      DEBUG_PRINTLN(F(": No config found. (Using defaults.)"));
      return false;
    }

    // Always read the same way regardless of fresh start or not
    char fancurve_label[8];
    for (int j = 0; j < NUM_FAN_CURVE_SLOTS; j++)
    {
      snprintf(fancurve_label, sizeof(fancurve_label), "temp %d", j);
      getJsonValue(top["fancurve"][fancurve_label], fancurve_temps[j]);
    }
    for (int j = 0; j < NUM_FAN_CURVE_SLOTS; j++)
    {
      snprintf(fancurve_label, sizeof(fancurve_label), "speed %d", j);
      getJsonValue(top["fancurve"][fancurve_label], fancurve_speeds[j]);
    }

    getJsonValue(top[FPSTR(_enabled)], enabled);
    getJsonValue(top[FPSTR(_cutoff_on_no_temp)], cutoff_on_no_temp_data);
    getJsonValue(top[FPSTR(_cutoff_temp)], cutoff_temp);

    if (!initDone)
    {
      // first run: reading from cfg.json
      DEBUG_PRINTLN(F(" config loaded."));
    }
    else
    {
      // Config update in UI
      DEBUG_PRINTLN(F(" config (re)loaded."));
      setup();
    }

    // some condition here to make sure the config is right
    return true;
  }

  /*
   * appendConfigData() is called when user enters usermod settings page
   * it may add additional metadata for certain entry fields (adding drop down is possible)
   * be careful not to add too much as oappend() buffer is limited to 3k
   */
  // void appendConfigData()
  // {
  //   oappend(SET_F("addInfo('")); oappend(String(FPSTR(_name)).c_str()); oappend(SET_F(":fancurve")); oappend(SET_F("',1,'Here is your stuff');"));
  // }

  /**
   * onStateChanged() is used to detect WLED state change
   * @mode parameter is CALL_MODE_... parameter used for notifications
   */
  void onStateChange(uint8_t mode)
  {
    // do something if WLED state changed (color, brightness, effect, preset, etc)
  }

  /*
   * getId() allows you to optionally give your V2 usermod an unique ID (please define it in const.h!).
   * This could be used in the future for the system to determine whether your usermod is installed.
   */
  uint16_t getId()
  {
    return USERMOD_ID_TWEEK_POWERMON;
  }

  // More methods can be added in the future, this example will then be extended.
  // Your usermod will remain compatible as it does not need to implement all methods from the Usermod base class!
};

const char TweekPowerMon::_name[] PROGMEM = "TweekPowerMon";
const char TweekPowerMon::_enabled[] PROGMEM = "enabled";
const char TweekPowerMon::_cutoff_on_no_temp[] PROGMEM = "cutoff_on_no_temp_data";
const char TweekPowerMon::_cutoff_temp[] PROGMEM = "cutoff_temperature";
