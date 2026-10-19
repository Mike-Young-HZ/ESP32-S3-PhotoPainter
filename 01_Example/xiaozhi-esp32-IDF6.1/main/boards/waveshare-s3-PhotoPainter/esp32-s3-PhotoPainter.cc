#include "application.h"
#include "button.h"
#include "codecs/box_audio_codec.h"
#include "config.h"
#include "wifi_board.h"
#include <wifi_station.h>

#include "power_save_timer.h"
#include "user_app.h"
#include <driver/i2c_master.h>
#include <esp_log.h>

#include "mcp_server.h"
#include "display.h"
#include "device_state_machine.h"
#include "settings.h"
#include <wifi_manager.h>
#include <esp_system.h>

class PhotoPainterDisplay : public NoDisplay {
public:
    void SetStatus(const char* status) override {
        NoDisplay::SetStatus(status);
        xiaozhi_init_received(status);
    }
    void SetChatMessage(const char* role, const char* content) override {
        NoDisplay::SetChatMessage(role, content);
        if (role && content) xiaozhi_ai_Message(role, content);
    }
};

class PhotoPainterLed : public Led {
public:
    void OnStateChanged() override {
        xiaozhi_application_received(DeviceStateMachine::GetStateName(
            Application::GetInstance().GetDeviceState()));
    }
};

#define TAG "esp-s3-PhotoPainter"

class waveshare_PhotoPainter : public WifiBoard {
  private:
    i2c_master_bus_handle_t codec_i2c_bus_;
    Button                  boot_button_;
    PowerSaveTimer         *power_save_timer_;

    void InitializeCodecI2c() {
        ESP_ERROR_CHECK(i2c_master_get_bus_handle(0, &codec_i2c_bus_));
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            {
                Settings settings("wifi", true);
                settings.SetInt("force_ap", 1);
            }
            GetDisplay()->ShowNotification("进入配网模式...");
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_restart();
        });
    }

    void InitializeTools() {
        auto &mcp_server = McpServer::GetInstance();
        mcp_server.AddTool("self.disp.refreshWeather", "仅当用户明确要求刷新天气、更新天气或显示天气预报时调用。用户仅说刷新图片、刷新屏幕、换图片时禁止调用本工具；意图不明确先询问。此工具联网更新天气。异步返回仅表示排队，禁止声称天气或图片已经刷新完成。", PropertyList(), [this](const PropertyList &) -> ReturnValue {
            if (epaper_groups == nullptr || ai_IMG_LoopGroup == nullptr) {
                return std::string("not_ready: 天气刷新任务未就绪，未提交请求。");
            }
            xEventGroupClearBits(ai_IMG_LoopGroup, 0x01);
            xEventGroupSetBits(epaper_groups, 0x01);
            return std::string("queued: 已提交天气刷新请求，尚未完成。只能回复已提交，不能声称天气或图片已刷新完成。");
        });
        mcp_server.AddTool("self.disp.refreshCurrentImage", "重新显示当前画面，不联网、不更新天气、不生成新图片。用户明确要求重新刷当前图片或屏幕时调用；仅说刷新图片且无法确定是重刷、换图还是生成时，先询问。返回 queued 仅表示已提交，禁止宣称刷新完成。", PropertyList(), [](const PropertyList &) -> ReturnValue {
            return std::string(RequestCurrentImageRefresh());
        });

        mcp_server.AddTool("self.disp.SwitchPictures", "切换本地或 SD 卡中的图片，通过整数参数指定图片序号（如 “显示第 1 张图片”）", PropertyList({Property("value", kPropertyTypeInteger, 1, sdcard_bmp_Quantity)}), [this](const PropertyList &properties) -> ReturnValue {
            int value = properties["value"].value<int>();
            sdcard_doc_count = value;
            xEventGroupSetBits(epaper_groups, 0x02);        //  0000  0010
            return std::string("queued: 已提交指定图片显示请求，尚未完成，请勿回复显示完成。");
        });

        mcp_server.AddTool("self.disp.getNumberimages", "获取 SD 卡中存储的图片文件总数，无输入参数，返回整数类型的图片数量", PropertyList(), [this](const PropertyList &) -> ReturnValue {
            xEventGroupSetBits(ai_IMG_Group, 0x02);       //Retrieve the images from the SD card
            if (xSemaphoreTake(ai_img_while_semap, pdMS_TO_TICKS(2000)) == pdTRUE) {
                return sdcard_bmp_Quantity;
            } else {
                return false;
            }
        });

        mcp_server.AddTool("self.disp.aiIMG", "仅在用户明确要求生成或画一张新图片时调用；刷新当前图片或天气不得调用。根据用户语音描述，使用 Seedream 5.0 Flash 生成 1280x720 图片，再缩放并六色抖动显示到相框。比如：帮我生成一张动漫图片。生成和刷屏异步进行，返回仅表示请求已提交。", PropertyList(), [this](const PropertyList &) -> ReturnValue {
            ESP_LOGI("MCP", "进入MCP aiIMG");
            if (!is_ai_img) { //Indicates that the image is being refreshed
                ESP_LOGE("MCP", "is_ai_img fill %d", is_ai_img);
                return std::string("busy: 屏幕正在刷新，未提交生图请求。");
            }
            xEventGroupSetBits(ai_IMG_Group, 0x01);         //Indicates that access to the volcano is permitted to obtain IMG.
            return std::string("queued: 已提交图片生成请求，生成和刷屏尚未完成，不能声称图片已生成或显示成功。");
        });

        mcp_server.AddTool("self.disp.imgloop", "进入轮询播放图片模式,循环sd卡里面的图片", PropertyList(), [this](const PropertyList &) -> ReturnValue {
            ESP_LOGI("MCP", "进入imgloop");
            xEventGroupSetBits(ai_IMG_LoopGroup, 0x01); 
            return true;
        });

        mcp_server.AddTool("self.disp.imgloopEit", "退出轮询播放图片模式,不在循环sd卡里面的图片", PropertyList(), [this](const PropertyList &) -> ReturnValue {
            ESP_LOGI("MCP", "进入imgloopEit");
            xEventGroupClearBits(ai_IMG_LoopGroup, 0x01); 
            return true;
        });

        mcp_server.AddTool("self.disp.imgsetTimerloop min", "设置轮询间隔时间,单位是分钟", PropertyList({Property("timer", kPropertyTypeInteger, 1, 60)}), [this](const PropertyList &properties) -> ReturnValue {
            ESP_LOGI("MCP", "进入imgsetTimerloop");
            int value = properties["timer"].value<int>();
            ESP_LOGE("min timer", "%d", value);
            img_loopTimer = value * 60 * 1000;
            return true;
        });

        mcp_server.AddTool("self.disp.imgsetTimerloop h", "设置轮询间隔时间,单位是小时", PropertyList({Property("timer", kPropertyTypeInteger, 1, 240)}), [this](const PropertyList &properties) -> ReturnValue {
            ESP_LOGI("MCP", "进入imgsetTimerloop");
            int value = properties["timer"].value<int>();
            ESP_LOGE("h timer", "%d", value);
            img_loopTimer = value * 3600 * 1000;
            return true;
        });

        mcp_server.AddTool("self.disp.isSHTC3", "获取设备温度和湿度", PropertyList(), [this](const PropertyList &) -> ReturnValue {
            ESP_LOGI("MCP", "进入MCP isSHTC3");
            char *str = Get_TemperatureHumidity();
            if(str) return str;
            else return NULL;
        });
    }

  public:
    waveshare_PhotoPainter()
        : boot_button_(BOOT_BUTTON_GPIO) {
        InitializeCodecI2c();
        User_xiaozhi_app_init();
        InitializeButtons();
        InitializeTools();
    }

    Display* GetDisplay() override {
        static PhotoPainterDisplay display;
        return &display;
    }

    Led* GetLed() override {
        static PhotoPainterLed led;
        return &led;
    }

    void StartNetwork() override {
        bool force_ap;
        {
            Settings settings("wifi", true);
            force_ap = settings.GetInt("force_ap", 0) == 1;
            if (force_ap) settings.SetInt("force_ap", 0);
        }
        WifiBoard::StartNetwork();
        if (force_ap && !IsInWifiConfigMode()) {
            esp_timer_stop(connect_timer_);
            WifiManager::GetInstance().StopStation();
            StartWifiConfigMode();
        }
    }

    virtual AudioCodec *GetAudioCodec() override {
        static BoxAudioCodec audio_codec(
            codec_i2c_bus_,
            AUDIO_INPUT_SAMPLE_RATE,
            AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK,
            AUDIO_I2S_GPIO_BCLK,
            AUDIO_I2S_GPIO_WS,
            AUDIO_I2S_GPIO_DOUT,
            AUDIO_I2S_GPIO_DIN,
            AUDIO_CODEC_PA_PIN,
            AUDIO_CODEC_ES8311_ADDR,
            AUDIO_CODEC_ES7210_ADDR,
            AUDIO_INPUT_REFERENCE);
        return &audio_codec;
    }
};

DECLARE_BOARD(waveshare_PhotoPainter);
