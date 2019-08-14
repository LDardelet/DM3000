#include "./DMXSerial.h"
#include <Wire.h>
#include <EEPROM.h>

/* 
We set here the list of possible modes. Probably changed through a button or something.
0 is for Feders input
1 is for DMX input
Using InterruptPins to set the mode seems like the most relevant option
*/
#define MANUAL_MODE 0
#define DMX_MODE 1

int SlaveI2CID = 1;

int nChannels = 16;

int MaxBrighness = 255; // Also change this value in SlaveArduino. If changed, multiplication factor has to be used with DMX
int ActiveChannelThreshold = 3;

byte Faders_ChannelsBrightness[16] = {0}; // Values retreived from faders read
byte Frame_ChannelsBrightness[16] = {0}; // Values retreived from frames in EEPROM
byte ChannelsBrightness[16] = {0}; // Actual value registered, choosen from all the previous ones

int MasterValue = 255; // Used only for DMX
// We store values tranformed from 0 to MaxBrightness 
int ChannelsLEDsPins[16] = {53, 23, 25, 27, 29, 31, 33, 35, 37, 39, 41, 43, 45, 47, 49, 51};

int FedersAnalogPins[16] =   {A14,A12,A15,A13 ,A11 ,A10, A9, A8, A7, A6, A5, A4, A3, A2, A1, A0}; // Fucked up assignments because fucked up wiring.
int AnalogMaxValue = 1023; // If changed, code must be adapted
int FedersStatusPin = 26;
// float AnalogMultiplier;

int DMXAddressPins[9] = {36, 38, 40, 42, 44, 46, 48, 50, 52};
int Selector0Pin = 34;
int Selector1Pin = 30;

int DMXRedStatusPin = 22;
int DMXGreenStatusPin = 24;

volatile int DMXAddress = 0;
// float DMXMultiplier;
volatile bool DMX_IDLE = true;

volatile int ControlMode = MANUAL_MODE;
bool ChangedValues = true;
bool ModifiedChannels[16] = {true};
bool ModifiedMaster = false;

int FrameAddressBase = -1;
int FrameAddressModifier = 0;
	
int SetModeFeders_iPin = 1; // Interrupt ID for button 0. Should associate a name with it when I knwo what it's gonna do
int SetModeDMX_iPin = 5; // Interrupt ID for button 0. Should associate a name with it when I knwo what it's gonna do
int LeftTriggerButton_iPin = 4; // Interrupt ID for button 0. Should associate a name with it when I knwo what it's gonna do

void setup() {
  // Initialize values for different control modes
  // AnalogMultiplier = (float)MaxBrighness / (float)AnalogMaxValue;
  // DMXMultiplier = MaxBrighness / (float)255;
  attachInterrupt(SetModeFeders_iPin, SetModeFeders, RISING);
  attachInterrupt(SetModeDMX_iPin, SetModeDMX, RISING);
  attachInterrupt(LeftTriggerButton_iPin, TriggerActionPlayPause, RISING);
  
  // Initialize Feders Pins Inputs
  for(int AddressBinPower=0; AddressBinPower<9; AddressBinPower++) {
    pinMode(DMXAddressPins[AddressBinPower], INPUT);
  }
  
  for(int nChannel = 0; nChannel < nChannels; nChannel ++){
    digitalWrite(ChannelsLEDsPins[nChannel], true);
    delay(100);
    digitalWrite(ChannelsLEDsPins[nChannel], false);
  }
  //Serial.begin(9600);
  
  // Initialize DMX system, and register address if default mode is DMX
  DMXSerial.init(DMXReceiver);
  if(ControlMode == DMX_MODE){
      RegisterDMXAddress();
  }
  
  // Initialize communication with slave arduino
  Update_Mode_LEDs();
  Wire.begin(); 
}

void SetModeFeders(){
  ControlMode = MANUAL_MODE;
  Update_Mode_LEDs();
}

void SetModeDMX() {
  ControlMode = DMX_MODE;
  DMX_IDLE = true;
  RegisterDMXAddress();
  Update_Mode_LEDs();
}

int RetreiveSelectorsPinsValues() {
  return digitalRead(Selector1Pin) << 1 | digitalRead(Selector0Pin);
}

int RetreiveAddressPinsValues(int nPins = 9, bool DigitalWrite = true){
  int Value = 0;
  for(int PinBinPower=0; PinBinPower<nPins; PinBinPower++) {
    bool BinValue = digitalRead(DMXAddressPins[PinBinPower]);
    if(DigitalWrite){
      digitalWrite(ChannelsLEDsPins[nChannels - 1 - PinBinPower], BinValue);
    }
    Value += BinValue * (1 << PinBinPower);
  }
  return Value;
}

void TriggerActionPlayPause(){
	if (ControlMode != MANUAL_MODE) return;
	
	bool TriggerMode = RetreiveSelectorsPinsValues() & B1;
	int LocalFrameAddressBase = RetreiveAddressPinsValues(8, false);
	if(FrameAddressBase == -1 || LocalFrameAddressBase != FrameAddressBase){
		FrameAddressBase = LocalFrameAddressBase;
		FrameAddressModifier = 0;
	}
	int FrameAddress = ((FrameAddressBase + FrameAddressModifier)%256)*16; // 256 Addresses, so we loop on them. 16 bytes per frame
	if(TriggerMode){ // Play mode
		for(int nChannel = 0; nChannel < nChannels; nChannel++){
			Frame_ChannelsBrightness[nChannel] = EEPROM.read(FrameAddress + nChannel);
			ChannelsBrightness[nChannel] = Frame_ChannelsBrightness[nChannel];
		}
	}
	else { // Record Mode
		for(int nChannel = 0; nChannel < nChannels; nChannel++){
			EEPROM.update(FrameAddress + nChannel, ChannelsBrightness[nChannel]);
		}
	}
	FrameAddressModifier++;
}

void RegisterDMXAddress(){
  DMXAddress = RetreiveAddressPinsValues() + 1;
  for(int Channel=0; Channel<nChannels; Channel++){
    DMXSerial.write(DMXAddress + Channel, 0);
  }
  delay(3000);
  for(int AddressBinPower=0; AddressBinPower<9; AddressBinPower++) {
    digitalWrite(ChannelsLEDsPins[nChannels - 1 - AddressBinPower], false);
  }
}

void ReadFedersValues(){
  for(int Channel=0; Channel<nChannels; Channel++){
    // int NewValue = (int)(analogRead(FedersAnalogPins[Channel]) * AnalogMultiplier);
    int NewValue = (AnalogMaxValue - analogRead(FedersAnalogPins[Channel])) >> 2;
    if(NewValue != Faders_ChannelsBrightness[Channel]){
      ChangedValues = true;
      ModifiedChannels[Channel] = true;
      Faders_ChannelsBrightness[Channel] = NewValue;
			ChannelsBrightness[Channel] = Faders_ChannelsBrightness[Channel];
    }
  }
}

void ReadDMXValues(){
  long LastFrame = DMXSerial.noDataSince();
  //unsigned long LastFrame = 0;
  if(LastFrame < 2000){
    if(DMX_IDLE){
      DMX_IDLE = false;
      Update_Mode_LEDs();
    }
    for(int Channel=0; Channel<nChannels; Channel++){
      int NewValue = DMXSerial.read(DMXAddress + Channel);
      //int NewValue = 0;
      if(NewValue != ChannelsBrightness[Channel]){
        ChangedValues = true;
        ModifiedChannels[Channel] = true;
        ChannelsBrightness[Channel] = NewValue;
      }
    }
    int NewMasterValue = DMXSerial.read(DMXAddress + nChannels);
    if(NewMasterValue != MasterValue){
      ChangedValues = true;
      ModifiedMaster = true;
      MasterValue = NewMasterValue;
    }
  }
  else{
    if(!DMX_IDLE){
      DMX_IDLE = true;
      Update_Mode_LEDs();
    }
  }
}

void Update_Mode_LEDs(){
  digitalWrite(DMXRedStatusPin, LOW);
  digitalWrite(DMXGreenStatusPin, LOW);
  digitalWrite(FedersStatusPin, LOW);
 
  if(ControlMode == MANUAL_MODE){
    digitalWrite(FedersStatusPin, HIGH);
  }
  else if(ControlMode == DMX_MODE) {
    digitalWrite(DMXGreenStatusPin, HIGH);
    if(DMX_IDLE) {
      digitalWrite(DMXRedStatusPin, HIGH);
    }
  } 
}

void loop() {
  if(ControlMode == MANUAL_MODE){
    ReadFedersValues();
  }
  else if(ControlMode == DMX_MODE){
    ReadDMXValues();
  }
  //RegisterDMXAddress();
  //Serial.println(DMXAddress);
  //Serial.println(digitalRead(Selector0Pin));
  //Serial.println(digitalRead(Selector1Pin));
  //Serial.println(ChannelsBrightness[0]);
  //Serial.println(ChannelsBrightness[1]);
  if(ChangedValues){
    ChangedValues = false;
    Wire.beginTransmission(SlaveI2CID);
    
    if(ControlMode == DMX_MODE){
      if(ModifiedMaster){
        ModifiedMaster = false;
        Wire.write((B01 << 6) + (MasterValue >> 6));
        Wire.write((1<<7) + (MasterValue & B111111));
      }
    }
    
    for(int Channel=0; Channel<nChannels; Channel++){
      if(ModifiedChannels[Channel]){
        ModifiedChannels[Channel] = false;
        //Serial.println("Sending DIM command");
        Wire.write((Channel << 2) + (ChannelsBrightness[Channel] >> 6));
        Wire.write((1<<7) + (ChannelsBrightness[Channel] & B111111));
        digitalWrite(ChannelsLEDsPins[Channel], (ChannelsBrightness[Channel] > ActiveChannelThreshold));
      }
    }
    Wire.endTransmission();
  }
}
