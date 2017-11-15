#include "Arduino.h"

//8 bytes in storage
typedef struct {
  byte alarmId; // id for the alarm from the timer alarm class
  byte id; // my id for the alarm in order to know where it lives in memory
  byte dayOfWeek;
  byte hour;
  byte minute;
  byte second;
  byte functionName;
  boolean isActive;
} AlarmStruct;
