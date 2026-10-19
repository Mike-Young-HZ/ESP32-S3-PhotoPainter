#include <stdio.h>
#include <string.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include "codec_bsp.h"
#include "i2c_bsp.h"
#include "pcm_decode.h"
#include <esp_heap_caps.h>

#define SAMPLE_RATE 24000 // Sampling rate: 24000Hz
#define BIT_DEPTH 32      // Word size: 32 bits

esp_codec_dev_handle_t playback = NULL;
esp_codec_dev_handle_t record   = NULL;

extern const uint8_t mode_pcm_start[] asm("_binary_mode_pcm_zlib_start");
extern const uint8_t mode_pcm_end[] asm("_binary_mode_pcm_zlib_end");

extern const uint8_t one_pcm_start[] asm("_binary_mode_1_pcm_zlib_start");
extern const uint8_t one_pcm_end[] asm("_binary_mode_1_pcm_zlib_end");

extern const uint8_t two_pcm_start[] asm("_binary_mode_2_pcm_zlib_start");
extern const uint8_t two_pcm_end[] asm("_binary_mode_2_pcm_zlib_end");

extern const uint8_t three_pcm_start[] asm("_binary_mode_3_pcm_zlib_start");
extern const uint8_t three_pcm_end[] asm("_binary_mode_3_pcm_zlib_end");

static const uint8_t* const prompt_starts[] = {mode_pcm_start, one_pcm_start, two_pcm_start, three_pcm_start};
static const uint8_t* const prompt_ends[] = {mode_pcm_end, one_pcm_end, two_pcm_end, three_pcm_end};

CodecPort::CodecPort(I2cMasterBus& i2cbus) :
i2cbus_(i2cbus) 
{
    set_codec_board_type("USER_CODEC_BOARD");
    codec_init_cfg_t codec_cfg = {};
    codec_cfg.in_mode          = CODEC_I2S_MODE_TDM;
    codec_cfg.out_mode         = CODEC_I2S_MODE_TDM;
    codec_cfg.in_use_tdm       = false;
    codec_cfg.reuse_dev        = false;
    ESP_ERROR_CHECK(init_codec(&codec_cfg));
    playback = get_playback_handle();
    record   = get_record_handle();

    i2c_master_bus_handle_t I2cMasterBus = i2cbus_.Get_I2cBusHandle();
    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address  = Es8311Address;
    dev_cfg.scl_speed_hz    = 100000;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(I2cMasterBus, &dev_cfg, &I2c_DevEs8311));

    dev_cfg.device_address  = Es7210Address;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(I2cMasterBus, &dev_cfg, &I2c_DevEs7210));

}

CodecPort::~CodecPort() {
    heap_caps_free(prompt_buffer_);
}

uint8_t CodecPort::Codec_PlayInfoAudio() {
    esp_codec_dev_set_out_vol(playback, 100.0); //Set the volume to 100.
    esp_codec_dev_sample_info_t fs = {};
    fs.sample_rate                 = 16000;
    fs.channel                     = 2;
    fs.bits_per_sample             = 16;
    int     err                    = esp_codec_dev_open(playback, &fs); //Start playback
    uint8_t errx                   = (err == ESP_CODEC_DEV_OK) ? 1 : 0;
    return errx;
}

void CodecPort::Codec_PlayBackWrite(void *data_ptr, uint32_t len) {
    esp_codec_dev_write(playback, data_ptr, len);
}

uint8_t CodecPort::Codec_ClosePlay() {
    int     err  = esp_codec_dev_close(playback);
    uint8_t errx = (err == ESP_CODEC_DEV_OK) ? 1 : 0;
    return errx;
}

int CodecPort::Codec_GetMusicSizt(uint8_t value) {
    if (value >= 4) return 0;
    return PcmDecodedSize(prompt_starts[value], prompt_ends[value] - prompt_starts[value]);
}

uint8_t *CodecPort::Codec_GetMusicData(uint8_t value) {
    if (value >= 4) return nullptr;
    if (prompt_index_ == value) return prompt_buffer_;
    size_t needed = Codec_GetMusicSizt(value);
    if (needed == 0 || needed > 512 * 1024) return nullptr;
    prompt_index_ = -1;
    if (needed > prompt_capacity_) {
        auto* buffer = static_cast<uint8_t*>(heap_caps_realloc(
            prompt_buffer_, needed, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!buffer) return nullptr;
        prompt_buffer_ = buffer;
        prompt_capacity_ = needed;
    }
    if (!DecodePcm(prompt_starts[value], prompt_ends[value] - prompt_starts[value],
                   prompt_buffer_, prompt_capacity_)) return nullptr;
    prompt_index_ = value;
    return prompt_buffer_;
}

void CodecPort::Codec_SetCodecReg(const char *str, uint8_t reg, uint8_t data) {
    if (!strcmp(str, "es8311"))
        i2cbus_.i2c_write_buff(I2c_DevEs8311, reg, &data, 1);
    if (!strcmp(str, "es7210"))
        i2cbus_.i2c_write_buff(I2c_DevEs7210, reg, &data, 1);
}

uint8_t CodecPort::Codec_GetCodecReg(const char *str, uint8_t reg) {
    uint8_t data = 0x00;
    if (!strcmp(str, "es8311"))
        i2cbus_.i2c_read_buff(I2c_DevEs8311, reg, &data, 1);
    if (!strcmp(str, "es7210"))
        i2cbus_.i2c_read_buff(I2c_DevEs7210, reg, &data, 1);
    return data;
}
