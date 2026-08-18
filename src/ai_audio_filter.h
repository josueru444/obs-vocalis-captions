#ifndef AI_AUDIO_FILTER_H
#define AI_AUDIO_FILTER_H

#include <obs-module.h>

#ifdef __cplusplus
#include <string>

struct FilterStatusInfo {
	bool has_active_filter{false};
	bool is_remote{false};
	std::string connection_status{"🔴 Desconectado"};
	std::string server_url;
	bool in_speech{false};
	std::string input_lang{"es"};
	std::string target_lang{"en"};
	bool is_paused{false};
	obs_source_t *source_context{nullptr};
};

FilterStatusInfo get_active_filter_status();
void trigger_active_filter_reconnect();
void toggle_active_filter_pause();
obs_source_t* get_active_filter_source();

extern "C" {
#endif

struct obs_source_info get_ai_filter_info();

#ifdef __cplusplus
}
#endif

#endif