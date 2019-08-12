#include <Wire.h>
 
/*

 Pin    |  Interrrupt # | Arduino Platform
 ---------------------------------------
 2      |  0            |  All
 3      |  1            |  All
 18     |  5            |  Arduino Mega Only
 19     |  4            |  Arduino Mega Only
 20     |  3            |  Arduino Mega Only
 21     |  2            |  Arduino Mega Only

 */
 
 int SelfI2CID = 1;
 
 int nChannels = 16;
 int ControlPins[16] = {22, 24, 26, 28, 30, 32, 34, 36, 38, 40, 42, 44, 46, 52, 50, 48};
 int ChannelPhases[16]   = {0,  0,  0,  0,  0,  1,  1,  1,  1,  1,  1,  2,  2,  2,  2,  2};
 int DimTimes[16] = {-1};
 int BrightnessesReceived[16] = {0};
 
 int PowerFrequency = 50; // In Hz
 int MinBrightness = 2; // Avoids False triggerings
 int MaxBrightness = 255; // Defines the quantization size.
 
 int BrightnessTimeStepMicros = 1000000 / (MaxBrightness * (2*PowerFrequency));
 
 int PhasePins[3] = {4, 0, 1};
 
 int MasterFaderPin = A0;
 int MasterValue = 0;
 int MaxMasterValue = 1023;
 unsigned long MasterReadDeltaMicros = 30000;
 int MasterPWMPin = 7;
 
 unsigned long MasterLastRead = 0;
 
 volatile unsigned long PhaseZCTimes[3] = {0, 0, 0};
 volatile bool PinFired[16] = {false};
 
 unsigned long CurrentTime = 0;
 
 void setup()
{
  Wire.begin(SelfI2CID);
  Wire.onReceive(receiveDimCommand);
  for(int Channel=0; Channel<nChannels; Channel++){
    pinMode(ControlPins[Channel], OUTPUT);// Set AC Load pin as output
  }
  attachInterrupt(PhasePins[0], ZC_0, RISING);  
  attachInterrupt(PhasePins[1], ZC_1, RISING);  
  attachInterrupt(PhasePins[2], ZC_2, RISING);  
  
  Serial.begin(9600);
}

void receiveDimCommand(int BytesReceived){
  int Byte;
  int Channel = -1;
  int Brightness = 0;
  Serial.println("Received DIM command");
  for(int nByte=0; nByte<BytesReceived; nByte++){
    Byte = Wire.read();
    Serial.println(Byte);
    if(!(Byte >> 7)){
      Channel = ((Byte >> 2) & B1111);
      Brightness = (Byte & B11) << 6;
    }
    else{
      if(Channel >= 0){
        Brightness += (Byte & B111111);
        BrightnessesReceived[Channel] = Brightness;
        if(Brightness < MinBrightness){
          DimTimes[Channel] = -1;
        }
        else{
          DimTimes[Channel] = (MaxBrightness - Brightness) * BrightnessTimeStepMicros * MasterValue / MaxMasterValue;
        }
      }
      Channel = -1;
      Brightness = 0;
    }
  }
}

void ZC_0()  
{
  PhaseZCTimes[0] = micros();
  for(int Channel=0; Channel<5; Channel++){
    PinFired[Channel] = false;
  }
}

void ZC_1()  
{
  PhaseZCTimes[1] = micros();
  for(int Channel=5; Channel<11; Channel++){
    PinFired[Channel] = false;
  }
}

void ZC_2()
{
  PhaseZCTimes[2] = micros();
  for(int Channel=11; Channel<16; Channel++){
    PinFired[Channel] = false;
  }
}

void ReadMasterValue(){
  int NewMasterValue = MaxMasterValue - analogRead(MasterFaderPin);
  if((NewMasterValue > MasterValue + 1) || (NewMasterValue < MasterValue - 1) || (NewMasterValue == 0) || (NewMasterValue == MaxMasterValue)){
    MasterValue = NewMasterValue;
    for(int Channel=0; Channel<nChannels; Channel++){
      DimTimes[Channel] = (MaxBrightness - BrightnessesReceived[Channel]) * BrightnessTimeStepMicros * MasterValue / MaxMasterValue;
    }
    analogWrite(MasterPWMPin, MasterValue >> 2);
  }
}
  
void loop(){
  CurrentTime = micros();
  for(int Channel=0; Channel<nChannels; Channel++){ 
    if((DimTimes[Channel] >= 0) && !PinFired[Channel] && (CurrentTime - PhaseZCTimes[ChannelPhases[Channel]]) > DimTimes[Channel]) {
      digitalWrite(ControlPins[Channel], HIGH);   // triac firing
      delayMicroseconds(10);
      digitalWrite(ControlPins[Channel], LOW);
      PinFired[Channel] = true;
    }
  }
  if((CurrentTime - MasterLastRead) > MasterReadDeltaMicros){
    ReadMasterValue();
    MasterLastRead = CurrentTime;
  }
}
  
