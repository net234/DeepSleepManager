/*************************************************
 *************************************************
    DeepSleepManager  Allow BP0 to be user push button and a awake form deep sleep buton while sleeping
    Copyright 2020  NET234 https://github.com/net234/DeepSleepManager

  This file is part of DeepSleepManager.

    DeepSleepManager is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DeepSleepManager is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with betaEvents.  If not, see <https://www.gnu.org/licenses/lglp.txt>.



   TODO: grab millisec lost in a RTC memory varibale for a better adjust of timestamps

**********************************************************************************/



#include "DeepSleepManager.h"


//My WiFi won't reconnect after deep sleep using ``WAKE_RF_DISABLED``
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//
//When you implement deep sleep using ``WAKE_RF_DISABLED``, this forces what
//appears to be a bare metal disabling of WiFi functionality, which is not
//restored using ``WiFi.forceSleepWake()`` or ``WiFi.mode(WIFI_STA)``. If you need
//to implement deep sleep with ``WAKE_RF_DISABLED`` and later connect to WiFi, you
//will need to implement an additional (short) deep sleep using
//``WAKE_RF_DEFAULT``.
//https://github.com/esp8266/Arduino/pull/7338/commits/ae0d8ffe84944284665facf13f847887e6459cfa

uint8_t DeepSleepManager::getRstReason(const int16_t buttonPin) {
  // rstReason is inited once
  if (rstReason != REASON_NOT_INITED) {
    return (rstReason);
  }
  // compute restReason
  bool bpStatus = HIGH;
  if ( buttonPin >= 0 ) {
    // pinMode(buttonPin, INPUT);  // not needed it should be in input mode by default
    bpStatus = digitalRead(buttonPin);
  }
  // get ESP reset reason
  rst_info* resetInfoPtr = ESP.getResetInfoPtr();
  rstReason = (resetInfoPtr->reason);
  // adjust ESP rstReason if bp is down or cold boot
  if ( rstReason == REASON_DEEP_SLEEP_AWAKE) {
    if (bpStatus == LOW ) rstReason = REASON_USER_BUTTON;
  }

  // adjust ESP rstReason if RTC memory not initialised

  ESP.rtcUserMemoryRead(0, RTC_DATA(savedRTCmemory));
  // if RTCmemory not proprely inited it is a cold boot

  //Serial.print("CRC1="); Serial.println(getCrc8( (uint8_t*)&savedRTCmemory,sizeof(savedRTCmemory) ));
  if ( !setCrc8( &savedRTCmemory.crc8 + 1, sizeof(savedRTCmemory) - 1, savedRTCmemory.crc8 ) ) {
    //Serial.println("Power on boot");
    //savedRTCmemory.crc8 = 0;
    savedRTCmemory.bootCounter = 0;
    savedRTCmemory.increment = 0;
    savedRTCmemory.remainingTime = 0;
    savedRTCmemory.actualTimestamp = 0;
    savedRTCmemory.powerOnTimestamp = 0;

    rstReason = REASON_DEFAULT_RST;  // this append only after a full power down
  }
  WiFiLocked = (savedRTCmemory.remainingTime > 0);
  // little trick to leave timeStatus to timeNotSet
  // TODO: see with https://github.com/PaulStoffregen/Time to find a way to say timeNeedsSync
  adjustTime(savedRTCmemory.actualTimestamp);
  savedRTCmemory.bootCounter++;

  // check for enable Wifi  (tricky stuff) restore previous restart event to hide WiFi restart
  if (savedRTCmemory.increment < 0) {
    rstReason = -savedRTCmemory.increment;
    savedRTCmemory.increment = 0;
  }
  bootTimestamp = savedRTCmemory.actualTimestamp;
  if (rstReason == REASON_DEEP_SLEEP_AWAKE && savedRTCmemory.remainingTime == 0 )  rstReason = REASON_DEEP_SLEEP_TERMINATED;
  saveRTCmemory();
  //system_rtc_mem_write(10, &savedRTCmemory, sizeof(savedRTCmemory));
  return (rstReason);
}

//=============== important note ==============================================
//ESP.deepSleep(microseconds, mode)`` will put the chip into deep sleep.
//``mode`` is one of ``WAKE_RF_DEFAULT``, ``WAKE_RFCAL``, ``WAKE_NO_RFCAL``, ``WAKE_RF_DISABLED``.
//(GPIO16 needs to be tied to RST to wake from deepSleep.)
//The chip can sleep for at most ``ESP.deepSleepMax()`` microseconds.
//If you implement deep sleep with ``WAKE_RF_DISABLED`` and require WiFi functionality on wake up,
//you will need to implement an additional ``WAKE_RF_DEFAULT`` before WiFi functionality is available.
//https://github.com/esp8266/Arduino/pull/7338/commits/ae0d8ffe84944284665facf13f847887e6459cfa


void DeepSleepManager::permanentDeepSleep() {
  savedRTCmemory.remainingTime = 0;
  savedRTCmemory.increment = 0;
  savedRTCmemory.actualTimestamp = now();
  saveRTCmemory();
  ESP.deepSleep(0, RF_DEFAULT);
}

void DeepSleepManager::startDeepSleep(const uint32_t sleepTimeSeconds, const uint16_t increment, const uint16_t offset ) { // start a deepSleepMode with   default increment 2 hours
  savedRTCmemory.remainingTime = sleepTimeSeconds;
  savedRTCmemory.actualTimestamp = now();
  uint16_t nextIncrement = increment;
  if (nextIncrement == 0) nextIncrement = 3 * 60 * 60;
  savedRTCmemory.increment = nextIncrement;
  if (savedRTCmemory.remainingTime <= nextIncrement) {
    nextIncrement = savedRTCmemory.remainingTime;
  }
  if (offset > 0) {
    nextIncrement -= offset;
    if ( nextIncrement <= 0 ) nextIncrement = 1;
  }
  savedRTCmemory.remainingTime -= nextIncrement;
  //Serial.print("sizeof savedRTCmemory=");
  //Serial.println(sizeof savedRTCmemory);  //20
  int32_t adjust = 149300;
  uint32_t milli = millis();
  if (offset > 0 && nextIncrement > milli / 1000) {
    nextIncrement -= milli / 1000;
    adjust += (milli % 1000) * 1000;
  }
  savedRTCmemory.actualTimestamp = now() + nextIncrement;
  saveRTCmemory();
  if (nextIncrement > 0) ESP.deepSleep(nextIncrement * 1.004 * 1E6 - adjust, (savedRTCmemory.remainingTime > 0 ) ? RF_DISABLED : RF_DEFAULT);  //2094
}



void DeepSleepManager::continueDeepSleep() {
  int16_t nextIncrement = savedRTCmemory.increment;
  if (savedRTCmemory.remainingTime <= nextIncrement) {
    nextIncrement = savedRTCmemory.remainingTime;
  }
  savedRTCmemory.remainingTime -= nextIncrement;
  savedRTCmemory.actualTimestamp = now() + nextIncrement;
  saveRTCmemory();

  if (nextIncrement > 0) ESP.deepSleep(nextIncrement * 1.004 * 1E6 - micros() - 149300 , (savedRTCmemory.remainingTime > 0 ) ? RF_DISABLED : RF_DEFAULT);  //2094
}




void DeepSleepManager::WiFiUnlock() {
  savedRTCmemory.increment = -rstReason;
  savedRTCmemory.remainingTime = 0;
  saveRTCmemory();
  ESP.deepSleep(50L * 1000, RF_DEFAULT);   //reset in 50 ms to clear RF_DISABLED
  while (true) delay(1);
}

uint16_t DeepSleepManager::getBootCounter() {      // Number of reboot since power on
  return savedRTCmemory.bootCounter;
}

uint32_t DeepSleepManager::getRemainingTime() {    // Number of second remaining to terminate deep sleep
  return savedRTCmemory.remainingTime;
}

time_t   DeepSleepManager::getBootTimestamp() {    // Timestamp of the last boot time
  return bootTimestamp;
}

time_t   DeepSleepManager::getPowerOnTimestamp() { // Timestamp of the power on (set to 0 at power on)
  return savedRTCmemory.powerOnTimestamp;
}

time_t   DeepSleepManager::getActualTimestamp() { // Timestamp saved in RTC
  return savedRTCmemory.actualTimestamp;
}


void     DeepSleepManager::setActualTimestamp(time_t timestamp) {   // save timestamp in RTC
  if (timestamp == 0) timestamp = now();
  if (bootTimestamp == 0 ) {
    bootTimestamp = timestamp - millis() / 1000;
  }
  if (savedRTCmemory.powerOnTimestamp == 0 && year(timestamp) >= 2000) {
    savedRTCmemory.powerOnTimestamp = timestamp - savedRTCmemory.actualTimestamp;
    bootTimestamp -= timestamp - savedRTCmemory.actualTimestamp;
  }
  savedRTCmemory.actualTimestamp = timestamp;
  saveRTCmemory();
}


bool DeepSleepManager::saveRTCmemory() {
  setCrc8(&savedRTCmemory.crc8 + 1, sizeof(savedRTCmemory) - 1, savedRTCmemory.crc8);
  //system_rtc_mem_read(64, &savedRTCmemory, sizeof(savedRTCmemory));
  return ESP.rtcUserMemoryWrite(0, RTC_DATA(savedRTCmemory) );
}



bool DeepSleepManager::restoreRTCData( uint32_t* data, const uint16_t size) {
  const uint16_t offset = (sizeof(savedRTCmemory) + 3) / 4;
  if (savedRTCmemory.bootCounter <= 1 || savedRTCmemory.userDataSize != size) return false;

  uint32_t buffer[(size + 3) / 4];
  if (!ESP.rtcUserMemoryRead( offset, buffer, size))   return false;

  uint8_t  aCrc8;
  setCrc8(buffer, size, aCrc8);
  if ( aCrc8 != savedRTCmemory.userDataCrc8 )   return false;

  memcpy(data, buffer, size);
  return true;
}

bool DeepSleepManager::saveRTCData( uint32_t* data, const uint16_t size) {
  const uint16_t offset = (sizeof(savedRTCmemory) + 3) / 4;
  if ( offset + (size + 3) / 4 > 125 ) return (false);
  setCrc8(data, size, savedRTCmemory.userDataCrc8);
  savedRTCmemory.userDataSize = size;
  return ESP.rtcUserMemoryWrite( offset, data, size ) && saveRTCmemory();
}


inline uint8_t _crc8_ccitt_update(uint8_t crc, const uint8_t inData);

bool  DeepSleepManager::setCrc8(const void* data, const uint16_t size, uint8_t &refCrc ) {
  uint8_t* dataPtr = (uint8_t*)data;
  uint8_t crc = 0xFF;
  for (uint8_t i = 0; i < size; i++) crc = _crc8_ccitt_update(crc, *(dataPtr++));
  //Serial.print("CRC "); Serial.print(refCrc); Serial.print(" / "); Serial.println(crc);
  bool result = (crc == refCrc);
  refCrc = crc;
  return result;
}

/////////////////////////////////////////////////////////////////////////
//  crc 8 tool
// https://www.nongnu.org/avr-libc/user-manual/group__util__crc.html


//__attribute__((always_inline))
inline uint8_t _crc8_ccitt_update  (uint8_t crc, const uint8_t inData)   {
  uint8_t   i;
  crc ^= inData;

  for ( i = 0; i < 8; i++ ) {
    if (( crc & 0x80 ) != 0 ) {
      crc <<= 1;
      crc ^= 0x07;
    } else {
      crc <<= 1;
    }
  }
  return crc;
}
