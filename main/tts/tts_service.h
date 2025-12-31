#pragma once
#ifndef TTS_SERVICE_H_20251230
#define TTS_SERVICE_H_20251230

#include <esp_err.h>
#include <esp_log.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <esp_partition.h>
#include <esp_tts.h>
#include <esp_tts_voice_xiaole.h>
#include <esp_tts_voice_template.h>

#include <string>
#include <stdint.h>
#include <vector>

#include "audio_service.h"

//////////////////////////////////////////////////////////////////////////
//
class TtsTask
{
public:
    TtsTask();
    ~TtsTask();

public:
    std::string m_text;
    std::vector<int16_t> m_pcm;

public:
    
};

//////////////////////////////////////////////////////////////////////////
//
class TtsService {
public:    
    static void handle_tts_th(void *arg);

public:
    TtsService();
    ~TtsService();

public:
    void set_audio_service(AudioService * audio_service) { m_audio_service = audio_service; }
    AudioService * get_audio_service() { return m_audio_service; }

public:
    int init();
    int fini();

    int start();
    int stop();

    int push_text(const std::string& text);

protected:
    void on_handle_tts_th(void *arg);
    void on_handle_tts_task(TtsTask **tsk);
    void on_handle_timeout();

    void play_data(short *pcm, int len);
    void play_flush();

protected:
    // tts task
    QueueHandle_t m_tts_queue = nullptr;
    TaskHandle_t m_tts_th = nullptr;
    bool m_tts_stop = false;
    int64_t m_tts_cur_ts = 0;       //毫秒
    int64_t m_tts_last_ts = 0;      //毫秒
    int32_t m_tts_timeout = 30000;  //毫秒
    int32_t m_tts_wait = pdMS_TO_TICKS(30000); //毫秒
    int32_t m_tts_speed = 2;    //range:0~5, 0: the slowest speed, 5: the fastest speech 

    // tts model
    esp_tts_voice_t * m_tts_voice = nullptr;
    esp_tts_handle_t m_tts_handle = nullptr;

    std::vector<short> m_pcm_buf;

    AudioService * m_audio_service = nullptr;
};


#endif // TTS_SERVICE_H_20251230