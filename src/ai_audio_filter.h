#ifndef AI_AUDIO_FILTER_H
#define AI_AUDIO_FILTER_H

#include <obs-module.h>

#ifdef __cplusplus
#include <string>
#include <vector>

struct ActiveFilterItem {
	void *filter_ptr{nullptr};
	std::string display_name;
};

struct FilterStatusInfo {
	bool has_active_filter{false};
	bool is_remote{false};
	std::string connection_status{"Desconectado"};
	std::string server_url;
	bool in_speech{false};
	std::string input_lang{"es"};
	std::string target_lang{"en"};
	bool is_paused{false};
	obs_source_t *source_context{nullptr};
	void *filter_ptr{nullptr};
};

std::vector<ActiveFilterItem> get_active_filter_list();
FilterStatusInfo get_active_filter_status(void *filter_ptr = nullptr);
void trigger_active_filter_reconnect(void *filter_ptr = nullptr);
void toggle_active_filter_pause(void *filter_ptr = nullptr);
void clear_active_filter_subtitles(void *filter_ptr = nullptr);
obs_source_t* get_active_filter_source(void *filter_ptr = nullptr);

extern "C" {
#endif

struct obs_source_info get_ai_filter_info();

#ifdef __cplusplus
}
#endif

#endif