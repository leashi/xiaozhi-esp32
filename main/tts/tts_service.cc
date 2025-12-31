#include "tts_service.h"

#define TAG "tts_service"

//////////////////////////////////////////////////////////////////////////
//
TtsTask::TtsTask()
{

}

TtsTask::~TtsTask()
{
    
}

//////////////////////////////////////////////////////////////////////////
//
void TtsService::handle_tts_th(void *arg)
{
    return ((TtsService *)arg)->on_handle_tts_th(arg);
}

//////////////////////////////////////////////////////////////////////////
//
TtsService::TtsService()
{

}

TtsService::~TtsService()
{
    
}

int TtsService::init()
{
    ESP_LOGI(TAG, "init");

    if (m_tts_queue == nullptr)
    {
        m_tts_queue = xQueueCreate(8, sizeof(void *));
        if (m_tts_queue == nullptr)
        {            
            return -1;
        }
    }

    // 从assets中加载TTS模型
    void* tts_model = nullptr;
    size_t tts_model_size = 0;
    std::string tts_model_name = "xiaole.dat";    
    if (!Assets::GetInstance().GetAssetData(tts_model_name, tts_model, tts_model_size)) 
    {
        ESP_LOGE(TAG, "加载TTS模型失败!"); 
        return -1;
    }
    else 
    {
        ESP_LOGI(TAG, "加载TTS模型成功, name=%s, size=%d", tts_model_name.c_str(), tts_model_size);
    }

    // 初始化TTS模型
    m_tts_voice = esp_tts_voice_set_init(&esp_tts_voice_template, tts_model); 
    if (m_tts_voice == NULL) 
    {
        ESP_LOGE(TAG, "esp_tts_voice_set_init failed!"); 
        return -1;
    } 
    else 
    {
        ESP_LOGD(TAG, "esp_tts_voice_set_init ok");
    }

    m_tts_handle = esp_tts_create(m_tts_voice);
    if (m_tts_handle == NULL) 
    {
        ESP_LOGE(TAG, "esp_tts_create failed!"); 
        return -1;
    } 
    else 
    {
        ESP_LOGD(TAG, "esp_tts_create ok");
    }

    return 0;
}

int TtsService::fini()
{
    ESP_LOGI(TAG, "fini");

    if (m_tts_th != nullptr)
    {
        vTaskDelete(m_tts_th);
        m_tts_th = nullptr;
    }    

    if (m_tts_queue != nullptr)
    {
        vQueueDelete(m_tts_queue);
        m_tts_queue = nullptr;
    }

    if (m_tts_handle != nullptr) 
    {
        esp_tts_destroy(m_tts_handle);
        m_tts_handle = NULL;
    }

    if (m_tts_voice != nullptr) 
    {
        esp_tts_voice_set_free(m_tts_voice);
        m_tts_voice = nullptr;
    }

    m_audio_service = nullptr;

    return 0;
}

int TtsService::start()
{
    ESP_LOGI(TAG, "start");

    if (m_tts_th == nullptr)
    {
        BaseType_t xTaskResult = xTaskCreate(TtsService::handle_tts_th, "tts_th", 8192, this, 5, &m_tts_th);
        if (xTaskResult != pdPASS)
        {
            return -1;
        }
    }

    return 0;
}

int TtsService::stop()
{
    ESP_LOGI(TAG, "stop");

    m_tts_stop = true;
    return 0;
}


int TtsService::push_text(const std::string& text)
{
    TtsTask *tsk = new TtsTask();
    tsk->m_text = text;

    if (xQueueSend(m_tts_queue, &tsk, 0) != pdTRUE)
    {
        delete tsk;
        return -1;
    }

    return 0;
}

void TtsService::on_handle_tts_th(void *arg)
{
    ESP_LOGI(TAG, "on_handle_tts_th start");

    m_tts_cur_ts = esp_timer_get_time()/1000LL;   //毫秒
    m_tts_last_ts = m_tts_cur_ts;
    TtsTask *tsk = nullptr;

    while (!m_tts_stop)
    {
        if (xQueueReceive(m_tts_queue, &tsk, m_tts_wait) == pdTRUE)
        {
            on_handle_tts_task(&tsk);
            if (tsk)
            {
                delete tsk;
                tsk = nullptr;
            }

            //触发超时处理
            m_tts_cur_ts = esp_timer_get_time()/1000LL;
            if ((m_tts_cur_ts - m_tts_last_ts) >= m_tts_timeout)
            {
                on_handle_timeout();
                m_tts_last_ts = m_tts_cur_ts;
            }
        }
        else
        {
            //触发超时处理
            on_handle_timeout();
            m_tts_last_ts = esp_timer_get_time()/1000LL;
        }
    }

    // Clean up at exit
    m_tts_th = nullptr;
    vTaskDelete(NULL);
}


void TtsService::on_handle_tts_task(TtsTask **tsk)
{
    ESP_LOGI(TAG, "on_handle_tts_task start");

    if (tsk == nullptr || *tsk == nullptr || (*tsk)->m_text.empty())
    {
        return;
    }

    //TTS输出格式为：单声道，16 bit @ 16000Hz。
    int ret = esp_tts_parse_chinese(m_tts_handle, (*tsk)->m_text.c_str());
    if (ret == 0) {
        ESP_LOGE(TAG, "esp_tts_parse_chinese failed!\n"); 
        return;
    }

    int len = 0;
    short *pcm = esp_tts_stream_play(m_tts_handle, &len, m_tts_speed);
    while (len > 0)
    {
        //play ...
        play_data(pcm, len);

        // next
        len = 0;
        pcm = esp_tts_stream_play(m_tts_handle, &len, m_tts_speed);
    }

    // 最后一块数据
    play_flush();   

    esp_tts_stream_reset(m_tts_handle);    
}

void TtsService::on_handle_timeout()
{
    ESP_LOGI(TAG, "on_handle_timeout start");

    static int cnt = 0;
    std::string text = "一片湖光万条柳，无人来看汉南秋。";
    text += std::to_string(++cnt);

    push_text(text);
}

void TtsService::play_data(short *pcm, int len)
{
    if (m_pcm_buf.size() < 16*1024)
    {
        m_pcm_buf.insert(m_pcm_buf.end(), pcm, pcm + len);
    }

    if (m_pcm_buf.size() >= 16*1024)
    {
        if (m_audio_service != nullptr)
        {
            m_audio_service->PushPlayback(m_pcm_buf.data(), m_pcm_buf.size());
        }

        m_pcm_buf.clear();
    }
}
void TtsService::play_flush()
{
    // 最后一块数据
    if ( m_pcm_buf.size() > 0)
    {
        if (m_audio_service != nullptr)
        {
            m_audio_service->PushPlayback(m_pcm_buf.data(), m_pcm_buf.size());
        }

        m_pcm_buf.clear();
    }
}