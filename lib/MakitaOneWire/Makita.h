#include "OneWire_direct_gpio.h"
#include "OneWire_direct_regtype.h"


#ifdef ARDUINO_ARCH_ESP32
// due to the dual core esp32, a critical section works better than disabling
// interrupts
#define wire_noInterrupts()                          \
  {                                                  \
    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED; \
    portENTER_CRITICAL(&mux);
#define wire_interrupts()  \
  portEXIT_CRITICAL(&mux); \
  }
// for info on this, search "IRAM_ATTR" at
// https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/general-notes.html
#else
#define wire_noInterrupts() noInterrupts();
#define wire_interrupts() interrupts();
#endif


#ifndef MAKITA_H
#define MAKITA_H

#define SWAP_NIBBLES(x) ((x & 0x0F) << 4 | (x & 0xF0) >> 4)



template < int m_pin > class Makita {
       public:
       Makita(){
        init();
       }

       void init(){
          m_timestamp=0;
          enable_extended=false;
          set_health(100);
          set_overload(0);
          set_overdischarge(0);
          set_cell_temperature(20);
          reset_voltages();
          reset_rom();

          bitmask = PIN_TO_BITMASK(m_pin);
          baseReg = PIN_TO_BASEREG(m_pin);
          DIRECT_WRITE_LOW(baseReg, bitmask);
          DIRECT_MODE_INPUT(baseReg, bitmask); 
       }

 
       //resets ROM to default values taken from a chinese battery
       void reset_rom(){
          uint8_t empty_rom[32]={ 0xf1, 0x36, 0xb6, 0xc3, 0x18, 0x58, 0x00, 0x00, 0x94, 0x94, 0x40, 0x21, 0x01, 0x80, 0x02, 0x0a, 0x43, 0xd0, 0x8e, 0x1b, 0xf0, 0x66, 0x00, 0x03, 0x02, 0x02, 0x00, 0x00, 0x00, 0x10, 0x02, 0x73};
          memcpy(m_rom,empty_rom,32);
       }

       void reset_voltages(){
         set_cell_voltage(0,4.0f);
         set_cell_voltage(1,4.0f);
         set_cell_voltage(2,4.0f);
         set_cell_voltage(3,4.0f);
         set_cell_voltage(4,4.0f);
         set_cell_voltage(5,4.0f);
       }
 
       /**
        * Check for reset signal. Return true(1) if reset was detected and
        * presence was signaled, otherwise false(0).
        * @return true(1) on reset and presence, otherwise false(0).
        */
       bool reset() {
         wire_noInterrupts();

          DIRECT_MODE_INPUT(baseReg, bitmask); 
         // Check reset start
         if (m_timestamp == 0) {
           if (!DIRECT_READ(baseReg, bitmask)) m_timestamp = micros();
           return (false);
         }
 
         // Check reset pulse stop
         if (!DIRECT_READ(baseReg, bitmask)) return (false);
 
         // Check reset pulse width
         if (micros() - m_timestamp < 300) {
           m_timestamp = 0;
           return (false);
         }
 
         // Generate presence signal
         delayMicroseconds(35);
          DIRECT_MODE_OUTPUT(baseReg, bitmask); 
         delayMicroseconds(100);
          DIRECT_MODE_INPUT(baseReg, bitmask); 

         // Wait for possible presence signals from other devices
         while (!DIRECT_READ(baseReg, bitmask));
         m_timestamp = 0;
 
 
         delayMicroseconds(200);

          wire_interrupts();

         return (true);
       }
 

       uint8_t read(uint8_t bits = 8) {
         uint8_t bitMask;
         uint8_t r = 0;
          wire_noInterrupts();


        DIRECT_MODE_INPUT(baseReg, bitmask); 
 
         noInterrupts();
 
         for (bitMask = 0x01; bitMask; bitMask <<= 1) {
           for (int tries = 4096; DIRECT_READ(baseReg, bitmask) && tries > 0; tries--) ;
           // Delay to sample bit value
           delayMicroseconds(20);
           if (DIRECT_READ(baseReg, bitmask)) r |= bitMask;
           delayMicroseconds(80);
         }
 
         wire_interrupts();
         return r;
       }
 
       void read(void * buf, size_t count) {
         uint8_t * bp = (uint8_t * ) buf;
         do {
           uint8_t value = read();
           * bp++ = value;
         } while (--count);
        }
 


       void write(uint8_t value, uint8_t bits = 8) {
         do {
         wire_noInterrupts();
         DIRECT_MODE_INPUT(baseReg, bitmask); 
           // Wait for bit start
           for (uint16_t tries = 4096; digitalRead(m_pin) && tries > 0; tries--) ;

           if ((value & 0x01) == 0) {
             DIRECT_MODE_OUTPUT(baseReg, bitmask); 
             DIRECT_WRITE_LOW(baseReg, bitmask);
             delayMicroseconds(33);
             DIRECT_MODE_INPUT(baseReg, bitmask); 
            } else {
             delayMicroseconds(33);
           }
           value >>= 1;

           // Wait for end
        //   pinMode(m_pin,INPUT);
           for (uint16_t tries = 4096; !digitalRead(m_pin) && tries > 0; tries--) ;

         } while (--bits);

         wire_interrupts();
       }

       void write_u16(uint16_t value){
        write(value&0xff);
        write(value>>8);
       }
 
       void write(const void * buf, size_t count) {
         // Write bytes and calculate cyclic redundancy check-sum
         const uint8_t * bp = (const uint8_t * ) buf;
         do {
           delayMicroseconds(40);
           uint8_t value = * bp++;
           write(value);
         } while (--count);
       }

       //sets the overload value
       void set_overload(uint16_t value){
          uint8_t overload_prec=value>75?75:value;//only goes up to 75 for older batteries
          uint8_t overload_div5=overload_prec/5;
          overload_div5|=overload_div5?0x20:0x0;
          m_rom[25]=SWAP_NIBBLES(overload_div5); //old protocol puts it in rom here
          overload=value;
       }


       //sets the overdischarge value
       void set_overdischarge(uint16_t value){
        uint8_t overdischarge_perc=value>80?80:value;//only goes up to 75
        uint8_t overdischarge_div=overdischarge_perc/5.33f;
        overdischarge_div= ~overdischarge_div;//somehow this parameter is an inverse
        overdischarge_div&=0xF;
        
        //bit 1 to 3 are overdischarge disable flags ( 0x0f )
        m_rom[24]=(overdischarge_div&0x0f)<<4 | (overdischarge_perc?1:3); //old protocol puts it in ROM here
        overdischarge=value;
       }

       void set_health(uint16_t value){
        health=value;
       }


       //sets the cycle count
       //for older batteries health = 100-(value/8.96)
       //health declines 1 bar per 224 cycles
       void set_cycle_count(uint16_t value){
        m_rom[26] =SWAP_NIBBLES(value>>8);
        m_rom[27] =SWAP_NIBBLES(value&0xff) ;
       }

       void set_extended(bool value){
        enable_extended=value;
       }

       void set_error(uint8_t value){
        m_rom[20]|=value&0x0f; 
       }

       void set_cell_temperature(float value){
        cell_temperature=value;
       }

       void set_cell_voltage(uint8_t cell, float value){
             if(cell>5)return;
             double max_v=0;
             double min_v=0;

             cell_voltages[cell]=value;
             pack_voltage=0;

             for(int i=0;i<5;i++){
                max_v=max(cell_voltages[i],max_v);
                min_v=min(cell_voltages[i],min_v);
                pack_voltage+=cell_voltages[i];
             }

             voltage_difference=abs(max_v-min_v);

             //set an error if the voltages aren't correct
             if(value<3.0f || voltage_difference)set_error(1);
       }

       //makita command process, please call in a continuous loop
       bool rom_command() {
         // Wait for reset
         byte buff[4];
 
         if (!reset()) return (false);

         int r = read();

         // Standard ROM commands
         if (r == 0x33) {

          
          uint8_t idbytes[] = { 0x16,0x07,0x13,0x64,0x14,0x0a,0x0e,0x69};
          write((void * ) idbytes, 8);

          if(read()!=0xF0) return;//only support this command for now
          read();
          write(m_rom, 32);
          return false;
        }

        if(enable_extended == false)return false;

         //extended protocol for newer batteries
         if (r == 0xCC) {
           r = read();
 
           if (r == 0xFF) { //no command recieved
             return false;
           }
 
           if (r == 0xDC) {
            uint8_t dcbytes[17] = { 0x1A, 0x01,0x0A,0x00,0x02,0x03,0x00,0x08,0x24,0x19,0x2A,0x3D,0x05,0x00,0x00,0x00,0x06};
             r = read();
             write(dcbytes, 17);
             return true;
           }
 
           if (r == 0xD4) {
             r = read();
 
             if (r == 0x50) {
               read(buff, 2);
               write(0x55); 
               write(10 + round(health/14) ); 
               write(0x06);
               return false;
             }
 
             if (r == 0x8D) {
              int overload_div=round(overload/2);

               read(buff, 2);
               write(0x00);
               write(0x00);
               write(0xFE);
               write(0x00);
               write(0x00); 
               write( (overload_div&0xF) << 4 );
               write( (overload_div&0xF0) >> 4 );
               write(0x06);
 
               return false;
             }
 
             if (r == 0xBA) {
              int overdischarge_div=round(overdischarge/2);
               read(buff, 2);
               write(overdischarge_div); //overdischarge 1 is 5%
               write(0x06);
             }
 
           }
 
           if (r == 0xD6) {
             r = read();
             read(buff, 1);
 
             if (r == 0x09) {
               write(0x00);
               write(0x00); //ff = 100, overdischarge 
               write(0x06);
               return true;
             }
 
             if (r == 0x38) {
               write(0x00);
               write(0x00);
               write(0xf0);
               write(0x06);
               return true;
             }
 
             if (r == 0x5B) {
               read(buff, 1);
               write(0x00); //overload
               write(0x00); //overload
               write(0x00); //overload ( lsb -1 )
               write(0x00); //overload ( lsb)
               write(0x06);
             }
 
             return false;
           }
 
           if (r == 0xD7) {
             r = read();
 
             if (r == 0x00) { //battery voltages
               read(buff, 2);
               write_u16(pack_voltage*1000.0f);
               for(int i=0;i<5;i++){
                write_u16(cell_voltages[i]*1000.0f);
               }
               write(0x06);
               return false;
             }
 
             if (r == 0x0E) {
               read(buff, 2);
               uint16_t tmp=(cell_temperature+273.15f)*10;
               write_u16(cell_temperature);
               write(0x06);
               return false;
             }
 
             if (r == 0x19) {
               read(buff, 2);
               write(0xd1);
               write(0xd5);
               write(0xA0);
               write(0x00);
               write(0x06);
             }
           }
 
           if (r == 0xD9) {
             write(0x96);
             write(0xA5);
             write(0x06);
           }
 
         }
 

 
         //ignore other commands
         return (false);
       }
       

 
       private:
        IO_REG_TYPE bitmask;
       volatile IO_REG_TYPE *baseReg;
       uint32_t m_timestamp=0;
       bool enable_extended=false;
       uint16_t overload=0;
       uint16_t overdischarge=0;
       uint16_t health=100;
       float pack_voltage=0;
       float cell_temperature=0;
       float voltage_difference=0;
       uint8_t m_rom[32];
       float cell_voltages[5];
      };
#endif
