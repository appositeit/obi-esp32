#include "OneWire_direct_gpio.h"
#include "OneWire_direct_regtype.h"


#ifndef OneWire_h
#define OneWire_h

#ifdef ARDUINO_ARCH_ESP32
// due to the dual core esp32, a critical section works better than disabling interrupts
#  define wire_noInterrupts() {portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;portENTER_CRITICAL(&mux);
#  define wire_interrupts() portEXIT_CRITICAL(&mux);}
#else
#  define wire_noInterrupts() noInterrupts();
#  define wire_interrupts() interrupts();
#endif

#ifdef __cplusplus

#include <stdint.h>

#if defined(__AVR__)
#include <util/crc16.h>
#endif

#if ARDUINO >= 100
#include <Arduino.h>       // for delayMicroseconds, digitalPinToBitMask, etc
#else
#include "WProgram.h"      // for delayMicroseconds
#include "pins_arduino.h"  // for digitalPinToBitMask, etc
#endif

template < int m_pin > class OneWire
{
  private:
    IO_REG_TYPE bitmask =PIN_TO_BITMASK(m_pin);;
    volatile IO_REG_TYPE *baseReg = PIN_TO_BASEREG(m_pin);
  public:

OneWire(){
	bitmask = PIN_TO_BITMASK(m_pin);
	baseReg = PIN_TO_BASEREG(m_pin);
    DIRECT_MODE_INPUT(baseReg, bitmask);
    DIRECT_WRITE_LOW(baseReg, bitmask);
}


// Perform the onewire reset function.  We will wait up to 250uS for
// the bus to come high, if it doesn't then it is broken or shorted
// and we return a 0;
//
// Returns 1 if a device asserted a presence pulse, 0 otherwise.
//
uint8_t reset(void)
{
	uint8_t r;
	uint8_t retries = 125;
	//wire_noInterrupts();
	DIRECT_MODE_INPUT(baseReg, bitmask);
	// wait until the wire is high... just in case
	do {
		if (--retries == 0) return 0;
		delayMicroseconds(2);
	} while ( !DIRECT_READ(baseReg, bitmask));

	DIRECT_WRITE_LOW(baseReg, bitmask);
	DIRECT_MODE_OUTPUT(baseReg, bitmask);	// drive output low
	delayMicroseconds(750);
	DIRECT_MODE_INPUT(baseReg, bitmask);	// allow it to float
	delayMicroseconds(70);
	r = !DIRECT_READ(baseReg, bitmask);
	delayMicroseconds(410);
    	//wire_interrupts();
	return r;
}

//
// Write a bit. Port and bit is used to cut lookup time and provide
// more certain timing.
//
void  write_bit(uint8_t v)
{
	//wire_noInterrupts();

	DIRECT_WRITE_LOW(baseReg, bitmask);
	DIRECT_MODE_OUTPUT(baseReg, bitmask);	// drive output low
    
	if (v & 1) {
		delayMicroseconds(12);
		DIRECT_WRITE_HIGH(baseReg, bitmask);	// drive output high
		delayMicroseconds(120);
	} else {
		delayMicroseconds(100);
		DIRECT_WRITE_HIGH(baseReg, bitmask);	// drive output high
		delayMicroseconds(30);
	}
  	//wire_interrupts();
}

//
// Read a bit. Port and bit is used to cut lookup time and provide
// more certain timing.
//
uint8_t read_bit(void)
{

	uint8_t r;

	//wire_noInterrupts();
	DIRECT_MODE_OUTPUT(baseReg, bitmask);
	DIRECT_WRITE_LOW(baseReg, bitmask);
	delayMicroseconds(10);
	DIRECT_MODE_INPUT(baseReg, bitmask);	// let pin float, pull up will raise
	delayMicroseconds(10);
	r = DIRECT_READ(baseReg, bitmask);
	delayMicroseconds(53);
        //wire_interrupts();
	return r;
}

//
// Write a byte. The writing code uses the active drivers to raise the
// pin high, if you need power after the write (e.g. DS18S20 in
// parasite power mode) then set 'power' to 1, otherwise the pin will
// go tri-state at the end of the write to avoid heating in a short or
// other mishap.
//
void write(uint8_t v) {
    delayMicroseconds(90);
    for (uint8_t writeNMask = 0x01; writeNMask; writeNMask <<= 1) {
	        OneWire::write_bit( (writeNMask & v)?1:0);
    }
}

void write_bytes(const uint8_t *buf, uint16_t count) {
  for (uint16_t i = 0 ; i < count ; i++)
    write(buf[i]);

    DIRECT_MODE_INPUT(baseReg, bitmask);
    DIRECT_WRITE_LOW(baseReg, bitmask);  
}

//
// Read a byte
//
uint8_t read() {
    uint8_t r = 0;
    delayMicroseconds(90);
    for (uint8_t readMask = 0x01; readMask; readMask <<= 1) {
	if ( OneWire::read_bit()) r |= readMask;
    }

    return r;
}

void read_bytes(uint8_t *buf, uint16_t count) {
  for (uint16_t i = 0 ; i < count ; i++)
    buf[i] = read();
}

//
// Do a ROM skip
//
void skip()
{
    write(0xCC);           // Skip ROM
};

};

// Prevent this name from leaking into Arduino sketches
#ifdef IO_REG_TYPE
#undef IO_REG_TYPE
#endif

#endif // __cplusplus
#endif // OneWire_h
