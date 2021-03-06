/*
   Copyright (c) 2018, DADAMACHINES
   Author: Justin Pedro
   All rights reserved.

   Redistribution and use in source and binary forms, with or without modification,
   are permitted provided that the following conditions are met:

    1. Redistributions of source code must retain the above copyright notice,
       this list of conditions and the following disclaimer.

    2. Redistributions in binary form must reproduce the above copyright notice,
       this list of conditions and the following disclaimer in the documentation
        and/or other materials provided with the distribution.

    3. Neither the name of DADAMACHINES nor the names of its contributors may be used
       to endorse or promote products derived from this software without
       specific prior written permission.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
   AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
   IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
   DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
   FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
   DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
   SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
   CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
   OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/


#ifndef _DADASYSEX_HPP
#define _DADASYSEX_HPP

#pragma once

#if 0
#define SYSEX_DEBUG_CONT(X) SerialUSB.print(X)
#define SYSEX_DEBUG(X) SerialUSB.println(X);SerialUSB.flush()
#else
#define SYSEX_DEBUG_CONT(X)
#define SYSEX_DEBUG(X)
#endif

extern void mapFixedDurationConfig();
extern void initMaxMinMap();
extern void handleMinConfig(byte pin, int val, int power);
extern void handleMaxConfig(byte pin, int val, int power);
byte dadaSysEx::sysexOutArr[dadaSysEx::SYSEX_CONFIG_LEN + 1];
byte dadaSysEx::UsbSysExBuffer[dadaSysEx::MAX_SYSEX_MESSAGE_SIZE];
byte dadaSysEx::structBuffer[dadaSysEx::MAX_SYSEX_MESSAGE_SIZE];
programCFG storedDataForCompare;

bool dadaSysEx::safeToRead(byte * now, const byte* before, unsigned bufferLen, unsigned readLen) {
  return (now - before) <= bufferLen - readLen; 
}

byte* dadaSysEx::getStructFromArray(byte* arr, int len) {
   byte* pAligned = structBuffer;
   pAligned += 4 - (((int)pAligned) % 4);
   memcpy(pAligned, arr, len);
   return pAligned;
}

bool dadaSysEx::handleSysEx(byte * arr, unsigned int len)
{
   SYSEX_DEBUG_CONT("handling Sysex of len ");
   SYSEX_DEBUG(len);

   unsigned int lenRemaining = len;
   const byte* startFrame = arr;
   if(len > 1 && (*arr == SYSEX_START))
   {
      arr++;
      lenRemaining--;
      // ignore the sysex framing
   }
  
   if (len < SYSEX_MIN_CONFIG_LEN)
   {
      if (len != SYSEX_GET_CONFIG_LEN)
      {
         if (len != SYSEX_SET_MIN_MAX_LEN)
         {
             return false;
         }
      }
   }

   if (*arr++ != 0) {
     SYSEX_DEBUG("Sysex bad delimiter");
     return false;
   }
   lenRemaining--;

   if (getIntFromArray(arr) != SYSEX_CONFIG_HEADER)
   {
       if (getIntFromArray(arr) == SYSEX_MIN_SET_HEADER) {
         arr += sizeof(int);
         int pin = *arr++;
         int value = *arr++;
         value <<= 7;
         value += *arr++;
         int power = *arr++;
         handleMinConfig(pin, value, power);        
         return true;
       } else if (getIntFromArray(arr) == SYSEX_MAX_SET_HEADER) {
         arr += sizeof(int);
         int pin = *arr++;
         int value = *arr++;
         value <<= 7;
         value += *arr++;
         int power = *arr++;
         handleMaxConfig(pin, value, power);        
         return true;
       }
       SYSEX_DEBUG("Sysex bad header");
       return false;
   }
   arr += sizeof(int);
   lenRemaining -= sizeof(int);

   if (len == SYSEX_GET_CONFIG_LEN)
   {
       if (getIntFromArray(arr) == SYSEX_CONFIG_GET_CONFIG)
       {
         // provide a small delay in case they need to get ready to receive this data
         delay(200);
         saveConfigToSysEx();
         return true;
       }
       if (getIntFromArray(arr) == SYSEX_CONFIG_GET_VERSION)
       {
         // provide a small delay in case they need to get ready to receive this data
         delay(200);
         sendVersionToSysEx();
         return true;
       }
       SYSEX_DEBUG("Sysex bad len");
       return false;
   }
   
   if (getIntFromArray(arr) != SYSEX_CONFIG_PINS)
   {
       SYSEX_DEBUG("Sysex bad pins header");
       return false;
   }
   arr += sizeof(int);
   lenRemaining -= sizeof(int);

   const int numPins = getIntFromArray(arr) & 0x0FF;
   if (numPins != OUTPUT_PINS_COUNT)
   {
       SYSEX_DEBUG("Sysex bad count header");
       return false;
   }
   arr += sizeof(int);
   lenRemaining -= sizeof(int);

   SYSEX_DEBUG("Sysex read config");

   dataCFG* dataP = (dataCFG*) getStructFromArray(arr, sizeof(dataCFG));

   if (hasConfigChanged(cfgData, dataP))
   {
      // avoid writing to Flash unless there is a need
      copyConfig(dataP, cfgData);
      nvStore.write(*cfgData);
   }
   arr += sizeof(dataCFG);
   lenRemaining -= sizeof(dataCFG);
    
   if (getIntFromArray(arr) != SYSEX_CONFIG_VELOCITY)
   {
       SYSEX_DEBUG("Sysex bad vel header");
       return false;
   }
   arr += sizeof(int);
   lenRemaining -= sizeof(int);
   
   SYSEX_DEBUG("Sysex check velo");
   storedDataForCompare = programStore.read();
   
   velocityCFG* veloP = (velocityCFG*) getStructFromArray(arr, sizeof(velocityCFG));
   bool programChanged = false;
   decodeForSysex(veloP);
   if (hasConfigChanged(&(storedDataForCompare.velocityConfig), veloP))
   {
      SYSEX_DEBUG("Sysex writing velo");
      // avoid writing to Flash unless there is a need
      copyConfig(veloP, &(cfgProgram->velocityConfig));
      programChanged = true;
      SYSEX_DEBUG("Sysex writing velo done");
   }

   arr += sizeof(velocityCFG);
   lenRemaining -= sizeof(velocityCFG);

   SYSEX_DEBUG("Sysex check gate");
   if (safeToRead(arr, startFrame, len, sizeof(gateCFG) + sizeof(int)) && getIntFromArray(arr) == SYSEX_CONFIG_GATE)
   {
     SYSEX_DEBUG("Sysex writing gate");
     // gate is an optional config section
     arr += sizeof(int);
     lenRemaining -= sizeof(int);
  
     gateCFG* gateP = (gateCFG*) getStructFromArray(arr, sizeof(gateCFG));
     decodeForSysex(gateP);
     if (hasConfigChanged(&(storedDataForCompare.gateConfig), gateP))
     {
        // avoid writing to Flash unless there is a need
        copyConfig(gateP, &(cfgProgram->gateConfig));
        programChanged = true;
        mapFixedDurationConfig();
     }
  
     // I know this line is not really needed, but I don't want it forgotten when we extend this method
     arr += sizeof(gateCFG);
     lenRemaining -= sizeof(gateCFG);
     SYSEX_DEBUG("Sysex writing gate done");
   }

   if (programChanged) 
   {
      SYSEX_DEBUG("Sysex program changed");
      programStore.write(*cfgProgram);
      initMaxMinMap();
   }
   
   SYSEX_DEBUG("Sysex success");
   SYSEX_DEBUG(lenRemaining);
   return true;
}

bool dadaSysEx::handleSysExUSBPacket(midiEventPacket_t rx)
{
    bool ret = false;
    byte b;

    for(int i = 1; i < 4; ++i) {
      switch(i) {
        case 1:
          b = rx.byte1;
        break;
        case 2:
          b = rx.byte2;
        break;
        case 3:
          b = rx.byte3;
        break;
      }

      if (b == SYSEX_END) {
        UsbSysExBuffer[UsbSysExCursor++] = b;
        
        ret = handleSysEx(UsbSysExBuffer, UsbSysExCursor);
        UsbSysExCursor = 0;
        break;
      } else if (((b & 0x80) == 0) || (b == SYSEX_START)) {
        if (b == SYSEX_START) {
            UsbSysExCursor = 0;
        }
         UsbSysExBuffer[UsbSysExCursor++] = b;

         if (UsbSysExCursor >= MAX_SYSEX_MESSAGE_SIZE) {
           // Something went wrong with message, abort
           UsbSysExCursor = 0;
           break;
         }
      }
    }

    return ret;
}

void dadaSysEx::saveConfigToSysEx()
{
   *cfgProgram = programStore.read();
  
   byte* outP = &sysexOutArr[0];

   *outP++ = SYSEX_START;

   *outP++ = 0;

   outP = putIntToArray(outP, SYSEX_CONFIG_HEADER);

   outP = putIntToArray(outP, SYSEX_CONFIG_PINS);

   *outP++ = 0;

#if PWM_SUPPORT
   *outP++ = 1;
#else
   *outP++ = 0;
#endif

   *outP++ = 0;

   *outP++ = OUTPUT_PINS_COUNT;

   dataCFG* dataP = (dataCFG*) outP;
   copyConfig(cfgData, dataP);
   sanitizeForSysex(dataP);
   outP += sizeof(dataCFG);
    
   outP = putIntToArray(outP, SYSEX_CONFIG_VELOCITY);

   velocityCFG* veloP = (velocityCFG*) outP;
   copyConfig(&(cfgProgram->velocityConfig), veloP);
   sanitizeForSysex(veloP);
   encodeForSysex(veloP);
   outP += sizeof(velocityCFG);

   outP = putIntToArray(outP, SYSEX_CONFIG_GATE);

   gateCFG* gateP = (gateCFG*) outP;
   copyConfig(&(cfgProgram->gateConfig), gateP);
   encodeForSysex(gateP);
   outP += sizeof(gateCFG);

   *outP = SYSEX_END;

   // the midi2.send function probably doesn't do anything with the current hardware, but I'm leaving it in for completeness
   if (midi2 != NULL) {
       midi2->sendSysEx(SYSEX_CONFIG_LEN, sysexOutArr, true);
   }
   MidiUSB_sendSysEx(sysexOutArr, SYSEX_CONFIG_LEN);
}

void dadaSysEx::sanitizeForSysex(velocityCFG* veloP)
{
  for (int i = 0; i < OUTPUT_PINS_COUNT; ++i)
  {
    if(veloP->velocityProgram[i] < MIN_PROGRAM || veloP->velocityProgram[i] > MAX_PROGRAM)
    {
      veloP->velocityProgram[i] = MAX_MIN_PROGRAM;
    }
    if( veloP->min_milli[i] > 1016) {
      veloP->min_milli[i] = MAX_MIN_INFINITE;
    }
    if( veloP->max_milli[i] > 1016) {
      veloP->max_milli[i] = MAX_MIN_INFINITE;
    }
    if(veloP->max_milli[i] < 1 || veloP->min_milli[i] > veloP->max_milli[i])
    {
      veloP->min_milli[i] = MAX_MIN_INFINITE;
      veloP->max_milli[i] = MAX_MIN_INFINITE;
      veloP->curve_power[i] = 3;
    }
    if(veloP->curve_power[i] != 3 && veloP->curve_power[i] != 0x12) {
      veloP->curve_power[i] = 3;
    }
  }
}

void dadaSysEx::encodeForSysex(gateCFG* gateP)
{ // we need to avoid having the high bit set in any byte
  for (int i = 0; i < OUTPUT_PINS_COUNT; ++i)
  {
    if(gateP->durationConfiguration[i] < 0)
    {
      gateP->durationConfiguration[i] = 0;
    }
    else if(gateP->durationConfiguration[i] > 16383)
    { // this value can only be 14 bits max
      gateP->durationConfiguration[i] = 16383;
    }
    int lowerPart = gateP->durationConfiguration[i] & 0x007F;
    int upperPart = gateP->durationConfiguration[i] & 0x3F80;
    gateP->durationConfiguration[i] = lowerPart | (upperPart << 1);
  }
}

void dadaSysEx::decodeForSysex(gateCFG* gateP)
{
  for (int i = 0; i < OUTPUT_PINS_COUNT; ++i)
  {
    int lowerPart = gateP->durationConfiguration[i] & 0x007F;
    int upperPart = gateP->durationConfiguration[i] & 0x7F00;
    gateP->durationConfiguration[i] = lowerPart | (upperPart >> 1);
  }
}

void dadaSysEx::sendVersionToSysEx()
{
   byte* outP = &sysexOutArr[0];

   *outP++ = SYSEX_START;

   *outP++ = 0;

   outP = putIntToArray(outP, SYSEX_VERSION_HEADER);

   outP = putIntToArray(outP, SYSEX_FIRMWARE_VERSION);

#if PWM_SUPPORT
   outP = putIntToArray(outP, 0x010000 | OUTPUT_PINS_COUNT);
#else 
   outP = putIntToArray(outP, OUTPUT_PINS_COUNT);
#endif

   *outP = SYSEX_END;

   // the midi2.send function probably doesn't do anything with the current hardware, but I'm leaving it in for completeness
   if (midi2 != NULL) {
       midi2->sendSysEx(SYSEX_VERSION_LEN, sysexOutArr, true);
   }
   MidiUSB_sendSysEx(sysexOutArr, SYSEX_VERSION_LEN);
}

void dadaSysEx::sanitizeForSysex(dataCFG* dataP)
{
     // we need to avoid any values > 127 for sysex     
     for (int i = 0; i < OUTPUT_PINS_COUNT; ++i)
     {
       if(dataP->midiChannels[i] < 0 || dataP->midiChannels[i] > 127)
       {
         dataP->midiChannels[i] = MIDI_CHANNEL_OMNI;
       }
       
       if(dataP->midiNotes[i] < 0 || dataP->midiNotes[i] > 127)
       {
         dataP->midiNotes[i] = 0;
       }
     }
}
  
bool dadaSysEx::hasConfigChanged(dataCFG* config1, dataCFG* config2)
{
    for (int i = 0; i < OUTPUT_PINS_COUNT; ++i)
    {
      if(config1->midiChannels[i] != config2->midiChannels[i])
      {
        return true;
      }
      if(config1->midiNotes[i] != config2->midiNotes[i])
      {
        return true;
      }
    }

    return false;
}
  
inline void dadaSysEx::copyConfig(dataCFG* src, dataCFG* dest)
{
    for (int i = 0; i < OUTPUT_PINS_COUNT; ++i)
    {
      dest->midiChannels[i] = src->midiChannels[i];
      dest->midiNotes[i] = src->midiNotes[i];
    }
}
    
bool dadaSysEx::hasConfigChanged(velocityCFG* config1, velocityCFG* config2)
{
    for (int i = 0; i < OUTPUT_PINS_COUNT; ++i)
    {
      if(config1->velocityProgram[i] != config2->velocityProgram[i])
      {
        return true;
      }
      if(config1->min_milli[i] != config2->min_milli[i])
      {
        return true;
      }
      if(config1->max_milli[i] != config2->max_milli[i])
      {
        return true;
      }
      if(config1->curve_power[i] != config2->curve_power[i])
      {
        return true;
      }
    }

    return false;
}
  
inline void dadaSysEx::copyConfig(velocityCFG* src, velocityCFG* dest)
{
    for (int i = 0; i < OUTPUT_PINS_COUNT; ++i)
    {
      dest->velocityProgram[i] = src->velocityProgram[i];
      dest->min_milli[i] = src->min_milli[i];
      dest->max_milli[i] = src->max_milli[i];
      dest->curve_power[i] = src->curve_power[i];
    }
}


void dadaSysEx::encodeForSysex(velocityCFG* veloP) {
    for (int i = 0; i < OUTPUT_PINS_COUNT; ++i)
    {
      int lowerPart = veloP->min_milli[i] & 0x007F;
      int upperPart = veloP->min_milli[i] & 0x3F80;
      veloP->min_milli[i] = lowerPart | (upperPart << 1);
  
      lowerPart = veloP->max_milli[i] & 0x007F;
      upperPart = veloP->max_milli[i] & 0x3F80;
      veloP->max_milli[i] = lowerPart | (upperPart << 1);
    }
}

void dadaSysEx::decodeForSysex(velocityCFG* veloP) {
  for (int i = 0; i < OUTPUT_PINS_COUNT; ++i)
  {
    int lowerPart = veloP->min_milli[i] & 0x007F;
    int upperPart = veloP->min_milli[i] & 0x7F00;
    veloP->min_milli[i] = lowerPart | (upperPart >> 1);
    
    lowerPart = veloP->max_milli[i] & 0x007F;
    upperPart = veloP->max_milli[i] & 0x7F00;
    veloP->max_milli[i] = lowerPart | (upperPart >> 1);
  }
}

bool dadaSysEx::hasConfigChanged(gateCFG* config1, gateCFG* config2)
{
    for (int i = 0; i < OUTPUT_PINS_COUNT; ++i)
    {
      if(config1->durationConfiguration[i] != config2->durationConfiguration[i])
      {
        return true;
      }
    }

    return false;
}
  
inline void dadaSysEx::copyConfig(gateCFG* src, gateCFG* dest)
{
    for (int i = 0; i < OUTPUT_PINS_COUNT; ++i)
    {
      dest->durationConfiguration[i] = src->durationConfiguration[i];
    }
}
  
inline void dadaSysEx::MidiUSB_sendSysEx(byte *data, size_t len)
{
  byte midiData[4];
  const byte *pData = data;
  int bytesRemaining = len;

  while (bytesRemaining > 0) {
      switch (bytesRemaining) {
      case 1:
          midiData[0] = 5;
          midiData[1] = *pData;
          midiData[2] = 0;
          midiData[3] = 0;
          bytesRemaining = 0;
          break;
      case 2:
          midiData[0] = 6;
          midiData[1] = *pData++;
          midiData[2] = *pData;
          midiData[3] = 0;
          bytesRemaining = 0;
          break;
      case 3:
          midiData[0] = 7;
          midiData[1] = *pData++;
          midiData[2] = *pData++;
          midiData[3] = *pData;
          bytesRemaining = 0;
          break;
      default:
          midiData[0] = 4;
          midiData[1] = *pData++;
          midiData[2] = *pData++;
          midiData[3] = *pData++;
          bytesRemaining -= 3;
          break;
      }
      MidiUSB.write(midiData, 4);
      delay(1);
  }
}

inline int dadaSysEx::getIntFromArray(byte* arr)
{
  int ret = (arr[0] & 0x0FF) << 24;
  ret |= (arr[1] & 0x0FF)  << 16;
  ret |= (arr[2] & 0x0FF)  << 8;
  ret |= (arr[3] & 0x0FF) ;
  return ret;
}
  
inline byte* dadaSysEx::putIntToArray(byte* arr, int in)
{
  arr[0] = in >> 24;
  arr[1] = (in >> 16) & 0x0FF;
  arr[2] = (in >> 8) & 0x0FF;
  arr[3] = in & 0x0FF;

  return arr + sizeof(int);
}
#endif
