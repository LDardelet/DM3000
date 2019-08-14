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
 int NChannels = 16;
 //int ControlPins[16] = {22, 24, 26, 28, 30, 32, 34, 36, 38, 40, 42, 44, 46, 52, 50, 48};
 int ControlPins[16] = {22, 24, 26, 28, 30, 32, 34, 36, 38, 40, 42, 44, 46, 52, 50, 48};
 int ChannelPhases[16]   = {0,  0,  0,  0,  0,  1,  1,  1,  1,  1,  1,  2,  2,  2,  2,  2};
 long DimTimes[16];
 int BrightnessesReceived[16];

 int nChannelMin[3] = {-1,-1,-1};
 int nChannelMax[3] = {-1,-1,-1};
   
 int PowerFrequency = 50; // In Hz
 int MinBrightness = 2; // Avoids False triggerings
 int MaxBrightness = 255; // Defines the quantization size.
 
 long MaxDimTime = int(0.97 * 1000000 / (2*PowerFrequency));
 int MinDimTimeMicros = 160;
 int DimTimeAutoTrigger = 30;
 
 int PhasePins[3] = {4, 0, 1};
 //int PhasePins[3] = {0, 4, 1};
 
 int MasterFaderPin = A0;
 int Fader_MasterValue = 0; // So we need 3 different variables here. One to store DMX sent signal. One to store last modified fader input. One holding the currently used value.
 int DMX_MasterValue = 0;
 float MasterValueOver1000 = 0.0;
 
 float MaxMasterValue = 1023;
 long MasterReadDeltaMicros = 40000;
 int MasterPWMPin = 7;
 
 long MasterLastRead = 0;
 
 bool MasterUpdate = true;
 bool ChannelUpdate[16];

 volatile long PhaseZCTimes[3] = {0, 0, 0};
 volatile bool PinFired[16] = {false};
 
 float BrightnessTimeStepMicros = 1000000 / (MaxBrightness * (2*PowerFrequency));
 float MaxBrightnessTimeMicros = BrightnessTimeStepMicros * MaxBrightness;
 float BrightnessTimeStepNanosOverMaxMasterValue = 1000 * BrightnessTimeStepMicros / MaxMasterValue; // in nanos because of float precision
 
 long CurrentTime = 0;
 long tStart;
 int nLoops = 0;
 long Failure = 0;
 
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
  
  int nPhase = 0;
  for (int nChannel=0; nChannel < NChannels; nChannel++){
    DimTimes[nChannel] = -1;
    BrightnessesReceived[nChannel] = 0;
    if(ChannelPhases[nChannel] == nPhase){
      if (nChannelMin[nPhase] == -1){
        nChannelMin[nPhase] = nChannel;
      }
      nChannelMax[nPhase] = nChannel;
    } else {
      nPhase++;
    }
  }
  Serial.begin(57600);
//Serial.println(BrightnessTimeStepMicros);
//Serial.println(MaxBrightnessTimeMicros);
//Serial.println(BrightnessTimeStepNanosOverMaxMasterValue);
  tStart = micros();
}

void receiveDimCommand(int BytesReceived){
  int Byte;
  int Channel = -1;
  int Brightness = 0;
  //Serial.println("Received DIM command");
  for(int nByte=0; nByte<BytesReceived; nByte++){
    Byte = Wire.read();
    //Serial.println(Byte);
    if(!(Byte >> 7)){
      Channel = ((Byte >> 2) & B11111);
      Brightness = (Byte & B11) << 6;
    }
    else{
      if(Channel >= 0){
        Brightness += (Byte & B111111);
        if(Channel < nChannels){
          BrightnessesReceived[Channel] = Brightness;
          ChannelUpdate[Channel] = true;
        }
        else {
          //Serial.println("MasterValue");
          if(Brightness != DMX_MasterValue){
            MasterUpdate = true;
            DMX_MasterValue = Brightness;
            MasterValueOver1000 = ((DMX_MasterValue << 2) + B11)/1000.0;
            analogWrite(MasterPWMPin, DMX_MasterValue);
          }
          //Serial.println(DMX_MasterValue);
        }
      }
      Channel = -1;
      Brightness = 0;
    }
  }
  for(int nChannel=0; nChannel<nChannels; nChannel++){
    if (MasterUpdate || ChannelUpdate[nChannel]){
      UpdateChannelDimTime(nChannel);
      ChannelUpdate[nChannel] = false;
    }
  }
  MasterUpdate = false;
}

void UpdateChannelDimTime(int nChannel){
  DimTimes[nChannel] = max(MinDimTimeMicros, int(MaxBrightnessTimeMicros - float(BrightnessesReceived[nChannel]) * MasterValueOver1000 * BrightnessTimeStepNanosOverMaxMasterValue));
  if (DimTimes[nChannel] > MaxDimTime){
    DimTimes[nChannel] = -1;
  }
}

void ZC_0()  
{
  //Serial.println("ZC0");
  int nPhase = 0;
  PhaseZCTimes[nPhase] = micros();
  //Serial.println(PhaseZCTimes[nPhase]);
  for(int nChannel = nChannelMin[nPhase]; nChannel < nChannelMax[nPhase] + 1; nChannel ++){
    PinFired[nChannel] = false;
  }
}

void ZC_1()  
{
  int nPhase = 1;
  PhaseZCTimes[nPhase] = micros();
  for(int nChannel = nChannelMin[nPhase]; nChannel < nChannelMax[nPhase] + 1; nChannel ++){
    PinFired[nChannel] = false;
  }
}

void ZC_2()  
{
  int nPhase = 2;
  PhaseZCTimes[nPhase] = micros();
  for(int nChannel = nChannelMin[nPhase]; nChannel < nChannelMax[nPhase] + 1; nChannel ++){
    PinFired[nChannel] = false;
  }
}

void ReadMasterValue(){
  int NewMasterValue = MaxMasterValue - analogRead(MasterFaderPin);
  //int NewMasterValue = analogRead(MasterFaderPin); // for some reason this seems better than line above. Dunno why
  if((NewMasterValue > Fader_MasterValue + 2) || (NewMasterValue < Fader_MasterValue - 2)){// || (NewMasterValue == 0) || (NewMasterValue == MaxMasterValue)){
  //Serial.println.println("UpdateFaderMaster");
    Fader_MasterValue = NewMasterValue;
    MasterValueOver1000 = Fader_MasterValue / 1000.0;
    for(int nChannel=0; nChannel<nChannels; nChannel++){
      UpdateChannelDimTime(nChannel);
    }
    analogWrite(MasterPWMPin, Fader_MasterValue >> 2);
    
  //Serial.println(MasterValue);
  //Serial.println(BrightnessesReceived[0]);
    //Serial.println(BrightnessesReceived[1]);
  //Serial.println(DimTimes[0]);
  //Serial.println(int(MaxBrightnessTimeMicros - float(BrightnessesReceived[0]) * MasterValue * BrightnessTimeStepNanosOverMaxMasterValue / 1000.0));
    //Serial.println(DimTimes[1]);
  }
}
  
void loop(){
  CurrentTime = micros();
  for(int Channel=0; Channel<nChannels; Channel++){ 
    long local_ZCTime = PhaseZCTimes[ChannelPhases[Channel]];
    //if(CurrentTime < PhaseZCTimes[ChannelPhases[Channel]]) {
    //  CurrentTime = micros();
    //}
    if((DimTimes[Channel] >= 0) && !PinFired[Channel] && (CurrentTime - PhaseZCTimes[ChannelPhases[Channel]]) > DimTimes[Channel]) {
      digitalWrite(ControlPins[Channel], HIGH);   // triac firing
      delayMicroseconds(5);
      digitalWrite(ControlPins[Channel], LOW);
      PinFired[Channel] = true;
      //Serial.println("Trigger");
      //Serial.println(42000000 + Channel + 100*(ChannelPhases[Channel] + 1));
      Serial.println(DimTimes[3]);
      //Serial.println(CurrentTime);
      //Serial.println(local_ZCTime);
      
      //Serial.println(PhaseZCTimes[ChannelPhases[Channel]]);
    }
  }
  if((CurrentTime - MasterLastRead) > MasterReadDeltaMicros){
    ReadMasterValue();
    MasterLastRead = CurrentTime;
  }
  //if((nLoops & 511) == 0) {
    //Serial.println((CurrentTime - tStart)/nLoops);
    //Serial.println(Failure/nLoops);
  //  nLoops = 0;
  //  Failure = 0;
  //  tStart = CurrentTime;
  //}
}
  
