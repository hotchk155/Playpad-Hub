
// inslude the SPI library:
#include <SPI.h>

void midi_out(byte *msg);
void midi_note(byte chan, byte note, byte vel);

#include "VNC2SPI.h"

#define MIDI_TICK     0xF8
#define MIDI_START    0xFA
#define MIDI_CONTINUE 0xFB
#define MIDI_STOP     0xFC


#define VNC2_SS_PIN 10
VNC2SPI VNC2(VNC2_SS_PIN);

#define LED_PIN  8

void midi_out(byte *msg)
{
  Serial.write(msg[0]);
  Serial.write(msg[1]);
  Serial.write(msg[2]);
  
}
void midi_note(byte chan, byte note, byte vel)
{
  Serial.write(0x90|chan);
  Serial.write(note & 0x7f);
  Serial.write(vel & 0x7f);
}



void setup() 
{
  // initialize SPI:
  Serial.begin(9600);
  Serial.println("start");
  VNC2.begin();
  pinMode(LED_PIN,OUTPUT);
}

int p;
#define SZ_BUFFER 100
void loop() 
{
  digitalWrite(LED_PIN,!!(p++&0x8000));
    
  byte buffer[SZ_BUFFER];   
  int count = VNC2.read(0,buffer,SZ_BUFFER);
  for(int i=0; i<count; ++i) {
    Serial.print(buffer[i], HEX);
    Serial.print(" ")
  }
    
}
