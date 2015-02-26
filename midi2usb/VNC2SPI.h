////////////////////////////////////////////////////////////////////////////
//
// Physical driver for SPI master to VNC2 chip in managed SPI mode
//
////////////////////////////////////////////////////////////////////////////
class VNC2SPI
{
  enum {
    VNC2_WRITE  = 0x00,
    VNC2_READ   = 0x10,
    VNC2_TXE    = 0x08,
    VNC2_RXF    = 0x04,
    VNC2_ACK    = 0x02
  };
  byte m_ssPin;
  byte m_addr;
public:  
  VNC2SPI(byte ssPin)
  {
    m_ssPin = ssPin;
    m_addr = 0;
  }


  ////////////////////////////////////////////////////////////////////////////
  // Set up SPI
  void begin()
  {
    pinMode(m_ssPin, OUTPUT);
    SPI.begin();
  }
  
  ////////////////////////////////////////////////////////////////////////////
  // master to slave write
  // 
  // MOSI  WCMD-DAT0-DAT1-DAT2-DAT3-
  // MISO  STAT-STAT-STAT-STAT-STAT-
  //
  // STAT should have RXF clear when slave has space to receive data
  //
  void write(byte addr, byte *pData, int iLen)
  {
    byte vnc2_status;
    int iIndex=0;
    while(iIndex<iLen)
    {    
        // wait until the slave reports space available in its
        // receive buffer
        for(;;) 
        {
          digitalWrite(m_ssPin, LOW);
          delayMicroseconds(1);
          vnc2_status = SPI.transfer((m_addr<<5)|VNC2_WRITE); //send write command
          if(!(vnc2_status & VNC2_RXF))
            break;  // slave buffer is not full, exit loop
          delayMicroseconds(1);
          digitalWrite(m_ssPin, HIGH);
          delay(5); // short wait before we check again
        }
          
        // Transfer bytes until the slave reports
        // full buffer
        while(iIndex<iLen)
        {  
          vnc2_status = SPI.transfer(pData[iIndex++]);
          if(vnc2_status & VNC2_RXF)
            break;        
        }
    }
    
    // read is complete
    delayMicroseconds(1);
    digitalWrite(m_ssPin, HIGH);
  }
  
  ////////////////////////////////////////////////////////////////////////////
  // master reads slave
  // 
  // MOSI  RCMD-------------------------------
  // MISO  STAT-DAT0-STAT-DAT1-STAT-DAT2-STAT-
  //
  // STAT should have ACK set, TXE is set if slave has nothing to send
  //
  int read(byte addr, byte *pData, int iLen)
  {
    int iIndex;
    
    // assert slave select
    digitalWrite(m_ssPin, LOW);
    delayMicroseconds(1);  
    
    byte vnc2_status = SPI.transfer((m_addr<<5)|VNC2_READ); // send read command
    
    // read bytes until we have enough, or slave has nothing more to send
    for(iIndex=0; iIndex<iLen && 
      VNC2_ACK == (vnc2_status&(VNC2_TXE|VNC2_ACK));
      iIndex++)
    {
      pData[iIndex] = SPI.transfer(0);  // read data byte
      vnc2_status = SPI.transfer(0);    // read status byte
    }
    
    // read complete
    delayMicroseconds(1);    
    digitalWrite(m_ssPin, HIGH);
    return iIndex;  
  }
};
