#include <Wire.h>
#include <Adafruit_MCP23017.h>

// YM3812 VCO for eurorack modular system

// originally from https://github.com/a1k0n/opl2/blob/master/arduino/ym3812/ym3812.ino

// pin map:
// teensy | YM3812
// PORTD = D0..D7  (pins 2, 14, 7, 8, 6, 20, 21, 5)
// pin 13 = A0
// pin 15 = /WR
// pin 16 -> /CS
// (pullup) -> /RD
// (pulldown) -> /IC

static const byte D2 = 2;
static const byte D3 = 3;
static const byte D4 = 4;
static const byte D5 = 5;
static const byte D6 = 6;
static const byte D7 = 7;
static const byte D8 = 8;
static const byte D9 = 9;
static const byte D10 = 10;
static const byte D11 = 11;
static const byte D12 = 12;

// bus pins for programming YM3812
static const int pin_a0 = D12;
static const int pin_wr = D11;
static const int pin_cs = D10;

// /IC aka reset pin
static const byte pin_ic = A0;

uint16_t notetbl[] = {
  //  C     C#      D       D#      E       F       F#      G       G#      A       A#       B
  0x0157, 0x016b, 0x0181, 0x0198, 0x01b0, 0x01ca, 0x01e5, 0x0202, 0x0220, 0x0241, 0x0263, 0x0287, 
  0x0557, 0x056b, 0x0581, 0x0598, 0x05b0, 0x05ca, 0x05e5, 0x0602, 0x0620, 0x0641, 0x0663, 0x0687,
  0x0957, 0x096b, 0x0981, 0x0998, 0x09b0, 0x09ca, 0x09e5, 0x0a02, 0x0a20, 0x0a41, 0x0a63, 0x0a87,
  0x0d57, 0x0d6b, 0x0d81, 0x0d98, 0x0db0, 0x0dca, 0x0de5, 0x0e02, 0x0e20, 0x0e41, 0x0e63, 0x0e87,
  0x1157, 0x116b, 0x1181, 0x1198, 0x11b0, 0x11ca, 0x11e5, 0x1202, 0x1220, 0x1241, 0x1263, 0x1287,
  0x1557, 0x156b, 0x1581, 0x1598, 0x15b0, 0x15ca, 0x15e5, 0x1602, 0x1620, 0x1641, 0x1663, 0x1687,
  0x1957, 0x196b, 0x1981, 0x1998, 0x19b0, 0x19ca, 0x19e5, 0x1a02, 0x1a20, 0x1a41, 0x1a63, 0x1a87,
  0x1d57, 0x1d6b, 0x1d81, 0x1d98, 0x1db0, 0x1dca, 0x1de5, 0x1e02, 0x1e20, 0x1e41, 0x1e63, 0x1e87
};

const byte voice_register_offsets[] = {
  0x00, 0x01, 0x02, 0x08, 0x09, 0x0A, 0x10, 0x11, 0x12
};

byte chord[] = { 0, 0, 0, 0, 0, 0, 0 };
byte chord_mode = 0;

byte note_num = 0;
byte vca = 0;
byte mult = 0;
byte fb = 0;

byte wf1 = 0;
byte wf2 = 0;
byte switches = 0;

byte algo = 0;

void ym3812_write1(uint8_t addr, uint8_t data) {
  // write reg
  digitalWrite(pin_a0, addr);  // set a0
  digitalWrite(pin_cs, 0);  // assert /CS

  // our 8 bit bus is spread out across some random arduino DIO pins
  digitalWrite(D2, (data >> 0 ) & 0x01);
  digitalWrite(D3, (data >> 1 ) & 0x01);
  digitalWrite(D4, (data >> 2 ) & 0x01);
  digitalWrite(D5, (data >> 3 ) & 0x01);
  digitalWrite(D6, (data >> 4 ) & 0x01);
  digitalWrite(D7, (data >> 5 ) & 0x01);
  digitalWrite(D8, (data >> 6 ) & 0x01);
  digitalWrite(D9, (data >> 7 ) & 0x01);
  
  digitalWrite(pin_wr, 0);  // assert /WR
  // 100ns
  delayMicroseconds(0);  // FIXME
  digitalWrite(pin_wr, 1);  // unassert /WR
  digitalWrite(pin_cs, 1);  // unassert /CS
}

void ym3812_write(uint8_t reg, uint8_t val) {
  ym3812_write1(0, reg);
  delayMicroseconds(5);  // orig 4
  ym3812_write1(1, val);
  delayMicroseconds(24); // orig 23
}

Adafruit_MCP23017 mcp;
const byte lcd_e = 13;  // gpb5
const byte lcd_rw = 14; // gpb6
const byte lcd_rs = 15; // gpb7
void init_lcd() {
  mcp.begin();
  for(byte i = 0; i < 8; i += 1) {
    mcp.pinMode(i+8, OUTPUT); // all of portB to outputs
    mcp.pinMode(i, INPUT); // all of portA to inputs
  }
  mcp.pinMode(6, OUTPUT); // actually we want 6 and 7 to be outputs
  mcp.pinMode(7, OUTPUT);
  mcp.digitalWrite(6, false); // all backlights on
  mcp.digitalWrite(7, false);
  mcp.digitalWrite(8, false);
  mcp.pullUp(0, true); // we want pullups on all five switches
  mcp.pullUp(1, true);
  mcp.pullUp(2, true);
  mcp.pullUp(3, true);
  mcp.pullUp(4, true);
  
  mcp.digitalWrite(lcd_rw, false); // not used
  mcp.digitalWrite(lcd_e, false); // default enable to low

  // get us into 4bit mode for sure
  delay(200);  // "more than 100ms"
  mcp.digitalWrite(lcd_rs, false); // command register
  lcd_half_write( 0b00110000);  //0x30);
  delay(6); // "more than 4.1 ms"
  lcd_half_write( 0b00110000);  //0x30);
  delay(2); // "more than 100us"
  lcd_half_write( 0b00110000);  //0x30);
  delay(2); // "more than 100us"
  lcd_half_write( 0b00100000); //0x20); // initial "function set" to change interface to 4bit mode
  delay(2); // "more than 100us"
  lcd_write(/*0x28*/ 0b00101000); // N=1, F=0: two logical lines, 5x7 font
  lcd_write(/*0x08*/ 0b00001000); // display on/off, "D=C=B=0"
  lcd_write(/*0x01*/ 0b00000001); // clear display
  delay(5); // "more than 3ms"
  lcd_write(/*0x06*/ 0b00000110); // I/D=1, S=0: move cursor to left, no shifting
  lcd_write(/*0x0C*/ 0b00001100); // display on/off, "D=1, C=B=0", display on, cursor off, no blinking
  lcd_write(0x80); // "move cursor to beginning of first line"
}

const byte lcd_db7 = 9; // gpb1
const byte lcd_db6 = 10;// gpb2
const byte lcd_db5 = 11;// gpb3
const byte lcd_db4 = 12;// gpb4
void lcd_write( byte data) {
  lcd_half_write(data & 0xF0);
  lcd_half_write(data << 4);
}

void lcd_half_write( byte data) {
  // we're only writing the high nibble because we've got a 4bit physical interface connected to an LCD in 8bit mode
  // high nibble
  delay(1);
  mcp.digitalWrite(lcd_db4, bitRead(data, 4));
  mcp.digitalWrite(lcd_db5, bitRead(data, 5));
  mcp.digitalWrite(lcd_db6, bitRead(data, 6));
  mcp.digitalWrite(lcd_db7, bitRead(data, 7));
  delay(1);
  mcp.digitalWrite(lcd_e, true);
  delay(1);
  mcp.digitalWrite(lcd_e, false);  // data read on falling edge
  delay(1);
}

void write_one_digit(byte value) {
  lcd_write('0'+value);
}

void write_two_digit(byte value) {
  write_one_digit(value / 10);
  write_one_digit(value % 10);
}

void write_to_lcd()
{
  mcp.digitalWrite(lcd_rs, false); // command register
  lcd_write(0x81); // "move cursor to beginning of first line"

  // write some chars
  mcp.digitalWrite(lcd_rs, true); // data register
  write_one_digit(fb);
  write_one_digit(wf1);
  write_one_digit(wf2);
  write_one_digit(algo);
  write_two_digit(vca);
  lcd_write(' ');
  write_one_digit(chord_mode);
}

void setup() {
  // debug...
  Serial.begin(9600);

  init_lcd();
   
  static const uint8_t output_pins[] = {
    D2, D3, D4, D5, D6, D7, D8, D9,   // D0..D7
    pin_a0, pin_wr, pin_cs, pin_ic
  };
  for (uint8_t i = 0; i < sizeof(output_pins); i++) {
    pinMode(output_pins[i], OUTPUT);
  }

  // reset the YM3812
  digitalWrite(pin_ic, LOW);
  delayMicroseconds(25);  // reset pulse should be minimum 80 clock cycles: 80 * (1/3.58Mhz) = 22.3us
  digitalWrite(pin_ic, HIGH);
  
  digitalWrite(pin_cs, 1);  // de-assert /CS
  digitalWrite(pin_wr, 1);  // de-assert /WR

  delay(100);

  // per chip
  ym3812_write(0x01, 0x20);  // test off, wave select enable(!)
  ym3812_write(0x08, 0x00);  // disable CSM
  ym3812_write(0xbd, 0xc0);  // full vib/tremolo depth

  // per voice
  set_voice_algo_fb(0);
  setup_voice_op2(0);
  setup_voice_op1(0);

  set_voice_algo_fb(1);
  setup_voice_op2(1);
  setup_voice_op1(1);
}

void setup_voice_op1(byte voice) {
  // op1, the modulator
  set_voice_op1_ADSR(voice);
  set_voice_vca(voice); // ym3812_write(0x40 + op1, 0x00);  // ksl / output level
  set_voice_mult(voice); // ym3812_write(0x20 + op1, 0x20 | 0x03);  // multiplier + vibrato etc
  set_voice_op1_waveform(voice); // ym3812_write(0xe0 + op1, 0x00);  // waveform (sine)
}

void setup_voice_op2(byte voice) {
  // op2, the one being modulated
  set_voice_op2_ADSR(voice);
  ym3812_write(0x43 + voice, 0x00);  // ksl / output level
  ym3812_write(0x23 + voice, 0x00 | 0x00 | 0x20 | 0x00 | 0x01 );  // AM, VIB, sus type, KSR, mult
  set_voice_op2_waveform(voice);  // ym3812_write(0xe0 + op2, 0x02);  // waveform (half sine)
}

void set_voice_op1_ADSR(byte voice) {
  ym3812_write(0x60 + voice, 0xff);  // ad
  ym3812_write(0x80 + voice, 0x0f);  // sr
}

void set_voice_op2_ADSR(byte voice) {
  ym3812_write(0x63 + voice, 0xff);  // ad
  ym3812_write(0x83 + voice, 0x0f);  // sr
}

void set_voice_op1_waveform(byte voice) {  
  ym3812_write(0xe0 + voice, wf1);
}

void set_voice_op2_waveform(byte voice) {
  ym3812_write(0xe3 + voice, wf2);
}

void set_voice_algo(byte voice) {
  ym3812_write(0xc0 + voice, algo | ((fb & 0x7) << 1));
}

void set_voice_note(byte voice, byte note_offset) {
  unsigned int fnum = notetbl[note_num + note_offset];
  ym3812_write(0xa0 + voice, fnum & 0x00ff);  // least significant byte of f-num
  ym3812_write(0xb0 + voice, 0x20 | ((fnum >> 8) & 0x00FF) ) ;  // f-num, octave, key on
}

void set_voice_vca(byte voice) {
  ym3812_write(0x40 + voice, 0x3F & vca);
}

void set_voice_mult(byte voice) {
  ym3812_write(0x20 + voice, 0x20 | (mult & 0xF));
}

void set_voice_algo_fb(byte voice) {
  ym3812_write(0xc0 + voice, algo | ((fb & 0x7) << 1));
}

void set_op1_ADSR() {
  set_voice_op1_ADSR(0);
  set_voice_op1_ADSR(1);
}

void set_op2_ADSR() {
  set_voice_op2_ADSR(0);
  set_voice_op2_ADSR(1);
}

void cycle_op1_waveform() {
  wf1 = (wf1 + 1) % 4;
  set_voice_op1_waveform(0);
  set_voice_op1_waveform(1);
}

void cycle_op2_waveform() {
  wf2 = (wf2 + 1) % 4;
  set_voice_op2_waveform(0);
  set_voice_op2_waveform(1);
}

void cycle_algo() {
  algo = (algo + 1) % 2;
  set_voice_algo(0);
  set_voice_algo(1);
}

void set_note() {
  byte offsets[] = { 3, 5, 7, 12};
  set_voice_note(0, 0);
  set_voice_note(1, offsets[chord_mode]);
}

void set_vca() {
  set_voice_vca(0);
  set_voice_vca(1);
}

void set_mult() {
  set_voice_mult(0);
  set_voice_mult(1);
}

void set_algo_fb() {
  set_voice_algo_fb(0);
  set_voice_algo_fb(1);
}

unsigned int count = 0;
bool dirty = false;
void loop() {
  // sample analog inputs
  int pitchcv = analogRead(A1) / 2;
  int tune = map(analogRead(A2), 0, 1023, -200, 200);
  int new_vca = map(analogRead(A3), 0, 1023, 0, 64);
  new_vca = constrain(new_vca, 0, 63);
  int new_mult = map(analogRead(A6), 0, 1023, 0, 15);
  int new_fb = map(analogRead(A7), 0, 1023, 0, 8);
  new_fb = constrain(new_fb, 0, 7);
  
  // calculate new register values
  int new_note_num = map(pitchcv, 0, (1023 + tune) / 2, 0, 12*5);

  // only update register values that have changed
  if( new_note_num != note_num) {
    digitalWrite(13, true);
    note_num = new_note_num;
    set_note();
  }

  if( new_vca != vca) {
    digitalWrite(13, true);
    vca = new_vca;
    set_vca();
    dirty = true;
  }

  if( new_mult != mult) {
    digitalWrite(13, true);
    mult = new_mult;
    set_mult();
  }

  if( new_fb != fb) {
    digitalWrite(13, true);
    fb = new_fb;
    set_algo_fb();
    dirty = true;
  }

  // throttle to debounce
  count += 1;
  if( count % 10 == 0) {
    byte new_switches = ~(mcp.readGPIO(0) & 0x1F); // 0 is portA
    if(new_switches != switches) {
      switches = new_switches;
      if(switches & 0x01) { // select
        cycle_algo();
      }
      if(switches & 0x02) { // right
        cycle_op2_waveform();
      }
      if(switches & 0x04) { // down
        chord_mode = ( chord_mode == 0) ? 3 : chord_mode - 1;
        set_note();
      }
      if(switches & 0x08) { // up
        chord_mode = (chord_mode+1)%4;
        set_note();
      }
      if(switches & 0x10) { // left
        cycle_op1_waveform();
      }
      dirty = true;
    }
  }

  if(dirty == true) {
    write_to_lcd();
    dirty = false;
  }
  digitalWrite(13, false);
}
