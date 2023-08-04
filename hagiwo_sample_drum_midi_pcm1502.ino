#include "sample.h"  //sample file
#include <EEPROM.h>
#include <MIDI.h>
#include <driver/i2s.h>
#include "freertos/queue.h"

#define I2S_NUM (i2s_port_t)0  // i2s port number
#define SAMPLE_RATE 48000      // sample rate
#define I2S_BCK_IO GPIO_NUM_4  // BCK pin
#define I2S_WS_IO GPIO_NUM_5   // LRCK pin
#define I2S_DO_IO GPIO_NUM_3   // DATA pin
#define I2S_DI_IO -1           // not used

i2s_config_t i2s_config = {
  .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
  .sample_rate = SAMPLE_RATE,
  .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
  .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
  .communication_format = I2S_COMM_FORMAT_I2S_MSB,
  .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
  .dma_buf_count = 8,
  .dma_buf_len = 64
};

i2s_pin_config_t pin_config = {
  .bck_io_num = I2S_BCK_IO,
  .ws_io_num = I2S_WS_IO,
  .data_out_num = I2S_DO_IO,
  .data_in_num = I2S_DI_IO
};

#define TRIGGER D5
#define MIDI_CHANNEL 1

float left_vol;
float right_vol;
volatile bool samplePlaying = false;  // add this to your global variables
volatile bool soundFlag = false;
float i;  //sample play progress
int freq = 48000;
int midifreq = 23;
float volume = 1.00;
float panl = 0.50;
float panr = 0.50;
float midivolume = 1.00;
float midipanl = 0.50;
float midipanr = 0.50;
bool trig1, old_trig1, done_trig1;
int sound_out;          //sound out PWM rate
byte sample_no = 0;     //select sample number
long timer = 0;         //timer count for eeprom write
bool eeprom_write = 0;  //0=no write,1=write

//-------------------------timer interrupt for sound----------------------------------
//hw_timer_t *timer0 = NULL;
//portMUX_TYPE timerMux0 = portMUX_INITIALIZER_UNLOCKED;

const uint8_t *sample_array[] = { smpl0, smpl1, smpl2, smpl3, smpl4, smpl5, smpl6, smpl7, smpl8, smpl9, smpl10, smpl11, smpl12, smpl13, smpl14, smpl15, smpl16, smpl17, smpl18, smpl19, smpl20, smpl21, smpl22, smpl23, smpl24, smpl25, smpl26, smpl27, smpl28, smpl29, smpl30, smpl31, smpl32, smpl33, smpl34, smpl35, smpl36, smpl37, smpl38, smpl39, smpl40, smpl41, smpl42, smpl43, smpl44, smpl45, smpl46, smpl47 };
const int speed_array[] = { 8000, 10000, 12000, 14000, 16000, 18000, 20000, 22000, 24000, 26000, 28000, 30000, 32000, 34000, 36000, 38000, 40000, 42000, 44000, 46000, 48000, 50000, 52000, 54000, 56000, 58000, 60000, 62000, 64000, 66000, 68000, 70000, 72000, 74000, 76000, 78000, 80000, 82000, 84000, 86000, 88000, 90000, 92000, 94000, 96000 };

void IRAM_ATTR onTimer() {
  soundFlag = true;
}

MIDI_CREATE_INSTANCE(HardwareSerial, Serial0, MIDI);


void setup() {
  Serial.begin(115200);
  i2s_driver_install(I2S_NUM, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM, &pin_config);
  i2s_set_clk(I2S_NUM, SAMPLE_RATE, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);

  EEPROM.begin(1);           //1byte memory space
  EEPROM.get(0, sample_no);  //callback saved sample number

  if (sample_no >= 48) {  //countermeasure rotary encoder error
    sample_no = 0;
  }

  pinMode(TRIGGER, INPUT);  //trigger in
  timer = millis();         //for eeprom write

  // timer0 = timerBegin(0, 1666, true);            // timer0, 12.5ns*1666 = 20.83usec(48kHz), count-up
  // timerAttachInterrupt(timer0, &onTimer, true);  // edge-triggered
  // timerAlarmWrite(timer0, 1, true);              // 1*20.83usec = 20.83usec, auto-reload
  // timerAlarmEnable(timer0);                      // enable timer0

  MIDI.begin(MIDI_CHANNEL);
  delay(300);
}

void eeprom_update() {
  EEPROM.put(0, sample_no);
  EEPROM.commit();
}

// update playSample function
void playSample() {
  if (!samplePlaying) {
    return;
  }

  const int chunk_size = 512;
  int16_t *i2s_data = new int16_t[chunk_size * 2];
  static int i = 0;
  int chunk_index = 0;

  if (done_trig1 == 1) {
    i = 0;
    done_trig1 = 0;
  }

  for (chunk_index = 0; chunk_index < chunk_size && i < 28800; chunk_index++, i++) {
    uint16_t sample_data = (((pgm_read_byte(&(sample_array[sample_no][i * 2]))) | (pgm_read_byte(&(sample_array[sample_no][i * 2 + 1]))) << 8));

    // Convert sample_data to float, apply volume and convert back to int16_t
    int16_t volume_adjusted_sample = (int16_t)((float)sample_data * 1);

    // Implement simplistic stereo panning
    i2s_data[chunk_index * 2] = volume_adjusted_sample * left_vol; // Left channel
    i2s_data[chunk_index * 2 + 1] = volume_adjusted_sample * right_vol; // Right channel
  }


  size_t bytes_written;
  i2s_set_sample_rates(I2S_NUM, (int)freq);
  Serial.println(freq);
  i2s_write((i2s_port_t)I2S_NUM, i2s_data, chunk_index * 2 * sizeof(int16_t), &bytes_written, portMAX_DELAY);

  if (i >= 28800 && done_trig1 == 0) {
    i = 0;
    samplePlaying = false;  // stop playing the sample once it's finished
  }

  delayMicroseconds(10);

  delete[] i2s_data;
}

void loop() {

  playSample();

  int type, noteMsg, velocity, channel, d1, d2;
  if (MIDI.read(MIDI_CHANNEL)) {
    byte type = MIDI.getType();
    switch (type) {

      case midi::NoteOn:
        d1 = MIDI.getData1();
        d2 = MIDI.getData2();
        switch (d1) {
          case 36:  // 36 for bottom C Bass
            if (d2 != 0) {
              done_trig1 = 1;
              i = 0;
              samplePlaying = true;
            }
            break;
        }
        break;

      case midi::ControlChange:
        d1 = MIDI.getData1();
        d2 = MIDI.getData2();
        switch (d1) {
          case 8:
            midifreq = map(d2, 0, 127, 0, 44); // range from 8kHz to 48kHz for simplicity, adjust as needed
            freq = speed_array[midifreq];
            break;
          case 9:                     // CC7 for volume
            midivolume = d2 / 127.0;  // MIDI CC messages have a range from 0 to 127
            volume = constrain(midivolume, 0.0, 1.0);
            break;
          case 10:                  // CC10 for panning
            midipanl = d2 / 127.0;  // pan will be a float ranging from 0 (full left) to 1 (full right)
            panl = constrain(midipanl, 0.0, 1.0);
            midipanr = map(d2, 0, 127, 127, 0) / 127.0;
            panr = midipanr;
            break;
        }
        left_vol = panl * volume;
        right_vol = panr * volume;
        break;

      case midi::ProgramChange:
        d1 = MIDI.getData1();
        sample_no = d1;
        done_trig1 = 1;  //1 shot play when sample changed
        i = 0;
        samplePlaying = true;
        timer = millis();
        eeprom_write = 1;  //eeprom update flug on
        break;
    }
  }

  // From 0 to 127
  //-------------------------trigger----------------------------------
  old_trig1 = trig1;
  trig1 = digitalRead(TRIGGER);
  if (trig1 == 1 && old_trig1 == 0) {  //detect trigger signal low to high , before sample play was done
    done_trig1 = 1;
    i = 0;
    samplePlaying = true;
  }

  //-------------------------save to eeprom----------------------------------
  if (timer + 5000 <= millis() && eeprom_write == 1) {  //Memorized 5 seconds after sample number change
    eeprom_write = 0;
    eeprom_update();
  }
}