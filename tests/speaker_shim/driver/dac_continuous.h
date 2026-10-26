#pragma once
#include <Arduino.h>
using esp_err_t=int;constexpr int ESP_OK=0,DAC_CHANNEL_MASK_CH1=2,DAC_DIGI_CLK_SRC_DEFAULT=0,DAC_CHANNEL_MODE_SIMUL=0;
using dac_continuous_handle_t=int*;
struct dac_continuous_config_t{int chan_mask,desc_num,buf_size,freq_hz,offset,clk_src,chan_mode;};
inline std::atomic<bool> dacWriting{false},blockWrites{false},dacEnabled{false};
inline bool failDac=false;
inline const char* esp_err_to_name(int){return "host injected";}
inline int dac_continuous_new_channels(const dac_continuous_config_t*,int** p){if(failDac){failDac=false;return 1;}*p=new int;return 0;}
inline int dac_continuous_enable(int*){dacEnabled=true;return 0;}
inline int dac_continuous_disable(int*){if(dacWriting)throw std::runtime_error("disable during write");dacEnabled=false;return 0;}
inline int dac_continuous_del_channels(int* p){if(dacWriting)throw std::runtime_error("delete during write");delete p;return 0;}
inline int dac_continuous_write(int*,const uint8_t* data,size_t n,size_t* loaded,unsigned){
  dacWriting=true;while(blockWrites)delay(1);volatile uint8_t sample=data[n-1];(void)sample;delay(1);*loaded=n;dacWriting=false;return 0;
}
