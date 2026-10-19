"""Execute production function bodies against host GPIO/RTOS boundaries.

The extraction keeps the bodies verbatim; no second implementation of LED or
request logic lives in the tests. Full ESP-IDF compilation checks integration.
"""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'migration' / 'host-tests'
OUT.mkdir(exist_ok=True)

def body(path, signature):
    source = (ROOT / path).read_text(encoding='utf-8')
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

prefix = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>
#include <utility>
#include <iostream>
#include "ArduinoJson.h"
#include "multi_button.h"
using EventBits_t = unsigned;
using gpio_num_t = int;
constexpr int Green_led_Mode_queue=1, Red_led_Mode_queue=2;
constexpr int LED_PIN_Green=42, LED_PIN_Red=45, LED_ON=0, LED_OFF=1;
constexpr int pdFALSE=0, portMAX_DELAY=-1, set_bit_all=255, GroupBit1=2;
#define pdMS_TO_TICKS(x) (x)
#define get_bit_data(x,y) (((x)>>(y))&1)
#define rset_bit_data(x) (1u<<(x))
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
std::vector<std::pair<int,int>> trace;
unsigned pending, waits, delays, cleared;
uint8_t flag;
bool continuous;
char sleep_buff[128];
void Led_SetLevel(int pin,int level) { trace.emplace_back(pin,level); }
void gpio_set_level(int pin,int level) { Led_SetLevel(pin,level); }
unsigned xEventGroupWaitBits(int,int,int,int,int) {
    if(waits++) throw 0;
    return pending;
}
void xEventGroupClearBits(int,unsigned mask) { cleared |= mask; }
void vTaskDelay(int ms) {
    trace.emplace_back(-1,ms);
    if(continuous && ++delays==2) flag=0;
}
struct BaseAIModel {
    JsonDocument doc;
    const char* model="configured-model";
    char ark_request_body[3072]{};
    bool is_success=false;
    void BaseAIModel_SetChat(const char*);
};
'''
production = '\n'.join([
    body('components/user_app_bsp/user_app.cpp','static void Green_led_user_Task'),
    body('components/user_app_bsp/user_app.cpp','static void Red_led_user_Task'),
    body('components/user_app_bsp/mode_src/xiaozhi_mode.cpp','void xiaozhi_application_received'),
    body('components/user_app_bsp/mode_src/xiaozhi_mode.cpp','void xiaozhi_init_received'),
    body('components/app_bsp/ai_app.cpp','void BaseAIModel::BaseAIModel_SetChat'),
])
checks = r'''
void run(void(*task)(void*),unsigned bit,bool loop=false) {
    trace.clear(); pending=1u<<bit; waits=delays=cleared=0;
    continuous=loop;flag=loop;
    try {task(&flag);} catch(int) {}
    assert(cleared==pending);
}
uint8_t pin=1;
int singles=0,doubles=0,longs=0,downs=0;
uint8_t read_pin(uint8_t) {return pin;}
void ticks(int n,int level) {pin=level;for(int i=0;i<n;++i)button_ticks();}
int main() {
    for(int n=1;n<=4;++n) {
        run(Green_led_user_Task,n==4?7:n);
        std::vector<std::pair<int,int>> expected;
        for(int i=0;i<n;++i) {
            expected.emplace_back(42,0);expected.emplace_back(-1,200);
            expected.emplace_back(42,1);
            if(i<n-1)expected.emplace_back(-1,200);
        }
        assert(trace==expected);
    }
    run(Green_led_user_Task,4); assert((trace==std::vector<std::pair<int,int>>{{42,0}}));
    run(Green_led_user_Task,5); assert((trace==std::vector<std::pair<int,int>>{{42,1}}));
    run(Green_led_user_Task,6,true);
    assert((trace==std::vector<std::pair<int,int>>{{42,0},{-1,100},{42,1},{-1,100}}));
    run(Red_led_user_Task,0);assert((trace==std::vector<std::pair<int,int>>{{45,0}}));
    run(Red_led_user_Task,6,true);
    assert((trace==std::vector<std::pair<int,int>>{{-1,100},{45,1},{-1,100},{45,0}}));
    run(Red_led_user_Task,1);
    std::vector<std::pair<int,int>> expected;
    for(int i=0;i<5;++i) {
        expected.emplace_back(45,0);expected.emplace_back(-1,400);
        expected.emplace_back(45,1);expected.emplace_back(-1,400);
    }
    assert(trace==expected);
    trace.clear();
    for(auto s:{"idle","listening","speaking","idle"}) xiaozhi_application_received(s);
    assert((trace==std::vector<std::pair<int,int>>{{45,0},{45,1}}));
    // This production callback links with no HTTP or display stub: boot cannot
    // refresh weather through it without breaking this test's build.
    for(auto s:{"starting","idle","listening"}) xiaozhi_init_received(s);
    BaseAIModel ai; ai.BaseAIModel_SetChat("anime with \"quotes\" and newline\n");
    assert(ai.is_success);
    JsonDocument parsed;
    assert(!deserializeJson(parsed,ai.ark_request_body));
    assert(parsed["model"]=="configured-model");
    assert(parsed["size"]=="1280x720");assert(parsed["output_format"]=="jpeg");
    assert(parsed["response_format"]=="url");assert(parsed["watermark"]==false);
    assert(parsed["sequential_image_generation"].isNull());assert(parsed["stream"].isNull());
    Button button; button_init(&button,read_pin,0,0);
    button_attach(&button,BTN_SINGLE_CLICK,[](Button*){++singles;});
    button_attach(&button,BTN_DOUBLE_CLICK,[](Button*){++doubles;});
    button_attach(&button,BTN_LONG_PRESS_START,[](Button*){++longs;});
    button_attach(&button,BTN_PRESS_DOWN,[](Button*){++downs;});
    assert(button_start(&button)==0);
    ticks(1,0);ticks(10,1);assert(downs==0); // reject bounce
    ticks(10,0);ticks(80,1);assert(singles==1 && doubles==0 && longs==0);
    ticks(10,0);ticks(10,1);ticks(10,0);ticks(80,1);assert(doubles==1);
    ticks(190,0);assert(longs==0);ticks(20,0);assert(longs==1);
    ticks(80,1);assert(singles==1);button_stop(&button);
    std::cout<<"PASS: LED patterns and state callback, button gestures, boot weather, Ark JSON\n";
}
'''
source = OUT / 'test_behavior.cpp'
source.write_text(prefix + production + checks, encoding='utf-8')
button = ROOT / 'components/port_bsp/src/multi_button'
subprocess.run(['cl','/nologo','/EHsc','/std:c++17','/utf-8',
    '/I'+str(ROOT/'components/app_bsp/json_inc'),'/I'+str(button),
    str(source),str(button/'multi_button.c'),'/Fe:'+str(OUT/'test_behavior.exe'),
    '/Fo:'+str(OUT)+'\\'],check=True,cwd=OUT)
subprocess.run([str(OUT/'test_behavior.exe')],check=True)

# Execute the actual startup dispatch with a small in-memory NVS boundary.
mode_source = r'''
#include <cassert>
#include <cstdint>
#include <map>
#include <string>
#include <iostream>
using esp_err_t=int;
using nvs_handle_t=int;
constexpr int ESP_OK=0, ESP_ERR_NVS_NO_FREE_PAGES=2, ESP_ERR_NVS_NEW_VERSION_FOUND=3, NVS_READWRITE=1;
#define ESP_ERROR_CHECK(x) assert((x)==ESP_OK)
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
std::map<std::string,uint8_t> values;
int dispatched=0, initialized=0;
int esp_event_loop_create_default(){return 0;}
int nvs_flash_init(){return 0;}
int nvs_flash_erase(){return 0;}
int nvs_open(const char* ns,int,int* handle){assert(std::string(ns)=="PhotoPainter");*handle=1;return 0;}
int nvs_get_u8(int,const char* key,uint8_t* value){auto it=values.find(key);if(it==values.end())return 1;*value=it->second;return 0;}
int nvs_set_u8(int,const char* key,uint8_t value){values[key]=value;return 0;}
int nvs_commit(int){return 0;}
void nvs_close(int){}
int User_Mode_init(){return 1;}
void User_Basic_mode_app_init(){dispatched=1;}
void User_Network_mode_app_init(){dispatched=2;}
void Mode_Selection_Init(){dispatched=4;}
struct Application {
    static Application& GetInstance(){static Application app;return app;}
    void Initialize(){++initialized;}
    void Run(){assert(initialized==1);dispatched=3;}
};
'''
mode_checks = r'''
int main(){
    app_main();assert(dispatched==1);
    assert(values["NetworkMode"]==0 && values["PhotPainterMode"]==1 && values["Mode_Flag"]==1);
    for(int mode=1;mode<=4;++mode){
        values["PhotPainterMode"]=mode;values["NetworkMode"]=1;values["Mode_Flag"]=0;
        dispatched=initialized=0;app_main();assert(dispatched==mode);
        assert(values["NetworkMode"]==1 && values["Mode_Flag"]==0);
    }
    std::cout<<"PASS: default mode, all mode dispatches, existing NVS settings\n";
}
'''
mode_file = OUT/'test_modes.cpp'
mode_file.write_text(mode_source + body('main/main.cc','extern "C" void app_main') + mode_checks,encoding='utf-8')
subprocess.run(['cl','/nologo','/EHsc','/std:c++17','/utf-8',str(mode_file),
    '/Fe:'+str(OUT/'test_modes.exe'),'/Fo:'+str(OUT)+'\\'],check=True,cwd=OUT)
subprocess.run([str(OUT/'test_modes.exe')],check=True)

stub = OUT/'stubs/freertos'
stub.mkdir(parents=True,exist_ok=True)
(stub/'FreeRTOS.h').write_text('#pragma once\n#include <stdint.h>\n',encoding='utf-8')
weather_source = r'''
#define _CRT_SECURE_NO_WARNINGS
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <string>
#include <iostream>
#include "ArduinoJson.h"
#include "weather_app.h"
#define ESP_LOGE(...) ((void)0)
#define MALLOC_CAP_SPIRAM 0
void* heap_caps_malloc(size_t n,int){return malloc(n);}
void heap_caps_free(void* p){free(p);}
JsonDocument doc;
WeatherPort::~WeatherPort(){}
void WeatherPort::WeatherPort_log(){}
'''
weather_checks = r'''
char* owned(const std::string& s){char* p=(char*)malloc(s.size()+1);memcpy(p,s.c_str(),s.size()+1);return p;}
int main(){
    WeatherPort weather;
    assert(weather.WeatherPort_DecodingSring(owned("bad json"))==nullptr);
    assert(weather.WeatherPort_DecodingSring(owned("{\"error\":\"unavailable\"}"))==nullptr);
    JsonDocument fixture;
    fixture["time"]="2026-10-19 12:30:00";fixture["data"]["shidu"]="60%";
    for(int i=0;i<4;++i){auto f=fixture["data"]["forecast"][i];
        f["high"]="high 25";f["low"]="low 15";f["fx"]="north";
        f["week"]="Mon";f["type"]="sunny";f["ymd"]="2026-10-19";f["aqi"]=30;
    }
    std::string valid;serializeJson(fixture,valid);
    auto* data=weather.WeatherPort_DecodingSring(owned(valid));assert(data);
    assert(std::string(data->calendar)=="2026-10-19");assert(std::string(data->td_RH)=="60%");
    WeatherData_t before=*data;
    fixture["data"]["forecast"][3]["type"]=std::string(100,'x');
    std::string invalid;serializeJson(fixture,invalid);
    assert(weather.WeatherPort_DecodingSring(owned(invalid))==nullptr);
    assert(memcmp(data,&before,sizeof(before))==0);
    assert(weather.WeatherPort_DecodingSring(owned(valid))==data);
    std::cout<<"PASS: weather errors, valid parsing, failure preserves data, retry recovery\n";
}
'''
weather_file=OUT/'test_weather.cpp'
weather_file.write_text(weather_source + body('components/app_bsp/weather_app.cpp','WeatherPort::WeatherPort()') +
    body('components/app_bsp/weather_app.cpp','WeatherData_t* WeatherPort::WeatherPort_DecodingSring') + weather_checks,encoding='utf-8')
subprocess.run(['cl','/nologo','/EHsc','/std:c++17','/utf-8',str(weather_file),
    '/I'+str(OUT/'stubs'),'/I'+str(ROOT/'components/app_bsp'),'/I'+str(ROOT/'components/app_bsp/json_inc'),
    '/Fe:'+str(OUT/'test_weather.exe'),'/Fo:'+str(OUT)+'\\'],check=True,cwd=OUT)
subprocess.run([str(OUT/'test_weather.exe')],check=True)

image_source = r'''
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <iostream>
#define CLAMP(x,lo,hi) ((x)<(lo)?(lo):((x)>(hi)?(hi):(x)))
struct ImgDecodeDither {
    int ImgDecode_NearestColor(uint8_t,uint8_t,uint8_t);
    void ImgDecode_DitherRgb888(uint8_t*,uint8_t*,int,int);
    void ImgDecode_ScaleRgb888Nearest(const uint8_t*,int,int,uint8_t*,int,int);
};
'''
image_checks = r'''
int main(){
    ImgDecodeDither decoder;
    for(bool portrait:{false,true}) {
        int sw=portrait?720:1280,sh=portrait?1280:720;
        int dw=portrait?480:800,dh=portrait?800:480;
        std::vector<uint8_t> src(sw*sh*3),scaled(dw*dh*3+2,0xA5),out(dw*dh*3+2,0xA5);
        for(int y=0;y<sh;++y)for(int x=0;x<sw;++x){
            auto color=PALETTE[x*6/sw];memcpy(&src[(y*sw+x)*3],color,3);
        }
        decoder.ImgDecode_ScaleRgb888Nearest(src.data(),sw,sh,scaled.data()+1,dw,dh);
        assert(scaled.front()==0xA5 && scaled.back()==0xA5);
        decoder.ImgDecode_DitherRgb888(scaled.data()+1,out.data()+1,dw,dh);
        assert(out.front()==0xA5 && out.back()==0xA5);
        for(int i=0;i<dw*dh;++i){bool found=false;
            for(auto& color:PALETTE)if(memcmp(&out[1+i*3],color,3)==0)found=true;
            assert(found);
        }
        for(int band=0;band<6;++band)assert(memcmp(&out[1+(dw/12+band*dw/6)*3],PALETTE[band],3)==0);
    }
    std::cout<<"PASS: landscape/portrait scaling, six-color dithering, output bounds\n";
}
'''
img=(ROOT/'components/app_bsp/imgdecode_app.cpp').read_text(encoding='utf-8')
palette=img[img.index('static const uint8_t PALETTE'):img.index('};',img.index('static const uint8_t PALETTE'))+2]
image_file=OUT/'test_image.cpp'
image_file.write_text(image_source+palette+'\n'+ '\n'.join(body('components/app_bsp/imgdecode_app.cpp',sig) for sig in [
    'int ImgDecodeDither::ImgDecode_NearestColor',
    'void ImgDecodeDither::ImgDecode_DitherRgb888',
    'void ImgDecodeDither::ImgDecode_ScaleRgb888Nearest'])+image_checks,encoding='utf-8')
subprocess.run(['cl','/nologo','/EHsc','/std:c++17','/utf-8',str(image_file),
    '/Fe:'+str(OUT/'test_image.exe'),'/Fo:'+str(OUT)+'\\'],check=True,cwd=OUT)
subprocess.run([str(OUT/'test_image.exe')],check=True)

pcm_source = r'''
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <vector>
#include <iterator>
#include <iostream>
#include "pcm_decode.h"
const uint8_t* prompt_starts[4];
const uint8_t* prompt_ends[4];
constexpr int MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2;
bool fail_alloc=false;
void* heap_caps_realloc(void* p,size_t n,int){if(fail_alloc)return nullptr;return realloc(p,n);}
struct CodecPort {
    uint8_t* prompt_buffer_=nullptr;
    size_t prompt_capacity_=0;
    int prompt_index_=-1;
    int Codec_GetMusicSizt(uint8_t);
    uint8_t* Codec_GetMusicData(uint8_t);
    ~CodecPort(){free(prompt_buffer_);}
};
std::vector<uint8_t> read(const std::string& name){std::ifstream f(name,std::ios::binary);assert(f);return {std::istreambuf_iterator<char>(f),{}};}
'''
pcm_checks = r'''
int main(int argc,char** argv){
    assert(argc==2);std::string root=argv[1];
    std::vector<uint8_t> packed[4],raw[4];
    const char* names[]={"mode.pcm","mode_1.pcm","mode_2.pcm","mode_3.pcm"};
    for(int i=0;i<4;++i){raw[i]=read(root+"/"+names[i]);packed[i]=read(root+"/"+names[i]+".zlib");
        prompt_starts[i]=packed[i].data();prompt_ends[i]=packed[i].data()+packed[i].size();}
    CodecPort codec;
    for(int i:{0,1,2,3,0,3}){
        assert(codec.Codec_GetMusicSizt(i)==raw[i].size());
        auto* decoded=codec.Codec_GetMusicData(i);assert(decoded);
        assert(std::equal(raw[i].begin(),raw[i].end(),decoded));
        assert(codec.Codec_GetMusicData(i)==decoded);
    }
    assert(codec.Codec_GetMusicSizt(4)==0 && codec.Codec_GetMusicData(4)==nullptr);
    CodecPort allocation;
    assert(allocation.Codec_GetMusicData(0));fail_alloc=true;
    assert(allocation.Codec_GetMusicData(1)==nullptr);fail_alloc=false;
    assert(allocation.Codec_GetMusicData(1));
    std::vector<uint8_t> out(raw[0].size());
    assert(!DecodePcm(packed[0].data(),3,out.data(),out.size()));
    assert(!DecodePcm(packed[0].data(),packed[0].size(),out.data(),out.size()-1));
    packed[0][4]^=0xff;
    assert(!DecodePcm(packed[0].data(),packed[0].size(),out.data(),out.size()));
    std::cout<<"PASS: all PCM bytes exact, cache switching, invalid input, allocation failure recovery\n";
}
'''
pcm_file=OUT/'test_pcm.cpp'
pcm_file.write_text(pcm_source+body('components/port_bsp/codec_bsp.cpp','int CodecPort::Codec_GetMusicSizt')+
    body('components/port_bsp/codec_bsp.cpp','uint8_t *CodecPort::Codec_GetMusicData')+pcm_checks,encoding='utf-8')
zlib=ROOT/'migration/idf61-project/managed_components/espressif__zlib/zlib'
subprocess.run(['cl','/nologo','/EHsc','/std:c++17','/utf-8','/D_CRT_SECURE_NO_WARNINGS',str(pcm_file),
    '/I'+str(ROOT/'components/port_bsp'),'/I'+str(zlib),
    *[str(zlib/name) for name in ['uncompr.c','inflate.c','inffast.c','inftrees.c','zutil.c','adler32.c','crc32.c']],
    '/Fe:'+str(OUT/'test_pcm.exe'),'/Fo:'+str(OUT)+'\\'],check=True,cwd=OUT)
subprocess.run([str(OUT/'test_pcm.exe'),str(ROOT/'components/port_bsp/pcm_src')],check=True)

wifi_source = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <map>
#include <string>
#include <vector>
#include <iostream>
#include "traverse_nvs.h"
#define ESP_LOGI(...)
#define ESP_LOGE(...)
using nvs_handle_t=int; using esp_err_t=int;
constexpr int ESP_OK=0,ESP_ERR_NVS_NOT_FOUND=1,NVS_READONLY=0;
std::map<std::string,std::string> current;
std::map<std::string,std::vector<uint8_t>> legacy;
int opened=0;
int nvs_open(const char* name,int,int* h){*h=strcmp(name,"wifi")==0?1:2; if((*h==1?current.empty():legacy.empty()))return 1; ++opened;return 0;}
void nvs_close(int){--opened;}
const char* esp_err_to_name(int){return "stub";}
int nvs_get_str(int,const char* key,char* out,size_t* n){auto i=current.find(key);if(i==current.end())return 1;auto len=i->second.size()+1;if(*n<len)return 2;memcpy(out,i->second.c_str(),len);*n=len;return 0;}
int nvs_get_blob(int,const char* key,void* out,size_t* n){auto i=legacy.find(key);if(i==legacy.end())return 1;if(out){if(*n<i->second.size())return 2;memcpy(out,i->second.data(),i->second.size());}*n=i->second.size();return 0;}
TraverseNvs::TraverseNvs(){} TraverseNvs::~TraverseNvs(){}
'''
wifi_checks = r'''
int main(){
 TraverseNvs reader;
 assert(!reader.Get_WifiCredentialFromNVS().is_valid);
 legacy["sta.ssid"]={3,0,0,0,'o','l','d'};legacy["sta.pswd"]={'p',0};
 auto c=reader.Get_WifiCredentialFromNVS();assert(c.is_valid && std::string(c.ssid)=="old");
 current={{"ssid","new"},{"password","updated"}};
 c=reader.Get_WifiCredentialFromNVS();assert(c.is_valid && std::string(c.ssid)=="new" && std::string(c.password)=="updated");
 legacy.clear();c=reader.Get_WifiCredentialFromNVS();assert(c.is_valid);
 current["password"]="";c=reader.Get_WifiCredentialFromNVS();assert(c.is_valid && c.password[0]==0);
 current={{"ssid1","second"},{"password1","p"}};c=reader.Get_WifiCredentialFromNVS();assert(c.is_valid && std::string(c.ssid)=="second");
 current={{"ssid",std::string(33,'x')},{"password","p"}};assert(!reader.Get_WifiCredentialFromNVS().is_valid);
 current.clear();legacy["sta.ssid"]={32,0,0,0,'x'};legacy["sta.pswd"]={'p',0};assert(!reader.Get_WifiCredentialFromNVS().is_valid);
 assert(opened==0);
 std::cout<<"PASS: Wi-Fi fresh provisioning, stale legacy override, legacy fallback, open network, sparse slots, invalid lengths\n";
}
'''
wifi_file=OUT/'test_wifi.cpp'
wifi_file.write_text(wifi_source+'\n'.join(body('components/port_bsp/traverse_nvs.cpp',sig) for sig in [
 'void TraverseNvs::parse_sta_ssid_blob','void TraverseNvs::parse_sta_pswd_blob','wifi_credential_t TraverseNvs::Get_WifiCredentialFromNVS'])+wifi_checks,encoding='utf-8')
subprocess.run(['cl','/nologo','/EHsc','/std:c++17','/utf-8','/D_CRT_SECURE_NO_WARNINGS',str(wifi_file),'/I'+str(ROOT/'components/port_bsp'),'/Fe:'+str(OUT/'test_wifi.exe'),'/Fo:'+str(OUT)+'\\'],check=True,cwd=OUT)
subprocess.run([str(OUT/'test_wifi.exe')],check=True)

refresh_source = r'''
#include <atomic>
#include <cassert>
#include <string>
#include <iostream>
std::atomic<bool> current_frame_ready{false};
void* epaper_groups=nullptr;
void* ai_IMG_LoopGroup=nullptr;
unsigned submitted=0; int clears=0;
using EventBits_t=unsigned;
#define ESP_LOGI(...)
struct Panel {int calls=0;void EPD_Display(){++calls;}} ePaperDisplay;
void xEventGroupClearBits(void*,unsigned b){assert(b==1);++clears;}
void xEventGroupSetBits(void*,unsigned b){submitted|=b;}
'''
refresh_checks = r'''
int main(){
 assert(std::string(RequestCurrentImageRefresh()).find("not_ready")!=std::string::npos);
 epaper_groups=(void*)1;ai_IMG_LoopGroup=(void*)2;
 assert(std::string(RequestCurrentImageRefresh()).find("no_current_image")!=std::string::npos);
 assert(submitted==0 && clears==0);
 current_frame_ready.store(true);
 assert(std::string(RequestCurrentImageRefresh()).find("queued")!=std::string::npos);
 assert(submitted==0x10 && clears==1); // No weather or image-generation event.
 submitted=0;
 ProcessCurrentImageRefresh(0x10,false,false);assert(submitted==0x10 && ePaperDisplay.calls==0);
 ProcessCurrentImageRefresh(submitted,false,true);assert(ePaperDisplay.calls==1);
 ProcessCurrentImageRefresh(0x10,true,true);assert(ePaperDisplay.calls==1);
 // Failed weather clears its own bit; redraw still reaches the consumer.
 ProcessCurrentImageRefresh(0x11 & ~1u,false,true);assert(ePaperDisplay.calls==2);
 ProcessCurrentImageRefresh(0x12,false,true);assert(ePaperDisplay.calls==3);
 ProcessCurrentImageRefresh(1,false,true);assert(ePaperDisplay.calls==3);
 std::cout<<"PASS: lock timeout/retry, failed weather or missing SD image with redraw; redraw rejects uninitialized frame; only redraw event is queued; no completion claim\n";
}
'''
refresh_file=OUT/'test_refresh.cpp'
refresh_file.write_text(refresh_source+body('components/user_app_bsp/mode_src/xiaozhi_mode.cpp','const char* RequestCurrentImageRefresh()')+body('components/user_app_bsp/mode_src/xiaozhi_mode.cpp','static void ProcessCurrentImageRefresh(')+refresh_checks,encoding='utf-8')
subprocess.run(['cl','/nologo','/EHsc','/std:c++17','/utf-8',str(refresh_file),'/Fe:'+str(OUT/'test_refresh.exe'),'/Fo:'+str(OUT)+'\\'],check=True,cwd=OUT)
subprocess.run([str(OUT/'test_refresh.exe')],check=True)
