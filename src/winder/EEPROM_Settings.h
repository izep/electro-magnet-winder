#ifndef EEPROM_SETTINGS_H
#define EEPROM_SETTINGS_H

#include <Arduino.h>
#include <EEPROM.h>

struct Settings {
  uint32_t magic; // 0x534B4950 (SKIP)
  int targetLayers;
  int spoolLengthMM;
  int gaugeIndex;
  int guideDir;
};

const uint32_t MAGIC_VAL = 0x534B4950;

class SettingsManager {
public:
  Settings current;

  void begin() {
    EEPROM.begin(512);
    load();
  }

  void load() {
    EEPROM.get(0, current);
    if (current.magic != MAGIC_VAL) {
      // Default settings
      current.magic = MAGIC_VAL;
      current.targetLayers = 10;
      current.spoolLengthMM = 50;
      current.gaugeIndex = 0;
      current.guideDir = -1;
      save();
    }
  }

  void save() {
    EEPROM.put(0, current);
    EEPROM.commit();
  }
};

extern SettingsManager settingsManager;

#endif
