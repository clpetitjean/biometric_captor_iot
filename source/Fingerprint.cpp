/*!
 * @file Fingerprint.cpp
 * @mainpage Adafruit Fingerprint Sensor Library
 * @section intro_sec Introduction
 * This is a library for our optical Fingerprint sensor
 * Designed specifically to work with the Adafruit Fingerprint sensor
 * ---. http://www.adafruit.com/products/751
 * These displays use TTL Serial to communicate, 2 pins are required to
 * interface
 * Adafruit invests time and resources providing this open source code,
 * please support Adafruit and open-source hardware by purchasing
 * products from Adafruit!
 * @section author Author
 * Written by Limor Fried/Ladyada for Adafruit Industries.
 * @section license License
 * BSD license, all text above must be included in any redistribution
 *
 * STM32 adaptation by Christian Dupaty 03/2021
 * MbedOS v6 adaptation by Aurélian PINET 11/2024
 *
 */

#include "Fingerprint.h"  
#include "mbed.h"
#include <vector>


/*!
 * @brief Gets the command packet
 */
#define GET_CMD_PACKET(...)                                                    \
uint8_t data[] = {__VA_ARGS__};                                                \
 Fingerprint_Packet packet(FINGERPRINT_COMMANDPACKET, sizeof(data),data);      \
  writeStructuredPacket(packet);                                               \
  if (getStructuredPacket(&packet) != FINGERPRINT_OK)                          \
    return FINGERPRINT_PACKETRECIEVEERR;                                       \
  if (packet.type != FINGERPRINT_ACKPACKET)                                   \
    return FINGERPRINT_PACKETRECIEVEERR;

/*!
 * @brief Sends the command packet
 */
#define SEND_CMD_PACKET(...)                                                   \
  GET_CMD_PACKET(__VA_ARGS__);                                                 \
  return packet.data[0];

/***************************************************************************
 PUBLIC FUNCTIONS
 ***************************************************************************/


/**************************************************************************/
/*!
    @brief  Instantiates sensor with Software Serial
    @param  ss Pointer to SoftwareSerial object
    @param  password 32-bit integer password (default is 0)
*/
/**************************************************************************/
Fingerprint::Fingerprint(PinName serialTX, PinName serialRX, uint32_t password) :   R503Serial(serialTX, serialRX) 
{
  thePassword = password;
  theAddress = 0xFFFFFFFF;

  status_reg = 0x0; ///< The status register (set by getParameters)
  system_id = 0x0;  ///< The system identifier (set by getParameters)
  capacity = 64; ///< The fingerprint capacity (set by getParameters)
  security_level = 0; ///< The security level (set by getParameters)
  device_addr = 0xFFFFFFFF;      ///< The device address (set by getParameters)
  packet_len = 64;   ///< The max packet length (set by getParameters)
  baud_rate = 57600; ///< The UART baud rate (set by getParameters)
  // Init buffer de reception, les deux pointeurs egaux
  pe=buffUART;
  pl=buffUART;
  // active IT sur reception UART vers methode receiveUART
  R503Serial.attach(callback(this,&Fingerprint::receiveUART),UnbufferedSerial::RxIrq);
}

/**************************************************************************/
/*!
    @brief  Initializes serial interface and baud rate
    @param  baudrate Sensor's UART baud rate (usually 57600, 9600 or 115200)
*/
/**************************************************************************/
void Fingerprint::begin(uint32_t baudrate) {
     R503Serial.baud(baudrate);
}

/**************************************************************************/
/*!
    @brief  Verifies the sensors' access password (default password is
   0x0000000). A good way to also check if the sensors is active and responding
    @returns True if password is correct
*/
/**************************************************************************/
bool Fingerprint::verifyPassword(void) {
  return checkPassword() == FINGERPRINT_OK;
}

uint8_t Fingerprint::checkPassword(void) {
  GET_CMD_PACKET(FINGERPRINT_VERIFYPASSWORD, (uint8_t)(thePassword >> 24),(uint8_t)(thePassword >> 16), (uint8_t)(thePassword >> 8),(uint8_t)(thePassword & 0xFF));
  if (packet.data[0] == FINGERPRINT_OK)
    return FINGERPRINT_OK;
  else
    return FINGERPRINT_PACKETRECIEVEERR;
}

/**************************************************************************/
/*!
    @brief  Get the sensors parameters, fills in the member variables
    status_reg, system_id, capacity, security_level, device_addr, packet_len
    and baud_rate
    @returns True if password is correct
*/
/**************************************************************************/
uint8_t Fingerprint::getParameters(void) {
  GET_CMD_PACKET(FINGERPRINT_READSYSPARAM);

  status_reg = ((uint16_t)packet.data[1] << 8) | packet.data[2];
  system_id = ((uint16_t)packet.data[3] << 8) | packet.data[4];
  capacity = ((uint16_t)packet.data[5] << 8) | packet.data[6];
  security_level = ((uint16_t)packet.data[7] << 8) | packet.data[8];
  device_addr = ((uint32_t)packet.data[9] << 24) |
                ((uint32_t)packet.data[10] << 16) |
                ((uint32_t)packet.data[11] << 8) | (uint32_t)packet.data[12];
  packet_len = ((uint16_t)packet.data[13] << 8) | packet.data[14];
  if (packet_len == 0) {
    packet_len = 32;
  } else if (packet_len == 1) {
    packet_len = 64;
  } else if (packet_len == 2) {
    packet_len = 128;
  } else if (packet_len == 3) {
    packet_len = 256;
  }
  baud_rate = (((uint16_t)packet.data[15] << 8) | packet.data[16]) * 9600;

  return packet.data[0];
}

/**************************************************************************/
/*!
    @brief   Ask the sensor to take an image of the finger pressed on surface
    @returns <code>FINGERPRINT_OK</code> on success
    @returns <code>FINGERPRINT_NOFINGER</code> if no finger detected
    @returns <code>FINGERPRINT_PACKETRECIEVEERR</code> on communication error
    @returns <code>FINGERPRINT_IMAGEFAIL</code> on imaging error
*/
/**************************************************************************/
uint8_t Fingerprint::getImage(void) {
  SEND_CMD_PACKET(FINGERPRINT_GETIMAGE);
}

/**************************************************************************/
/*!
    @brief   Ask the sensor to convert image to feature template
    @param slot Location to place feature template (put one in 1 and another in
   2 for verification to create model)
    @returns <code>FINGERPRINT_OK</code> on success
    @returns <code>FINGERPRINT_IMAGEMESS</code> if image is too messy
    @returns <code>FINGERPRINT_PACKETRECIEVEERR</code> on communication error
    @returns <code>FINGERPRINT_FEATUREFAIL</code> on failure to identify
   fingerprint features
    @returns <code>FINGERPRINT_INVALIDIMAGE</code> on failure to identify
   fingerprint features
*/
uint8_t Fingerprint::image2Tz(uint8_t slot) {
  SEND_CMD_PACKET(FINGERPRINT_IMAGE2TZ, slot);
}

/**************************************************************************/
/*!
    @brief   Ask the sensor to take two print feature template and create a
   model
    @returns <code>FINGERPRINT_OK</code> on success
    @returns <code>FINGERPRINT_PACKETRECIEVEERR</code> on communication error
    @returns <code>FINGERPRINT_ENROLLMISMATCH</code> on mismatch of fingerprints
*/
uint8_t Fingerprint::createModel(void) {
  SEND_CMD_PACKET(FINGERPRINT_REGMODEL);
}

/**************************************************************************/
/*!
    @brief   Ask the sensor to store the calculated model for later matching
    @param   location The model location #
    @returns <code>FINGERPRINT_OK</code> on success
    @returns <code>FINGERPRINT_BADLOCATION</code> if the location is invalid
    @returns <code>FINGERPRINT_FLASHERR</code> if the model couldn't be written
   to flash memory
    @returns <code>FINGERPRINT_PACKETRECIEVEERR</code> on communication error
*/
uint8_t Fingerprint::storeModel(uint16_t location) {
  SEND_CMD_PACKET(FINGERPRINT_STORE, 0x01, (uint8_t)(location >> 8),
                  (uint8_t)(location & 0xFF));
}

/**************************************************************************/
/*!
    @brief   Ask the sensor to load a fingerprint model from flash into buffer 1
    @param   location The model location #
    @returns <code>FINGERPRINT_OK</code> on success
    @returns <code>FINGERPRINT_BADLOCATION</code> if the location is invalid
    @returns <code>FINGERPRINT_PACKETRECIEVEERR</code> on communication error
*/
uint8_t Fingerprint::loadModel(uint16_t location) {
  SEND_CMD_PACKET(FINGERPRINT_LOAD, 0x01, (uint8_t)(location >> 8),
                  (uint8_t)(location & 0xFF));
}

/**************************************************************************/
/*!
    @brief   Ask the sensor to transfer 256-byte fingerprint template from the
   buffer to the UART
    @returns <code>FINGERPRINT_OK</code> on success
    @returns <code>FINGERPRINT_PACKETRECIEVEERR</code> on communication error
*/
uint8_t Fingerprint::getModel(void) {
  SEND_CMD_PACKET(FINGERPRINT_UPLOAD, 0x01);
}

/**************************************************************************/
/*!
    @brief   Ask the sensor to delete a model in memory
    @param   location The model location #
    @returns <code>FINGERPRINT_OK</code> on success
    @returns <code>FINGERPRINT_BADLOCATION</code> if the location is invalid
    @returns <code>FINGERPRINT_FLASHERR</code> if the model couldn't be written
   to flash memory
    @returns <code>FINGERPRINT_PACKETRECIEVEERR</code> on communication error
*/
uint8_t Fingerprint::deleteModel(uint16_t location) {
  SEND_CMD_PACKET(FINGERPRINT_DELETE, (uint8_t)(location >> 8),
                  (uint8_t)(location & 0xFF), 0x00, 0x01);
}

/**************************************************************************/
/*!
    @brief   Ask the sensor to delete ALL models in memory
    @returns <code>FINGERPRINT_OK</code> on success
    @returns <code>FINGERPRINT_BADLOCATION</code> if the location is invalid
    @returns <code>FINGERPRINT_FLASHERR</code> if the model couldn't be written
   to flash memory
    @returns <code>FINGERPRINT_PACKETRECIEVEERR</code> on communication error
*/
uint8_t Fingerprint::emptyDatabase(void) {
  SEND_CMD_PACKET(FINGERPRINT_EMPTY);
}

/**************************************************************************/
/*!
    @brief   Ask the sensor to search the current slot 1 fingerprint features to
   match saved templates. The matching location is stored in <b>fingerID</b> and
   the matching confidence in <b>confidence</b>
    @returns <code>FINGERPRINT_OK</code> on fingerprint match success
    @returns <code>FINGERPRINT_NOTFOUND</code> no match made
    @returns <code>FINGERPRINT_PACKETRECIEVEERR</code> on communication error
*/
/**************************************************************************/
uint8_t Fingerprint::fingerFastSearch(void) {
  // high speed search of slot #1 starting at page 0x0000 and page #0x00A3
  GET_CMD_PACKET(FINGERPRINT_HISPEEDSEARCH, 0x01, 0x00, 0x00, 0x00, 0xA3);
  fingerID = 0xFFFF;
  confidence = 0xFFFF;

  fingerID = packet.data[1];
  fingerID <<= 8;
  fingerID |= packet.data[2];

  confidence = packet.data[3];
  confidence <<= 8;
  confidence |= packet.data[4];

  return packet.data[0];
}

/**************************************************************************/
/*!
    @brief   Control the built in LED
    @param on True if you want LED on, False to turn LED off
    @returns <code>FINGERPRINT_OK</code> on success
*/
/**************************************************************************/
uint8_t Fingerprint::LEDcontrol(bool on) {
  if (on) {
    SEND_CMD_PACKET(FINGERPRINT_LEDON);
  } else {
    SEND_CMD_PACKET(FINGERPRINT_LEDOFF);
  }
}

/**************************************************************************/
/*!
    @brief   Control the built in Aura LED (if exists). Check datasheet/manual
    for different colors and control codes available
    @param control The control code (e.g. breathing, full on)
    @param speed How fast to go through the breathing/blinking cycles
    @param coloridx What color to light the indicator
    @param count How many repeats of blinks/breathing cycles
    @returns <code>FINGERPRINT_OK</code> on fingerprint match success
    @returns <code>FINGERPRINT_NOTFOUND</code> no match made
    @returns <code>FINGERPRINT_PACKETRECIEVEERR</code> on communication error
*/
/**************************************************************************/
uint8_t Fingerprint::LEDcontrol(uint8_t control, uint8_t speed,
                                         uint8_t coloridx, uint8_t count) {
  SEND_CMD_PACKET(FINGERPRINT_AURALEDCONFIG, control, speed, coloridx, count);
}

/**************************************************************************/
/*!
    @brief   Ask the sensor to search the current slot fingerprint features to
   match saved templates. The matching location is stored in <b>fingerID</b> and
   the matching confidence in <b>confidence</b>
   @param slot The slot to use for the print search, defaults to 1
    @returns <code>FINGERPRINT_OK</code> on fingerprint match success
    @returns <code>FINGERPRINT_NOTFOUND</code> no match made
    @returns <code>FINGERPRINT_PACKETRECIEVEERR</code> on communication error
*/
/**************************************************************************/
uint8_t Fingerprint::fingerSearch(uint8_t slot) {
  // search of slot starting through the capacity
  GET_CMD_PACKET(FINGERPRINT_SEARCH,
                 slot,
                 static_cast<uint8_t>(0x00),
                 static_cast<uint8_t>(0x00),
                 static_cast<uint8_t>(capacity >> 8),
                 static_cast<uint8_t>(capacity & 0xFF));

  fingerID = 0xFFFF;
  confidence = 0xFFFF;

  fingerID = packet.data[1];
  fingerID <<= 8;
  fingerID |= packet.data[2];

  confidence = packet.data[3];
  confidence <<= 8;
  confidence |= packet.data[4];

  return packet.data[0];
}

/**************************************************************************/
/*!
    @brief   Ask the sensor for the number of templates stored in memory. The
   number is stored in <b>templateCount</b> on success.
    @returns <code>FINGERPRINT_OK</code> on success
    @returns <code>FINGERPRINT_PACKETRECIEVEERR</code> on communication error
*/
/**************************************************************************/
uint8_t Fingerprint::getTemplateCount(void) {
  GET_CMD_PACKET(FINGERPRINT_TEMPLATECOUNT);

  templateCount = packet.data[1];
  templateCount <<= 8;
  templateCount |= packet.data[2];

  return packet.data[0];
}

/**************************************************************************/
/*!
    @brief   Set the password on the sensor (future communication will require
   password verification so don't forget it!!!)
    @param   password 32-bit password code
    @returns <code>FINGERPRINT_OK</code> on success
    @returns <code>FINGERPRINT_PACKETRECIEVEERR</code> on communication error
*/
/**************************************************************************/
uint8_t Fingerprint::setPassword(uint32_t password) {
  SEND_CMD_PACKET(FINGERPRINT_SETPASSWORD,
                  static_cast<uint8_t>(password >> 24),
                  static_cast<uint8_t>(password >> 16),
                  static_cast<uint8_t>(password >> 8),
                  static_cast<uint8_t>(password));
}

/**************************************************************************/
/*!
    @brief   Helper function to process a packet and send it over UART to the
   sensor
    @param   packet A structure containing the bytes to transmit
*/
/**************************************************************************/

void Fingerprint::writeStructuredPacket(const Fingerprint_Packet &packet) 
{
  // Create a buffer to hold the structured packet data
  uint8_t buffer[9];  // 9 bytes: 2 for start_code, 4 for address, 1 for type, 2 for wire_length

  // Fill the buffer
  buffer[0] = static_cast<uint8_t>(packet.start_code >> 8);   // High byte of start_code
  buffer[1] = static_cast<uint8_t>(packet.start_code & 0xFF); // Low byte of start_code
  buffer[2] = packet.address[0];
  buffer[3] = packet.address[1];
  buffer[4] = packet.address[2];
  buffer[5] = packet.address[3];
  buffer[6] = packet.type;

  // Compute the wire_length and add it to the buffer
  uint16_t wire_length = packet.length + 2;
  buffer[7] = static_cast<uint8_t>(wire_length >> 8);        // High byte of wire_length
  buffer[8] = static_cast<uint8_t>(wire_length & 0xFF);      // Low byte of wire_length

  // Send the entire buffer in a single call
  R503Serial.write(buffer, sizeof(buffer));

  #ifdef FINGERPRINT_DEBUG
  printf("-> Send packet \n-> ");
  printf("0x%02X%02X ",(uint8_t)(packet.start_code >> 8),(uint8_t)(packet.start_code & 0xFF));
  printf(", 0x%02X ",packet.address[0]);
  printf(", 0x%02X ",packet.address[1]);
  printf(", 0x%02X ",packet.address[2]);
  printf(", 0x%02X ",packet.address[3]);
  printf(", 0x%02X ",packet.type);
  printf(", 0x%02X ",(uint8_t)(wire_length >> 8));
  printf(", 0x%02X \n-> Data  ",(uint8_t)(wire_length & 0xFF));
  #endif

  uint16_t sum = (static_cast<uint8_t>(wire_length >> 8)) + (static_cast<uint8_t>(wire_length & 0xFF)) + packet.type;

  // Buffer to hold packet data for a single write operation, if needed
  std::vector<uint8_t> dataBuffer;
  dataBuffer.reserve(packet.length);  // Reserve space for efficiency

  for (uint8_t i = 0; i < packet.length; i++) 
  {
    dataBuffer.push_back(packet.data[i]);  // Add to buffer
    sum += packet.data[i];

  #ifdef FINGERPRINT_DEBUG
    printf(", 0x%02X ", packet.data[i]);
  #endif
  }

  // Write all data in a single call if R503Serial.write() requires a buffer and length
  if (!dataBuffer.empty()) {
    R503Serial.write(dataBuffer.data(), dataBuffer.size());
  }
  //  #ifdef FINGERPRINT_DEBUG
  //  printf("\n-\n");
  //  #endif

  uint8_t sumBuffer[2];
  sumBuffer[0] = static_cast<uint8_t>(sum >> 8);     // High byte of sum
  sumBuffer[1] = static_cast<uint8_t>(sum & 0xFF);   // Low byte of sum

  // Send both bytes in a single write call
  R503Serial.write(sumBuffer, sizeof(sumBuffer));

#ifdef FINGERPRINT_DEBUG
  printf("-> chksum = 0x%02X%02X \n",(uint8_t)(sum >> 8),(uint8_t)(sum & 0xFF));
#endif

  return;
}

/**************************************************************************/
/*!
    @brief   Helper function to receive data over UART from the sensor and
   process it into a packet
    @param   packet A structure containing the bytes received
    @param   timeout how many milliseconds we're willing to wait
    @returns <code>FINGERPRINT_OK</code> on success
    @returns <code>FINGERPRINT_TIMEOUT</code> or
   <code>FINGERPRINT_BADPACKET</code> on failure
*/
/**************************************************************************/
uint8_t Fingerprint::getStructuredPacket(Fingerprint_Packet *packet, uint16_t timeout) 
{
  uint8_t byte;
  uint16_t idx = 0, timer = 0;

#ifdef FINGERPRINT_DEBUG
  printf("\n<----------------------packet reception\n<- ");
#endif

  while (true) 
  {
    while (pl==pe)   // rien n'est arrivé 
    {
      ThisThread::sleep_for(1ms);
      timer++;
      if (timer >= timeout) 
      {
        #ifdef FINGERPRINT_DEBUG
        printf("Timed out\n");
        #endif
        return FINGERPRINT_TIMEOUT;
      }
    }
    byte = readUARTbuff();

    #ifdef FINGERPRINT_DEBUG
    printf("0x%02X, ",byte);
    #endif
    switch (idx) 
    {
        case 0:
          if (byte != (FINGERPRINT_STARTCODE >> 8))
            continue;
          packet->start_code = (uint16_t)byte << 8;
          break;
        case 1:
          packet->start_code |= byte;
          if (packet->start_code != FINGERPRINT_STARTCODE)
            return FINGERPRINT_BADPACKET;
          break;
        case 2: // 4 bytes for adress
        case 3:  
        case 4:
        case 5:
          packet->address[idx - 2] = byte;
          break;
        case 6:
          packet->type = byte;
          break;
        case 7:
          packet->length = (uint16_t)byte << 8;
          break;
        case 8:
          packet->length |= byte;
          #ifdef FINGERPRINT_DEBUG
           printf("\n<- Data ");
          #endif
          break;
        default:
          packet->data[idx - 9] = byte;
          if ((idx - 8) == packet->length) 
          {
                #ifdef FINGERPRINT_DEBUG
                printf("\n<--------------------packet reception OK \n\n");
                #endif
                return FINGERPRINT_OK;
          }
          break;
    }
    idx++;
  }
  // Shouldn't get here so...
  // return FINGERPRINT_BADPACKET;
}

/*
Added by Christian Dupaty, STM32 adaptation
*/

/***************************************************************************
 PRIVATE FUNCTIONS
 ***************************************************************************/

void Fingerprint::receiveUART(void) {
    uint8_t c;
    while(!R503Serial.readable());
    R503Serial.read(&c, 1);
    *pe=c;
    pe++;
    if (pe>buffUART+sizeof(buffUART)) pe=buffUART;
}

uint8_t Fingerprint::readUARTbuff(void) {
   uint8_t c;
   c=*pl;
   pl++;
   if (pl>buffUART+sizeof(buffUART)) pl=buffUART;
   return c;
}
