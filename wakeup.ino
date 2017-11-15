#include <EEPROM.h>
#include "EEPROMFunctions.h"
#include "myStructures.h"
#include <Time.h>
#include <TimeAlarms.h>
#include <DmxSimple.h>

//############################ Global Variables ####################################
const byte maxAlarms = 10; //must also edit TimeAlarms.h to increase max number of alarms
const byte alarmStructSize = 8; //storage size on EEPROM of an alarm in bytes   
const int lightAddress = 1; //starting address of DMX light
const int lightAddress2 = 7;

String inputString = ""; //string used to store input data from serial connection
String command; //first portion of serial input data
String arguments; //second portion of serial input data
boolean stringComplete = false; //used to signal when a full string of input data has been received

boolean isWifiMode; //defines if responses should be of wifi type or standard serial connection
byte cid; //connection ID for a wifi session

char activeProgram[15] = "None"; //variable to store name of active program
byte numAlarms = 0; //variable to store current number of alarms in place

//############################ Reset Function ####################################
//software-initiated reset of the arduino
void(* resetFunc)(void) = 0; //declare reset at address 0

//############################ Setup ####################################
// put your setup code here, to run once:
void setup() {

  pinMode(6, INPUT_PULLUP); //used to set wifi vs. direct serial (testing) mode (if high, then wifi; else, serial)

  // reserve memory for serial input coming in
  inputString.reserve(100);

  // check value of pin to determine whether we want responses to be in Wifi form or in direct serial form
  if (digitalRead(6) == HIGH) {
    isWifiMode = true;
  }
  else {
    isWifiMode = false;
  }

  // setup DMX
  DmxSimple.usePin(11);
  DmxSimple.maxChannel(48);


  //setup the wifi module and the connection to it
//  Serial.begin(115200);
//  delay(1000);
//  Serial.println("AT+CIOBAUD=19200"); //change baud rate of wifi module
//  delay(1000);
//  serialCatch(true);
//  Serial.end();
  Serial.begin(19200);
  delay(100);
  Serial.setTimeout(2000); //longer timeout in case web service is slower than 1000ms timeout default
//  delay(1000);
  Serial.println((String("AT+") + String("CIPMUX") + String("=1"))); //set wifi module to handle multiple connections
  delay(200);
  serialCatch(true);
  Serial.println(String("AT+") + String("CIPSERVER") + String("=1") + String(",") + String("8080")); //set wifi module as server to listen for requests
  delay(200);
  serialCatch(true);

  //query a web service to get the current time and set the system clock to it
  setCurrentTime();
  //  setTestTime();

  //setup a timer to periodically query the web service to keep time in sync.
  //Alarm.timerRepeat(21600, setCurrentTime); //run every 6 hours

  // read alarms from EEPROM and create them
  loadAlarms();

  // read the active program from memory and run it (mostly used if a reset takes place during a program; will reset program to the start so not useful for wakeup)
//  loadActiveProgram();

  // change active program to None in memory
  storeActiveProgram();

  printResponse("Run");
  endResponse();
}


//############################ Main Loop ####################################
void loop() {
  Alarm.delay(0); //need this for alarms to work properly
  serialCatch(false); //check for serial input
  handleInput(); //attempt to handle any input
}


//############################ Misc Functions ####################################
//function used to print responses over the serial connection
void printResponse(String resp) {
  //wifi mode requires interacting with the wifi module to send data
  if (isWifiMode == true) {
    Serial.println(String("AT+") + String("CIPSEND=") + String(cid) + "," + String(resp.length()+2) + String("\r\n"));
    Alarm.delay(200);
    serialCatch(true);
    Serial.println(resp + String("\r\n") + String("\r\n"));
    Alarm.delay(200);
    serialCatch(true);
  }
  //simple response over serial 
  else {
    Serial.println(resp);
  }
}

void endResponse() {
  printResponse("@#$");
}

void resetDevice() {
  printResponse(String("Resetting") + String("..."));
  endResponse();
  Alarm.delay(300);
  resetFunc();
}

//function used to receive serial data
//if dumpString param is true, then the data isn't important and can be discarded
void serialCatch(boolean dumpString) {
  //loop is true as long as data is in the serial receive buffer
  while(Serial.available()) {
    char inchar = (char)Serial.read();
    inputString += inchar;
    if (inchar == '\n') {
      stringComplete = true;
      //if there is still more data to read, then dump this data (typically junk line breaks and such) and continue reading.
      if(Serial.available()) {
        resetInputString();
      }
    }
  }
  //if data isn't important, then clean up input string
  if (dumpString == true) {
    resetInputString();
  }
}

//function used to reset inputString and stringComplete params
void resetInputString() {
  inputString = "";
  stringComplete = false;
}


//function used to handle serial input and take appropriate action
//returns true if a deactivate program or new program activated command was received; otherwise returns false
//this helps us escape out of a wakeup program
boolean handleInput() {
  //only process if we have a full string
  if (stringComplete) {
    inputString.trim();
    
    // handle cases for commands and take action
    if (parseInput()) {
      // AT commands
      if (command == "AT") {
//        handleAT(arguments);
      }
      // Software reset
      else if (command == "RST") {
        resetDevice();
      }
      // Activate Program
      else if (command == "AP"){
        activateProgram(arguments);

        resetInputString();
        return true;
      }
      // Deactivate Program (sets program to None)
      else if (command == "DP") {
        activateProgram("None");
        
        resetInputString();
        return true;
      }
      // Read Program
      else if (command == "RP") {
        printResponse(activeProgram);
        endResponse();
      }
      // Read Time
      else if (command == "RT") {
        printResponse(readTime());
        endResponse();
      }
      // Set Time (typically not needed since happens automatically on reset)
      else if (command == "ST") {
        setCurrentTime();
        printResponse(readTime());
        endResponse();
      }
      // Manually Set Time (work around if automated time setting isn't working)
      else if (command == "MT") {
        setManualTime(arguments);
        printResponse(readTime());
        endResponse();
      }
      // Set Alarm SA+dayOfWeek,H,MI,S,func,isActive  0 = All, 1 = Sun, etc.
      // Set Daily Alarm SDA+H,MI,S,isRecurring,func,isActive
      // Set Weekly Alarm SWA+H,MI,S,isRecurring,func,isActive,dayOfWeek
      // returns H,MI,S,isRecurring,func,isActive,id,dayOfWeek
      else if (command == "SA") {
        String alarm = setAlarm(arguments);
        printResponse(alarm);
        endResponse();
      }
      // Read Alarms
      else if (command == "RA") {
        if (numAlarms == 0) {
          printResponse("No alarms");
          endResponse();
        }
        else {
          readAlarms(); //prints each alarm as it is read
          endResponse();
        }
      }
      // Disable Alarm (ID)
      else if (command == "DA") {
        String alarm = toggleAlarm(arguments, false);
        printResponse(alarm);

        //reset the arduino to force alarms to be reassigned, thus removing the now deleted one (module has no actual delete functionality)
        printResponse(String("Alarm") + String(" ") + String("disabled"));
        resetDevice();
      }
      // Enable Alarm (ID)
      else if (command == "EA") {
        String alarm = toggleAlarm(arguments, true);
        printResponse(alarm);

        //reset the arduino to force alarms to be reassigned, thus removing the now deleted one (module has no actual delete functionality)
        printResponse(String("Alarm") + String(" ") + String("enabled"));
        resetDevice();
      }
      // Delete Alarm (ID)
      else if (command == "XA") {
        printResponse(String("Deleting") + String(" ") + String("Alarm") + String("..."));
        if(deleteAlarm(arguments)){
          //reset the arduino to force alarms to be reassigned, thus removing the now deleted one (module has no actual delete functionality)
          printResponse(String("Alarm") + String(" ") + String("deleted"));
          resetDevice();
        } else {
          printResponse(String("Alarm") + String(" ") + String("no") + String(" ") + String("exist"));
          endResponse();
        }
      }
      // Wipe Alarms
      else if (command == "WA") {
        printResponse(String("Wiping") + String(" ") + String("Alarm") + String("s") + String("..."));
        wipeAlarms();
        
        //reset the arduino to make the changes take effect (module has no functionality to actually delete an alarm)
        printResponse(String("Alarm") + String("s") + String(" ") + String("wiped"));
        resetDevice();
      }
      else {
        printResponse(String("Unknown") + String(" ") + String("Command"));
        endResponse();
      }
    }

    resetInputString();
  }

  return false;
}

//function used to parse input data and split it into command and arguments params
//returns true if we have good data to process, false otherwise
boolean parseInput() {
  int firstComma;
  int secondComma;
  int colon;
  int funcSep;

  // this block deals with the extra characters that are included when connecting via wifi vs. direct serial over usb cable
  //+IPD,1,15:asdfasfasfasf
  colon = inputString.indexOf(':');
  if (inputString.length() == 0) {
    return false;
  }
  else if (inputString == "\r\n" || inputString == "\n") {
    // skip these blank lines
    return false;
  }
  else if (inputString.indexOf("CLOSED") > -1 || inputString.indexOf("CONNECT") > -1) {
    return false;
  }
  else if (colon == -1) {
    funcSep = inputString.indexOf('+');
  }
  else {
    firstComma = inputString.indexOf(',');
    secondComma = inputString.indexOf(',',firstComma+1);

    if (firstComma == -1 || secondComma == -1) {
      printResponse(String("Bad") + String(" ") + String("Wifi") + String(" ") + String("Command"));
      endResponse();
      return false;
    }
    else {
      cid = (byte)inputString.substring(firstComma+1,secondComma).toInt();
      funcSep = inputString.indexOf('+', colon+1);
    }
  }
  
  if (funcSep == -1) {
    printResponse(String("Bad") + String(" ") + String("Command"));
    endResponse();
    return false;
  }
  else {
    command = inputString.substring(colon+1, funcSep);
    arguments = inputString.substring(funcSep+1);

    return true;
  }
}

//void handleAT(String args) {
//  Serial.println(String("AT+") + args);
//
//  while(Serial.available()) {
//    
//  }
//  
//}

//############################ Memory Storage ####################################
//EEPROM memory map
//byte 0 = numAlarms
//byte 1 = current program
//byte 2-n = alarms in 8 byte chunks (alarms are ordered in EEPROM by the ID)


//function to store the active program to EEPROM
void storeActiveProgram() {
  EEPROM_writeAnything(1, byteFromFunctionName(activeProgram));
}

//function to read active program from EEPROM and activate it (used after a reset)
void loadActiveProgram() {
  byte functionByte;
  EEPROM_readAnything(1, functionByte);
  String functionName = functionNameFromByte(functionByte);
  functionName.toCharArray(activeProgram, functionName.length()+1);
  activateProgram(String(activeProgram));
}

//function to calculate storage location of a given alarm
int calculateAlarmMemoryLocation(byte id) {
  return ((int)id - 1) * alarmStructSize + 2;
}



//############################ Alarm Helpers ####################################
//function used to convert byte into day of week
timeDayOfWeek_t dowFromByte(byte val) {
  switch (val) {
    case 0:
      return dowInvalid;
    case 1:
      return dowSunday;
    case 2:
      return dowMonday;
    case 3:
      return dowTuesday;
    case 4:
      return dowWednesday;
    case 5:
      return dowThursday;
    case 6:
      return dowFriday;
    case 7:
      return dowSaturday;
    default:
      return dowInvalid;
  }
}


//function used to turn an alarm structure into a printable string
String marshalAlarm(AlarmStruct alarm) {
  String output = String(alarm.dayOfWeek) + "," + String(alarm.hour) + "," + String(alarm.minute) + "," + String(alarm.second) + ",";
  
  output += functionNameFromByte(alarm.functionName);
  output += ",";
  if (alarm.isActive == true) {
    output += "1";
  }
  else {
    output += "0";
  }
  output += ",";
  output +=String(alarm.id);

  return output;
}

//function used to parse input for creating a new alarm and return an alarm structure
AlarmStruct parseAlarm(String args) {
  int counter;
  int startIndex = 0;
  int endIndex;
  AlarmStruct alarm;

  endIndex = args.indexOf(',');
  alarm.dayOfWeek = (byte)args.substring(startIndex).toInt();
  startIndex = endIndex + 1;
    
  endIndex = args.indexOf(',', startIndex);
  alarm.hour = args.substring(startIndex, endIndex).toInt();
  startIndex = endIndex + 1;

  endIndex = args.indexOf(',', startIndex);
  alarm.minute = args.substring(startIndex, endIndex).toInt();
  startIndex = endIndex + 1;

  endIndex = args.indexOf(',', startIndex);
  alarm.second = args.substring(startIndex, endIndex).toInt();
  startIndex = endIndex + 1;
  
  endIndex = args.indexOf(',', startIndex);
  alarm.functionName = byteFromFunctionName(args.substring(startIndex, endIndex));
  startIndex = endIndex + 1;

  String tempVal;
  tempVal = args.substring(startIndex);
  tempVal.trim();
  if (tempVal == "1") {
    alarm.isActive = true;
  }
  else {
    alarm.isActive = false;
  }
  
  
  return alarm;
}


//############################ Alarm Functions ####################################
//function used to set a new alarm
String setAlarm(String args) {
  //if we already have the max number of alarms, then tell the user and skip rest of processing
  if (numAlarms == maxAlarms) {
    return "Already have max # alarms";
  }
  
  //parse args into the alarm structure 
  AlarmStruct alarm = parseAlarm(args);

  //create actual alarm and add id to the struct
  createAlarm(alarm);

  //increment alarm count
  numAlarms++;
  alarm.id = numAlarms; // assign the next id for the alarm

  //store alarm to EEPROM
  EEPROM_writeAnything(calculateAlarmMemoryLocation(alarm.id), alarm);

  //store numAlarms to EEPROM
  EEPROM_writeAnything(0, numAlarms);

  //return string representation of the alarm
  return marshalAlarm(alarm);
}

//function used to enable/disable an existing alarm
//input params are String of alarm id and boolean with true to enable alarm and false to disable
String toggleAlarm(String args, bool enabled) {
  byte id = (byte)args.toInt();
  
  //make sure alarm exists
  if (id > numAlarms) {
    return String("Alarm ") + String("no exist");
  }
  //if it does
  else {
    AlarmStruct alarm;

    //read the alarm from memory
    EEPROM_readAnything(calculateAlarmMemoryLocation(id), alarm);

    //toggle the state of the alarm as needed
    // enable/disable doesn't work so just get the parameters correct and then reset the arduino so that proper alarms are instantiated
    if (enabled == true) {
      alarm.isActive = true;
//      Alarm.enable(alarm.alarmId);
    }
    else {
      alarm.isActive = false;
//      Alarm.disable(alarm.alarmId);
    }

    //store the alarm back to EEPROM
    EEPROM_writeAnything(calculateAlarmMemoryLocation(id), alarm);

    //return updated string representation of alarm
    return marshalAlarm(alarm);
  }
}


//function used to read all alarms from EEPROM and return string representations of each
void readAlarms() {
  AlarmStruct alarm;
  
  for(byte i=1;i<=numAlarms;i++) {
    EEPROM_readAnything(calculateAlarmMemoryLocation(i),alarm);
    printResponse(marshalAlarm(alarm)); //have to print this as we go because we don't have enough RAM to hold them all at once
  }
}


//function used to load the alarms when booting up
void loadAlarms() {
  EEPROM_readAnything(0, numAlarms);
  AlarmStruct alarm;
  
  for(byte i=1;i<=numAlarms;i++) {
    EEPROM_readAnything(calculateAlarmMemoryLocation(i),alarm); //read alarm from EEPROM
    createAlarm(alarm); //create the alarm and update the alarm struct with the alarmId
    EEPROM_writeAnything(calculateAlarmMemoryLocation(i),alarm); //store the alarm with its newly assigned alarmId
  }
}

//function used to instantiate an alarm with the TimeAlarms library
void createAlarm(AlarmStruct alarm) {
  byte alarmId;

  // toggling alarms using the disable/enable functionality doesn't seem to work. Must just choose whether or not to instantiate alarm instead.
  if (alarm.isActive == true) {
    //create the alarm
    //TODO: figure out how to pass the function name as a pointer rather than having this lookup list
    if (functionNameFromByte(alarm.functionName) == "Wakeup") {
      if (alarm.dayOfWeek == 0) {
        alarmId = Alarm.alarmRepeat(alarm.hour, alarm.minute, alarm.second, Wakeup);
      } else {
        alarmId = Alarm.alarmRepeat(dowFromByte(alarm.dayOfWeek), alarm.hour, alarm.minute, alarm.second, Wakeup);
      }
    }
    else if (functionNameFromByte(alarm.functionName) == "WakeupDemo") {
      if (alarm.dayOfWeek == 0) {
        alarmId = Alarm.alarmRepeat(alarm.hour, alarm.minute, alarm.second, WakeupDemo);
      } else {
        alarmId = Alarm.alarmRepeat(dowFromByte(alarm.dayOfWeek), alarm.hour, alarm.minute, alarm.second, WakeupDemo);
      }
    }
    else if (functionNameFromByte(alarm.functionName) == "None") {
      if (alarm.dayOfWeek == 0) {
        alarmId = Alarm.alarmRepeat(alarm.hour, alarm.minute, alarm.second, DeactivateProgram);
      } else {
        alarmId = Alarm.alarmRepeat(dowFromByte(alarm.dayOfWeek), alarm.hour, alarm.minute, alarm.second, DeactivateProgram);
      }
    }
  }

  alarm.alarmId = alarmId; //set the alarmId

  //if alarm isn't active, then disable it
//  if (alarm.isActive == false) {
//    Alarm.disable(alarm.alarmId);
//  }
}

//function used to delete an alarm based on provided ID
boolean deleteAlarm(String args){
  byte id = (byte)args.toInt();

  //make sure id exists
  if (id <= numAlarms) {
    AlarmStruct alarm;
    
    //find all alarms with higher IDs than the one being deleted and move them one position down in memory
    for (byte i=id+1;i<=numAlarms;i++) {
      EEPROM_readAnything(calculateAlarmMemoryLocation(i),alarm);
      alarm.id = i-1;
      EEPROM_writeAnything(calculateAlarmMemoryLocation(i-1),alarm);
    }

    //update the numAlarms variable accordingly
    numAlarms--;
    EEPROM_writeAnything(0,numAlarms);

    return true;
  }
  
  else {
    return false;
  }
}


//function used to remove all alarms in one go
void wipeAlarms() {

  //update the numAlarms variable so we "forget" about all the alarms
  numAlarms = 0;
  EEPROM_writeAnything(0, numAlarms);
}


//############################ Time Functions ####################################
//function used to read the system time and return it as a string
String readTime() {
  //load a structure with the elements of the current system time
  TimeElements tm;
  breakTime(now(),tm);

  String sYear = String(1970 + tm.Year);
  String sMonth = String(tm.Month);
  if (tm.Month < 10) {
    sMonth = "0" + sMonth;
  }
  String sDay = String(tm.Day);
  if (tm.Day < 10) {
    sDay = "0" + sDay;
  }
  String sHour = String(tm.Hour);
  if (tm.Hour < 10) {
    sHour = "0" + sHour;
  }
  String sMinute = String(tm.Minute);
  if (tm.Minute < 10) {
    sMinute = "0" + sMinute;
  }
  String sSecond = String(tm.Second);
  if (tm.Second < 10) {
    sSecond = "0" + sSecond;
  }
  return sYear + "-" + sMonth + "-" + sDay + " " + sHour + ":" + sMinute + ":" + sSecond;
}


//function used to set a time manually (useful mainly during initial coding and debugging to avoid making too many requests to time web service (limited to 250 per day))
void setManualTime(String timeStr){
  //String timeStr = "2015-01-01 00:00";
  parseTimeString(timeStr);
}


//function used to query a web service for the current time and set the system clock to it
//current web service does not support seconds
void setCurrentTime() {
  //storing these long strings in flash memory rather than RAM because they take too much space
  const static char timeQueryConnection[] PROGMEM = "AT+CIPSTART=4,\"TCP\",\"api.worldweatheronline.com\",80";
  const static char timeQuery[] PROGMEM = "GET /free/v2/weather.ashx?key=a7090c9f880d2b8a636d0de393c90&q=80020&showlocaltime=yes&format=json&fx=no&cc=no HTTP/1.1\r\nHost: api.worldweatheronline.com\r\n\r\n";
  
  char myChar;
  int k;

//  Serial.setTimeout(5000); //longer timeout in case web service is slower than 1000ms timeout default
  
  //read string from program memory and send it to wifi module
  for (k=0;k<strlen_P(timeQueryConnection);k++) {
    myChar =  pgm_read_byte_near(timeQueryConnection + k);
    Serial.print(myChar);
  }
  Serial.println();

  //if connection established, then continue
  if (Serial.find("OK")) {
    serialCatch(true);

    //prepare to send query string
    Serial.println(String("AT+") + String("CIPSEND") + String("=4") + String(",") + String("158"));
    Alarm.delay(100);

    //read query string from program memory and send it
    for (k=0;k<strlen_P(timeQuery);k++) {
      myChar =  pgm_read_byte_near(timeQuery + k);
      Serial.print(myChar);
    }
    Serial.println();

    boolean stayInLoop = true;
    String tempStr;
    String timeStr;
    
    //loop through response until we find the right part of the json containing the time
    //{ "data": { "request": [ {"query": "80020", "type": "Zipcode" } ],  "time_zone": [ {"localtime": "2015-11-07 22:53", "utcOffset": "-7.0" } ] }}
    while (stayInLoop) {
      tempStr = Serial.readStringUntil('\"');
      if (tempStr == "localtime"){
        Serial.readStringUntil('\"');
        timeStr = Serial.readStringUntil('\"');
        stayInLoop = false;
      }
    }

    //parse the time string and set system time
    parseTimeString(timeStr);

    //clean up connection with web service (probably unnecessary because service seems to close connections quickly but still good practice to do)
    Serial.println(String("AT+") + String("CIPCLOSE") + String("=4"));
    Alarm.delay(300);
    serialCatch(true);
  }
    
//    Serial.setTimeout(1000); //reset serial timeout back to default
}

//function used to parse time string and set system time
void parseTimeString(String timeStr) {
  int firstHyphen = timeStr.indexOf('-');
  int secondHyphen = timeStr.indexOf('-', firstHyphen + 1);
  int space = timeStr.indexOf(' ', secondHyphen + 1);
  int colon = timeStr.indexOf(':', space + 1);
  int colon2 = timeStr.indexOf(':', colon + 1);

  String strYear = timeStr.substring(0, firstHyphen);
  int iYear = strYear.toInt();

  String strMonth = timeStr.substring(firstHyphen+1, secondHyphen);
  int iMonth = strMonth.toInt();

  String strDay = timeStr.substring(secondHyphen+1, space);
  int iDay = strDay.toInt();

  String strHour = timeStr.substring(space+1, colon);
  int iHour = strHour.toInt();

  int iSec = 0;
  int iMin = 0;
  String strMin;
  if (colon2 == -1) {
    strMin = timeStr.substring(colon+1);
    iMin = strMin.toInt();
  } else {
    strMin = timeStr.substring(colon+1, colon2);
    iMin = strMin.toInt();

    String strSec = timeStr.substring(colon2+1);
    iSec = strSec.toInt();
  }
  

  setTime(iHour, iMin, iSec, iDay, iMonth, iYear);
}

//############################ Program Lookups ####################################
void activateProgram(String args) {
  args.toLowerCase();
  args.trim();
  int val[6] = {0,0,0,0,0,0};

  int startIndex = args.indexOf(',') + 1;

  if (startIndex > 0) {
    args = args + ",";
    int endIndex;

    for (byte i=0; i<6;i++){
      endIndex = args.indexOf(',',startIndex);
      if (endIndex > -1) {
        val[i] = args.substring(startIndex,endIndex).toInt();
        startIndex = endIndex + 1;
      }
      else {
        val[i] = 0;
      }
    }
    args = args.substring(0,args.indexOf(','));
  }
  
  if (args == "none") {
    printResponse(String("Running") + String(" ") + String("None"));
    endResponse();
    DeactivateProgram();
  }
  else if (args == "lightcontrol"){
    printResponse(String("Running") + String(" ") + String("LightControl"));
    endResponse();

    byte cval[6];
    for (byte i=0;i<6;i++) {
      cval[i] = (byte)val[i];
    }
    LightControl(cval);
  }
  else if (args == "wakeup") {
    printResponse(String("Running") + String(" ") + String("Wakeup"));
    endResponse();
    Wakeup();
  }
  else if (args == "wakeupdemo") {
    printResponse(String("Running") + String(" ") + String("WakeupDemo"));
    endResponse();
    WakeupDemo();
  }
  else if (args == "colorchange") {
    printResponse(String("Running") + String(" ") + String("ColorChange"));
    endResponse();

    int cval[3] = {0,0,0};
    for (byte i=0;i<3;i++) {
      cval[i] = val[i];
    }
    ColorChange(cval);
  }
  else {
    printResponse(String("Unknown") + String(" ") + String("Program"));
    endResponse();
  }
}

String functionNameFromByte(byte arg) {
  if (arg == 0) {
    return "None";
  }
  else if (arg == 1) {
    return "Wakeup";
  }
  else if (arg == 2) {
    return "WakeupDemo";
  }
  else if (arg == 3) {
    return "LightControl";
  }
  else if (arg == 4) {
    return "ColorChange";
  }
}

byte byteFromFunctionName(String arg) {
  arg.toLowerCase();
  if (arg == "none") {
    return 0;
  }
  else if (arg == "wakeup") {
    return 1;
  }
  else if (arg == "wakeupdemo") {
    return 2;
  }
  else if (arg == "lightcontrol") {
    return 3;
  }
  else if (arg == "colorchange") {
    return 4;
  }
}

//############################ Program Definitions ####################################
void turnOffLights() {
  byte val[6] = {0,0,0,0,0,0};
  setValues(lightAddress, val);
  setValues(lightAddress2, val);
  Alarm.delay(200);
}

void DeactivateProgram(){
  strncpy(activeProgram, "None", sizeof(activeProgram)-1);
  storeActiveProgram();
  turnOffLights();
}

void LightControl(byte val[6]){
  strncpy(activeProgram, "LightControl", sizeof(activeProgram)-1);
  storeActiveProgram();
  setValues(lightAddress, val);
  Alarm.delay(8000);
  setValues(lightAddress2, val);
}

void Wakeup() {
  strncpy(activeProgram, "Wakeup", sizeof(activeProgram)-1);
  storeActiveProgram();
  WakeupBase((long)18);
}

void WakeupDemo() {
  strncpy(activeProgram, "WakeupDemo", sizeof(activeProgram)-1);
  storeActiveProgram();
  WakeupBase((long)1);
}

//colorchange,<max_bright>=150,<static_time_msec>=7000,<trans_time_msec>=1000
void ColorChange(int in_val[3]) {
  strncpy(activeProgram, "ColorChange", sizeof(activeProgram)-1);
  storeActiveProgram();

  // set initial variable values
  byte max_bright = 150;
  int static_time_msec = 7000;
  int trans_time_msec = 1000;

  // process arguments and load variables if present
  if (in_val[0] > 0) {
    max_bright = (byte)in_val[0];
  }
  if (in_val[1] > 0) {
    static_time_msec = in_val[1];
  }
  if (in_val[2] > 0) {
    trans_time_msec = in_val[2];
  }
  
//  int comma1 = args.indexOf(',');
//  if (comma1 != -1) {
//    max_bright = (byte) args.substring(0, comma1).toInt();
//    int comma2 = args.indexOf(',', comma1 + 1);
//
//    if (comma2 != -1) {
//      static_time_msec = args.substring(comma1+1, comma2).toInt();
//      int comma3 = args.indexOf(',', comma2 + 1);
//
//      if (comma3 != -1) {
//        trans_time_msec = (int)args.substring(comma2+1, comma3).toInt();
//      }
//    }
//  }

  // create seed for random function calls by reading unconnected (noisy) pin
  randomSeed(analogRead(8));

  // initialize variables
  byte val[6];
  byte val2[6];

  byte prev[6];
  byte prev2[6];

  int staticCount;
  staticCount = static_time_msec / 1000;

  boolean keepLooping = true;
  boolean abortProgram = false;
  
  // loop for the program
  while(keepLooping) {
    // do transition between colors
    

    // create new values
    randomSeed(analogRead(8));
    val[0] = random(max_bright);
    val[1] = random(max_bright);
    val[2] = random(max_bright);
    val[3] = 0;
    val[4] = 0;
    val[5] = 0;

    val2[0] = random(max_bright);
    val2[1] = random(max_bright);
    val2[2] = random(max_bright);
    val2[3] = 0;
    val2[4] = 0;
    val2[5] = 0;

    // assign to prev variables for next iteration
    for (byte i = 0; i < 6; i++) {
      prev[i] = val[i];
      prev2[i] = val2[i];
    }

    // send DMX to lights
    setValues(lightAddress, val);
    setValues(lightAddress2, val2);

    // delay for static time
    for (byte j = 0; j < staticCount; j++) {
      Alarm.delay(1000);
      // check for serial input and handle appropriately
      serialCatch(false);
      abortProgram = handleInput();
      if (abortProgram) {
        turnOffLights();
        keepLooping = false;
        break;
      }
    }
  }
}



void WakeupBase(long mult){
  byte startColor[3];
  byte endColor[3];
  boolean abortProgram = false;

  turnOffLights();

  if (!abortProgram) {
    startColor[0] = 0; startColor[1] = 0; startColor[2] = 0;
    endColor[0] = 0; endColor[1] = 0; endColor[2] = 10;
    abortProgram = crossFade(startColor, endColor, 5000*mult); // dark blue
  }
  if (!abortProgram) {
    startColor[0] = 0; startColor[1] = 0; startColor[2] = 10;
    endColor[0] = 2; endColor[1] = 0; endColor[2] = 15;
    abortProgram = crossFade(startColor, endColor, 5000*mult); // purple
  }
  if (!abortProgram) {
    startColor[0] = 2; startColor[1] = 0; startColor[2] = 15;
    endColor[0] = 7; endColor[1] = 0; endColor[2] = 20;
    abortProgram = crossFade(startColor, endColor, 5000*mult); // reddish purple
  }
  if (!abortProgram) {
    startColor[0] = 7; startColor[1] = 0; startColor[2] = 20;
    endColor[0] = 20; endColor[1] = 1; endColor[2] = 0;
    abortProgram = crossFade(startColor, endColor, 5000*mult); // blood orange
  }
  if (!abortProgram) {
    startColor[0] = 20; startColor[1] = 1; startColor[2] = 0;
    endColor[0] = 50; endColor[1] = 6; endColor[2] = 0;
    abortProgram = crossFade(startColor, endColor, 5000*mult); // orange
  }
  if (!abortProgram) {
    startColor[0] = 50; startColor[1] = 6; startColor[2] = 0;
    endColor[0] = 70; endColor[1] = 15; endColor[2] = 0;
    abortProgram = crossFade(startColor, endColor, 5000*mult); // yellow
  }
  if (!abortProgram) {
    startColor[0] = 70; startColor[1] = 15; startColor[2] = 0;
    endColor[0] = 70; endColor[1] = 15; endColor[2] = 2;
    abortProgram = crossFade(startColor, endColor, 5000*mult); // warm white
  }
  if (!abortProgram) {
    startColor[0] = 70; startColor[1] = 15; startColor[2] = 2;
    endColor[0] = 255; endColor[1] = 200; endColor[2] = 100;
    abortProgram = crossFade(startColor, endColor, 10000*mult); // white
  }
  byte finalval[6] = {255,255,255,0,0,0};
  setValues(lightAddress, finalval);
  setValues(lightAddress2, finalval);
  
  for (int i=0;i<1200;i++) {
    serialCatch(false);
    abortProgram = handleInput();
    if (abortProgram) {
      break;
    }
    Alarm.delay(1000);
  }
  turnOffLights();
  strncpy(activeProgram, "None", sizeof(activeProgram)-1);
  storeActiveProgram();
}

//############################ Program Helpers ####################################
boolean crossFade(byte startColor[3], byte endColor[3], long fadeTimeMs) {
  byte RVal = startColor[0];
  byte GVal = startColor[1];
  byte BVal = startColor[2];

  byte RChange = endColor[0] - startColor[0];
  byte GChange = endColor[1] - startColor[1];
  byte BChange = endColor[2] - startColor[2];

  long RStep;
  long GStep;
  long BStep;

  boolean abortProgram = false;

  if(RChange != 0){
    RStep = fadeTimeMs / RChange;
  }
  else {
    RStep = 999999999999;
  }
  if(GChange != 0){
    GStep = fadeTimeMs / GChange;
  }
  else {
    GStep = 999999999999;
  }
  if(BChange != 0){
    BStep = fadeTimeMs / BChange;
  }
  else {
    BStep = 999999999999;
  }

  for (long i=0;i<fadeTimeMs;i++) {
    if((i % RStep) == 0){
      DmxSimple.write(lightAddress,RVal);
      if (RChange > 0){
        RVal++;
      }
      else if (RChange < 0){
        RVal--;
      }
    }
    if((i % GStep) == 0){
      DmxSimple.write(lightAddress+1,GVal);
      if (GChange > 0){
        GVal++;
      }
      else if (GChange < 0){
        GVal--;
      }
    }
    if((i % BStep) == 0){
      DmxSimple.write(lightAddress+2,BVal);
      if (BChange > 0){
        BVal++;
      }
      else if (BChange < 0){
        BVal--;
      }
    }
    if (i % 100 == 0) {
//      Serial.println(String(RVal));
//      Serial.println(String(GVal));
//      Serial.println(String(BVal));
//      Serial.println();
      serialCatch(false);
      abortProgram = handleInput();
      if (abortProgram) {
        return true;
      }
    }
    Alarm.delay(1);
  }
  return false;
}

void setValues(int address, byte val[6]) {
  DmxSimple.write(address + 0, val[0]);
  DmxSimple.write(address + 1, val[1]);
  DmxSimple.write(address + 2, val[2]);
  DmxSimple.write(address + 3, val[3]);
  DmxSimple.write(address + 4, val[4]);
  DmxSimple.write(address + 5, val[5]);
  
}
