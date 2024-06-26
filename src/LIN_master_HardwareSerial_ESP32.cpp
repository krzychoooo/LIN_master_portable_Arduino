/**
  \file     LIN_master_HardwareSerial_ESP32.cpp
  \brief    LIN master emulation library using a HardwareSerial interface of ESP32
  \details  This library provides a master node emulation for a LIN bus via a HardwareSerial interface of ESP32.
            For an explanation of the LIN bus and protocol e.g. see https://en.wikipedia.org/wiki/Local_Interconnect_Network
  \note     Serial.available() has >1ms delay, likely due to 2nd Core implementation, see https://esp32.com/viewtopic.php?p=65158. Use BREAK duration instead
  \author   Georg Icking-Konert
*/

// assert ESP32 platform
#if defined(ARDUINO_ARCH_ESP32)

// include files
#include "Arduino.h"
#include "LIN_master_HardwareSerial_ESP32.h"


/**
  \brief      Send LIN break
  \details    Send LIN break (=16bit low)
  \return     current state of LIN state machine
*/
LIN_Master::state_t LIN_Master_HardwareSerial_ESP32::_sendBreak(void)
{
  // print debug message
  #if defined(LIN_DEBUG_SERIAL) && (LIN_DEBUG_LEVEL >= 2)
    LIN_DEBUG_SERIAL.println("LIN_Master_HardwareSerial::_sendBreak()");
  #endif
  
  // if state is wrong, exit immediately
  if (this->state != LIN_Master::STATE_IDLE)
  {
    this->error = (LIN_Master::error_t) ((int) this->error | (int) LIN_Master::ERROR_STATE);
    this->state = LIN_Master::STATE_DONE;
    return this->state;
  }

  // empty buffers, just in case...
  this->pSerial->flush();
  while (this->pSerial->available())
    this->pSerial->read();
 
  // set half baudrate for BREAK
  this->pSerial->updateBaudRate(this->baudrate >> 1);

  // send BREAK (>=13 bit low)
  this->pSerial->write(bufTx[0]);

  // store starting time
  timeStartBreak = micros();

  // progress state
  this->state = LIN_Master::STATE_BREAK;

  // return state
  return this->state;

} // LIN_Master_HardwareSerial_ESP32::_sendBreak()



/**
  \brief      Send LIN bytes (request frame: SYNC+ID+DATA[]+CHK; response frame: SYNC+ID)
  \details    Send LIN bytes (request frame: SYNC+ID+DATA[]+CHK; response frame: SYNC+ID)
  \return     current state of LIN state machine
*/
LIN_Master::state_t LIN_Master_HardwareSerial_ESP32::_sendFrame(void)
{
  // print debug message
  #if defined(LIN_DEBUG_SERIAL) && (LIN_DEBUG_LEVEL >= 2)
    LIN_DEBUG_SERIAL.println("LIN_Master_HardwareSerial::_sendFrame()");
  #endif
    
  // if state is wrong, exit immediately
  if (this->state != LIN_Master::STATE_BREAK)
  {
    this->error = (LIN_Master::error_t) ((int) this->error | (int) LIN_Master::ERROR_STATE);
    this->state = LIN_Master::STATE_DONE;
    return this->state;
  }

  // Serial.available() has >1ms delay -> use duration of BREAK instead
  if ((micros() - timeStartBreak) > (timePerByte << 1))
  {
    // skip reading Rx now (is not yet in buffer)

    // restore nominal baudrate. Apparently this is ok for BREAK
    this->pSerial->updateBaudRate(this->baudrate);

    // send rest of frame (request frame: SYNC+ID+DATA[]+CHK; response frame: SYNC+ID)
    this->pSerial->write(this->bufTx+1, this->lenTx-1);

    // progress state
    this->state = LIN_Master::STATE_BODY;

  } // BREAK echo received
  
  // no byte(s) received
  else
  {
    // check for timeout
    if (micros() - this->timeStart > this->timeMax)
    {
      this->error = (LIN_Master::error_t) ((int) this->error | (int) LIN_Master::ERROR_TIMEOUT);
      this->state = LIN_Master::STATE_DONE;
    }

  } // no byte(s) received
  
  // return state
  return this->state;

} // LIN_Master_HardwareSerial_ESP32::_sendFrame()



/**
  \brief      Receive and check LIN frame
  \details    Receive and check LIN frame (request frame: check echo; response frame: check header echo & checksum). Here dummy!
  \return     current state of LIN state machine
*/
LIN_Master::state_t LIN_Master_HardwareSerial_ESP32::_receiveFrame(void)
{
  // print debug message
  #if defined(LIN_DEBUG_SERIAL) && (LIN_DEBUG_LEVEL >= 2)
    LIN_DEBUG_SERIAL.println("LIN_Master_HardwareSerial_ESP32::_receiveFrame()");
  #endif
    
  // if state is wrong, exit immediately
  if (this->state != LIN_Master::STATE_BODY)
  {
    this->error = (LIN_Master::error_t) ((int) this->error | (int) LIN_Master::ERROR_STATE);
    this->state = LIN_Master::STATE_DONE;
    return this->state;
  }

  // frame body received. Here, need to read BREAK as well due to delay of Serial.available()
  if (this->pSerial->available() >= this->lenRx)
  {
    // store bytes in Rx
    this->pSerial->readBytes(this->bufRx, this->lenRx);

    // check frame for errors
    this->error = (LIN_Master::error_t) ((int) this->error | (int) this->_checkFrame());

    // progress state
    this->state = LIN_Master::STATE_DONE;

  } // frame body received
  
  // frame body received not yet received
  else
  {
    // check for timeout
    if (micros() - this->timeStart > this->timeMax)
    {
      this->error = (LIN_Master::error_t) ((int) this->error | (int) LIN_Master::ERROR_TIMEOUT);
      this->state = LIN_Master::STATE_DONE;
    }

  } // not enough bytes received
  
  // return state
  return this->state;

} // LIN_Master_HardwareSerial_ESP32::_receiveFrame()



/**
  \brief      Constructor for LIN node class using ESP32 HardwareSerial
  \details    Constructor for LIN node class for using ESP32 HardwareSerial. Inherit all methods from LIN_Master_HardwareSerial, only different constructor
  \param[in]  Interface     serial interface for LIN
  \param[in]  PinRx         GPIO used for reception
  \param[in]  PinTx         GPIO used for transmission
  \param[in]  Baudrate    communication speed [Baud]
  \param[in]  NameLIN     LIN node name 
*/
LIN_Master_HardwareSerial_ESP32::LIN_Master_HardwareSerial_ESP32(HardwareSerial &Interface, uint8_t PinRx, uint8_t PinTx, uint8_t pinLedRx, uint8_t pinLedTx, const char NameLIN[] = "") : LIN_Master_HardwareSerial::LIN_Master_HardwareSerial(Interface, NameLIN)
{
  // store parameters in class variables
  this->pinRx      = PinRx;                                   // receive pin
  this->pinTx      = PinTx;                                   // transmit pin
  this->pinLedRx   = pinLedRx;
  this->pinLedTx   = pinLedTx;
  // must not open connection here, else system resets

} // LIN_Master_HardwareSerial_ESP32::LIN_Master_HardwareSerial_ESP32()



/**
  \brief      Open serial interface
  \details    Open serial interface with specified baudrate. Here dummy!
  \param[in]  Baudrate    communication speed [Baud]
*/
void LIN_Master_HardwareSerial_ESP32::begin(uint16_t Baudrate)
{
  // call base class method
  LIN_Master::begin(Baudrate);  

  // open serial interface incl. used pins
  this->pSerial->end();
  this->pSerial->begin(baudrate, SERIAL_8N1, pinRx, pinTx);
  while(!(*(this->pSerial)));

} // LIN_Master_HardwareSerial_ESP32::begin()

void LIN_Master_HardwareSerial_ESP32::ledTx(uint8_t value){
  digitalWrite(this->pinLedTx, value);
}
void LIN_Master_HardwareSerial_ESP32::ledRx(uint8_t value){
  digitalWrite(this->pinLedRx, value);
}


#endif // ARDUINO_ARCH_ESP32

/*-----------------------------------------------------------------------------
    END OF FILE
-----------------------------------------------------------------------------*/
