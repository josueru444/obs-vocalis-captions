#include "ai_audio_filter.h"
#include "audio_processor.h"
#include "remote_transcriber.h"

#include <obs-frontend-api.h>
#include <media-io/audio-resampler.h>
#include <obs-module.h>
#include <util/threading.h>
#include <util/dstr.h>
#include <util/platform.h>
#include <obs.hpp>

#include "subtitle_animator.h"
#include <string.h>
#include <thread>
#include <mutex>
#include <vector>
#include <atomic>
#include <queue>
#include <deque>
#include <condition_variable>
#include <cmath>
#include <algorithm>
#include <cctype>
#include <unordered_set>
#include <unordered_map>
#include <map>

// Define VAD parameters
static const size_t MAX_SEGMENT_MS = 6000;      // Original: 15000
static const size_t PREROLL_MS = 200;
static const size_t OVERLAP_MS = 200;



// Define data structures
struct VadState {
	bool speaking = false;
	std::vector<float> speech_frames;
	std::vector<float> preroll;
	std::vector<float> overlap_buffer;
	size_t silence_ms = 0;
	size_t speech_ms = 0;
	size_t last_partial_ms = 0;
	size_t sentence_id = 0;
};

struct AudioSegment {
	std::vector<float>* audio;
	bool is_final;
	size_t sentence_id;
};

struct ai_filter_data {
	// Manage active transcription backend
	audio_processor *processor;       // Local Whisper processor instance
	RemoteTranscriber *remote_client; // Remote WebSocket client instance

	// Store configuration parameters
	bool use_remote_transcription; // Enable remote WebSocket server
	std::string ws_url;            // WebSocket server target URL
	std::string ws_token;          // Authentication token
	std::string connection_status{"🔴 Desconectado"};
	std::mutex status_mutex;
	obs_source_t *self_source{nullptr};
	std::string current_language;
	std::string target_language;
	bool local_translation;
	bool use_gpu;
	int whisper_threads;
	std::string current_model_path;
	std::string target_source_name;

	// Configure partial update interval
	size_t partial_send_interval_ms{1000};

	// Queue raw PCM frames for worker thread
	std::deque<std::vector<float>> pcm_raw_queue;
	std::mutex raw_pcm_mutex;
	std::condition_variable raw_cv;
	uint32_t obs_sample_rate{0};

	// Manage VAD and segment queue
	float vad_silence_rms_threshold{0.003f};
	size_t vad_min_speech_ms{250};
	size_t vad_silence_hangover_ms{800};
	VadState vad;
	std::queue<AudioSegment> segment_queue;
	std::mutex queue_mutex;
	std::condition_variable cv;
	std::thread worker_thread;
	std::atomic<bool> stop_worker;

	std::vector<std::vector<float>*> buffer_pool;
	std::mutex pool_mutex;

	audio_resampler_t *resampler;
	uint32_t resampler_src_rate;

	struct whisper_vad_context *vad_ctx{nullptr};

	// ── Auto-clear & Performance ──────────────────────────────────────────
	obs_weak_source_t *subtitle_weak_ref{nullptr};
	int auto_clear_seconds{5};
	size_t max_lines{2};

	SubtitleAnimator *animator{nullptr};
	obs_source_t *context{nullptr};

	// ── Partial transcription mode ──────────────────────────────────────────
	std::string partial_mode{"balanced"};

	bool was_skipping{false};
	std::atomic<bool> is_paused{false};
};

static std::mutex s_filters_mutex;
static std::vector<ai_filter_data*> s_active_filters;

	// Construct WebSocket URL with parameters
static std::string build_full_ws_url(const std::string &url, const std::string &token, const std::string &lang_in, const std::string &lang_out, const std::string &show_partial)
{
	auto trim_string = [](std::string s) {
		s.erase(std::remove_if(s.begin(), s.end(), [](unsigned char c) { return std::isspace(c); }), s.end());
		return s;
	};

	std::string clean_url = trim_string(url);
	std::string clean_token = trim_string(token);

	if (clean_url.empty()) return clean_url;
	std::string full_url = clean_url;
	bool has_query = (full_url.find('?') != std::string::npos);

	auto append_param = [&](const std::string &key, const std::string &val) {
		if (val.empty()) return;
		// Do not duplicate parameter if already present in full_url
		if (has_query && (full_url.find("?" + key + "=") != std::string::npos || full_url.find("&" + key + "=") != std::string::npos))
			return;

		if (has_query) full_url += "&" + key + "=" + val;
		else { full_url += "?" + key + "=" + val; has_query = true; }
	};

	append_param("token", clean_token);
	append_param("lang_in", trim_string(lang_in));
	append_param("lang_out", trim_string(lang_out));
	append_param("show_partial", show_partial);

	return full_url;
}

// Format and sanitize transcription text
static std::string format_subtitles(const std::string &text, size_t max_chars = 150)
{
	if (text.length() <= max_chars)
		return text;

	size_t start_pos = text.length() - max_chars;
	size_t space_pos = text.find_first_of(" \t\n", start_pos);

	if (space_pos != std::string::npos && space_pos < text.length() - 1)
		return text.substr(space_pos + 1);

	return text.substr(start_pos);
}

static bool is_repetitive(const std::string &text)
{
	std::vector<std::string> words;
	std::string current;
	for (char c : text) {
		if (std::isalnum(static_cast<unsigned char>(c))) {
			current += std::tolower(static_cast<unsigned char>(c));
		} else if (!current.empty()) {
			words.push_back(current);
			current.clear();
		}
	}
	if (!current.empty())
		words.push_back(current);

	if (words.size() < 4)
		return false;

	std::unordered_map<std::string, int> bigram_counts;
	for (size_t i = 0; i + 1 < words.size(); ++i) {
		std::string bigram = words[i] + " " + words[i + 1];
		bigram_counts[bigram]++;
		if (bigram_counts[bigram] >= 3)
			return true;
	}

	std::unordered_set<std::string> unique_words(words.begin(), words.end());
	float unique_ratio = (float)unique_words.size() / (float)words.size();
	if (unique_ratio < 0.35f)
		return true;

	return false;
}

static std::string sanitize_text(const std::string &text)
{
	std::string result;
	bool in_bracket = false;
	bool in_paren = false;
	bool in_asterisk = false;

	for (char c : text) {
		if (c == '[')
			in_bracket = true;
		else if (c == ']') {
			in_bracket = false;
			continue;
		} else if (c == '(')
			in_paren = true;
		else if (c == ')') {
			in_paren = false;
			continue;
		} else if (c == '*') {
			in_asterisk = !in_asterisk;
			continue;
		} else if (c == '\\') {
			continue;
		}

		if (!in_bracket && !in_paren && !in_asterisk)
			result += c;
	}

	size_t start = result.find_first_not_of(" \t\n\r");
	if (start == std::string::npos)
		return "";
	size_t end = result.find_last_not_of(" \t\n\r");
	result = result.substr(start, end - start + 1);

	if (result.length() <= 2)
		return "";

	std::string lower_res;
	for (char c : result) {
		lower_res += std::tolower(static_cast<unsigned char>(c));
	}
	if (lower_res.find("thanks for watching") != std::string::npos ||
	    lower_res.find("subtitles by") != std::string::npos ||
	    lower_res.find("subtitled by") != std::string::npos ||
	    lower_res.find("amara.org") != std::string::npos ||
	    lower_res.find("subscribe") != std::string::npos ||
	    lower_res.find("suscríbete") != std::string::npos) {
		return "";
	}

	if (is_repetitive(result))
		return "";

	return result;
}

// Update subtitle display source
static void update_subtitle_source(ai_filter_data *data, const std::string &text, bool is_final, size_t sentence_id)
{
	if (!data || !data->animator)
		return;

	data->animator->update_text(text, is_final, sentence_id);
}

// Forward declarations — defined later in this file, used by transcription_worker
static std::vector<float> *get_audio_buffer(ai_filter_data *data);
static void _flush_segment(ai_filter_data *filter_data);

// Process queued PCM frames and dispatch audio segments
static void transcription_worker(ai_filter_data *data)
{
	while (!data->stop_worker.load()) {

		// Consume raw PCM frames from queue
		{
			std::unique_lock<std::mutex> raw_lock(data->raw_pcm_mutex);
			data->raw_cv.wait_for(raw_lock, std::chrono::milliseconds(50), [data] {
				return !data->pcm_raw_queue.empty() || data->stop_worker.load();
			});

			while (!data->pcm_raw_queue.empty()) {
				std::vector<float> raw_frame = std::move(data->pcm_raw_queue.front());
				data->pcm_raw_queue.pop_front();
				uint32_t src_rate = data->obs_sample_rate;
				raw_lock.unlock();

				// Resample audio to 16kHz
				std::vector<float> pcmf32;
				if (src_rate != 0 && src_rate != 16000) {
					if (!data->resampler || data->resampler_src_rate != src_rate) {
						if (data->resampler)
							audio_resampler_destroy(data->resampler);
						struct resample_info src_info = {src_rate, AUDIO_FORMAT_FLOAT, SPEAKERS_MONO};
						struct resample_info dst_info = {16000, AUDIO_FORMAT_FLOAT, SPEAKERS_MONO};
						data->resampler = audio_resampler_create(&dst_info, &src_info);
						data->resampler_src_rate = src_rate;
					}
					if (data->resampler) {
						const uint8_t *in[MAX_AV_PLANES] = {(const uint8_t *)raw_frame.data()};
						uint8_t *out[MAX_AV_PLANES] = {nullptr};
						uint32_t out_frames = 0;
						uint64_t ts_offset = 0;
						if (audio_resampler_resample(data->resampler, out, &out_frames,
						                             &ts_offset, in,
						                             (uint32_t)raw_frame.size())) {
							if (out_frames > 0 && out[0])
								pcmf32.assign((float *)out[0], (float *)out[0] + out_frames);
						}
					}
				} else {
					pcmf32 = std::move(raw_frame);
				}

				if (pcmf32.empty()) {
					raw_lock.lock();
					continue;
				}

				// Remove DC offset from audio frame
				float mean = 0.0f;
				for (float s : pcmf32) mean += s;
				mean /= (float)pcmf32.size();
				for (float &s : pcmf32) s -= mean;

				// Calculate RMS audio energy level
				float sum = 0.0f;
				for (float s : pcmf32) sum += s * s;
				float rms = std::sqrt(sum / (float)pcmf32.size());

				// Detect speech with Silero VAD (or fallback to RMS if VAD model missing)
				bool is_speech = false;
				if (data->vad_ctx) {
					// Let Silero VAD decide without an artificial RMS gate, as WASAPI mic levels can be low
					is_speech = whisper_vad_detect_speech_no_reset(
						data->vad_ctx, pcmf32.data(), pcmf32.size());
				} else {
					is_speech = (rms > data->vad_silence_rms_threshold * 0.5f); // Sensitive threshold for RMS fallback
				}

				size_t frame_ms = (pcmf32.size() * 1000) / 16000;

				// Update VAD speech state machine
				if (is_speech) {
					if (!data->vad.speaking) {
						data->vad.speaking = true;
						data->vad.speech_frames = data->vad.overlap_buffer;
						data->vad.speech_frames.insert(data->vad.speech_frames.end(),
						                               data->vad.preroll.begin(),
						                               data->vad.preroll.end());
						data->vad.overlap_buffer.clear();
						data->vad.speech_ms =
							(data->vad.speech_frames.size() * 1000) / 16000;
					}
					data->vad.speech_frames.insert(data->vad.speech_frames.end(),
					                               pcmf32.begin(), pcmf32.end());
					data->vad.speech_ms += frame_ms;
					data->vad.silence_ms = 0;

					const size_t interval = data->partial_send_interval_ms;
					if (interval > 0 &&
					    data->vad.speech_ms - data->vad.last_partial_ms >= interval) {
						data->vad.last_partial_ms = data->vad.speech_ms;
						AudioSegment seg;
						seg.audio = get_audio_buffer(data);
						*seg.audio = data->vad.speech_frames;
						seg.is_final = false;
						seg.sentence_id = data->vad.sentence_id;
						{
							std::lock_guard<std::mutex> lock(data->queue_mutex);
							data->segment_queue.push(seg);
						}
						data->cv.notify_one();
					}
				} else {
					data->vad.preroll.insert(data->vad.preroll.end(), pcmf32.begin(), pcmf32.end());
					if (data->vad.preroll.size() > (PREROLL_MS * 16)) {
						data->vad.preroll.erase(
							data->vad.preroll.begin(),
							data->vad.preroll.begin() +
								(data->vad.preroll.size() - (PREROLL_MS * 16)));
					}
					if (data->vad.speaking) {
						data->vad.speech_frames.insert(data->vad.speech_frames.end(),
						                               pcmf32.begin(), pcmf32.end());
						data->vad.silence_ms += frame_ms;
						if (data->vad.silence_ms >= data->vad_silence_hangover_ms) {
							_flush_segment(data);
						} else {
							const size_t interval = data->partial_send_interval_ms;
							if (interval > 0 &&
							    data->vad.speech_ms - data->vad.last_partial_ms >= interval) {
								data->vad.last_partial_ms = data->vad.speech_ms;
								AudioSegment seg;
								seg.audio = get_audio_buffer(data);
								*seg.audio = data->vad.speech_frames;
								seg.is_final = false;
								seg.sentence_id = data->vad.sentence_id;
								{
									std::lock_guard<std::mutex> lock(data->queue_mutex);
									data->segment_queue.push(seg);
								}
								data->cv.notify_one();
							}
						}
					}
				}
				if (data->vad.speaking && data->vad.speech_ms >= MAX_SEGMENT_MS)
					_flush_segment(data);

				raw_lock.lock();
			} // end while pcm_raw_queue
		} // end Stage 1 lock scope

		// Check auto-clear condition
		if (data->animator)
			data->animator->trigger_auto_clear();

		// Dispatch segments from queue
		AudioSegment segment;
		bool has_segment = false;
		{
			std::unique_lock<std::mutex> lock(data->queue_mutex);
			if (!data->segment_queue.empty()) {
				segment = data->segment_queue.front();
				data->segment_queue.pop();
				has_segment = true;

				// Discard obsolete partial segments for same sentence_id
				while (!segment.is_final && !data->segment_queue.empty()) {
					AudioSegment &next = data->segment_queue.front();
					if (!next.is_final && next.sentence_id == segment.sentence_id) {
						if (segment.audio) {
							std::lock_guard<std::mutex> pool_lock(data->pool_mutex);
							data->buffer_pool.push_back(segment.audio);
						}
						segment = next;
						data->segment_queue.pop();
					} else {
						break;
					}
				}
			}
		}

		if (has_segment) {
			if (!segment.audio || segment.audio->empty()) {
				if (segment.audio) {
					std::lock_guard<std::mutex> pool_lock(data->pool_mutex);
					data->buffer_pool.push_back(segment.audio);
				}
			} else if (data->use_remote_transcription && data->remote_client) {
				// Send segment to remote WebSocket server
				blog(LOG_DEBUG,
				     "[AI Translator] -> Remote: send segment %zu (%s, %zu samples)",
				     segment.sentence_id, segment.is_final ? "FINAL" : "PARTIAL",
				     segment.audio->size());
				data->remote_client->send_audio(*segment.audio, segment.sentence_id,
				                                segment.is_final);
			} else if (data->processor) {
				// Process segment with local Whisper model
				std::string raw_texto = data->processor->process_audio(
					*segment.audio, data->current_language, data->local_translation, "",
					data->whisper_threads);
				std::string texto = sanitize_text(raw_texto);
				if (!texto.empty() || segment.is_final)
					update_subtitle_source(data, texto, segment.is_final, segment.sentence_id);
			}

			// Return buffer to pool
			if (segment.audio) {
				segment.audio->clear();
				std::lock_guard<std::mutex> pool_lock(data->pool_mutex);
				data->buffer_pool.push_back(segment.audio);
			}
		}
	}
}

// Handle connect button click event
static bool on_connect_clicked(obs_properties_t *props, obs_property_t *p, void *data)
{
	(void)props;
	(void)p;
	ai_filter_data *fd = static_cast<ai_filter_data *>(data);
	if (!fd || !fd->remote_client)
		return false;

	std::string show_partial = "true";
	std::string full_url = build_full_ws_url(fd->ws_url, fd->ws_token,
	                                          fd->current_language, fd->target_language, show_partial);

	if (full_url.empty()) {
		blog(LOG_INFO, "[AI Translator] Connect button pressed with empty URL -> Disconnecting");
	} else {
		blog(LOG_INFO, "[AI Translator] Connect button pressed -> %s", full_url.c_str());
	}
	fd->remote_client->update_url(full_url);

	bool url_changed = (full_url != fd->remote_client->get_url());
	bool is_connected = fd->remote_client->is_connected();

	// Optimistically update the UI to show that an action is taking place.
	// The true status will be updated asynchronously.
	std::string optimistic_status;
	if (full_url.empty()) {
		optimistic_status = "🔴 Desconectado";
	} else if (url_changed || !is_connected) {
		optimistic_status = "🟡 Conectando / Verificando...";
	} else {
		// URL didn't change and we are already connected. Fetch the true status from the background thread safely.
		std::lock_guard<std::mutex> lock(fd->status_mutex);
		optimistic_status = fd->connection_status;
	}

	obs_property_t *status_prop = obs_properties_get(props, "status_label");
	if (status_prop) {
		obs_property_set_description(status_prop, ("Estado: " + optimistic_status).c_str());
	}

	return true; // Force UI refresh
}

// Exported getter for the Qt UI
std::string get_filter_connection_status(void* data) {
	ai_filter_data* fd = static_cast<ai_filter_data*>(data);
	if (!fd) return "Desconocido";
	std::lock_guard<std::mutex> lock(fd->status_mutex);
	return fd->connection_status;
}

static ai_filter_data* get_filter_locked(void *filter_ptr) {
	if (s_active_filters.empty()) return nullptr;
	if (filter_ptr) {
		for (ai_filter_data *fd : s_active_filters) {
			if (static_cast<void*>(fd) == filter_ptr) return fd;
		}
	}
	return s_active_filters.front();
}

std::vector<ActiveFilterItem> get_active_filter_list() {
	std::lock_guard<std::mutex> lock(s_filters_mutex);
	std::vector<ActiveFilterItem> list;
	for (ai_filter_data *fd : s_active_filters) {
		if (!fd) continue;
		ActiveFilterItem item;
		item.filter_ptr = static_cast<void*>(fd);
		std::string name;
		if (fd->context) {
			obs_source_t *parent = obs_filter_get_parent(fd->context);
			const char *pname = parent ? obs_source_get_name(parent) : nullptr;
			const char *fname = obs_source_get_name(fd->context);
			if (pname && pname[0] != '\0') {
				name = std::string(pname);
				if (fname && strcmp(fname, "Traductor") != 0 && strcmp(fname, "Traductor IA") != 0 && fname[0] != '\0') {
					name += " (" + std::string(fname) + ")";
				}
			} else if (fname && fname[0] != '\0') {
				name = fname;
			}
		}
		if (name.empty()) {
			name = "Micrófono " + std::to_string(list.size() + 1);
		}
		item.display_name = name;
		list.push_back(item);
	}
	return list;
}

FilterStatusInfo get_active_filter_status(void *filter_ptr) {
	FilterStatusInfo info;
	std::lock_guard<std::mutex> lock(s_filters_mutex);
	ai_filter_data* fd = get_filter_locked(filter_ptr);
	if (!fd) {
		info.has_active_filter = false;
		return info;
	}

	info.has_active_filter = true;
	info.filter_ptr = static_cast<void*>(fd);
	info.is_remote = fd->use_remote_transcription;
	{
		std::lock_guard<std::mutex> slock(fd->status_mutex);
		info.connection_status = fd->connection_status;
	}
	info.server_url = fd->ws_url;
	info.in_speech = fd->vad.speaking;
	info.input_lang = fd->current_language.empty() ? "auto" : fd->current_language;
	info.target_lang = fd->target_language.empty() ? "en" : fd->target_language;
	info.is_paused = fd->is_paused.load();
	info.source_context = fd->context;
	if (fd->context) {
		obs_source_t *parent = obs_filter_get_parent(fd->context);
		if (parent) {
			info.is_muted = obs_source_muted(parent) || !obs_source_active(parent);
		}
	}
	return info;
}

void trigger_active_filter_reconnect(void *filter_ptr) {
	std::lock_guard<std::mutex> lock(s_filters_mutex);
	ai_filter_data* fd = get_filter_locked(filter_ptr);
	if (fd && fd->use_remote_transcription && fd->remote_client) {
		blog(LOG_INFO, "[AI Translator] Triggering manual reconnect from UI...");
		std::string full_url = build_full_ws_url(fd->ws_url, fd->ws_token,
		                                         fd->current_language, fd->target_language, "true");
		{
			std::lock_guard<std::mutex> slock(fd->status_mutex);
			fd->connection_status = "🟡 Conectando...";
		}
		fd->remote_client->update_url(full_url);
	}
}

void toggle_active_filter_pause(void *filter_ptr) {
	std::lock_guard<std::mutex> lock(s_filters_mutex);
	ai_filter_data* fd = get_filter_locked(filter_ptr);
	if (fd) {
		bool current = fd->is_paused.load();
		fd->is_paused.store(!current);
		blog(LOG_INFO, "[AI Translator] Filter translation %s from UI", !current ? "PAUSED" : "RESUMED");
	}
}

void clear_active_filter_subtitles(void *filter_ptr) {
	std::lock_guard<std::mutex> lock(s_filters_mutex);
	ai_filter_data* fd = get_filter_locked(filter_ptr);
	if (fd) {
		blog(LOG_INFO, "[AI Translator] Clearing active subtitles and audio queues from UI...");

		// 1. Purge raw PCM queue
		{
			std::lock_guard<std::mutex> raw_lock(fd->raw_pcm_mutex);
			fd->pcm_raw_queue.clear();
		}

		// 2. Purge segment queue and return buffers to pool
		{
			std::lock_guard<std::mutex> q_lock(fd->queue_mutex);
			while (!fd->segment_queue.empty()) {
				if (fd->segment_queue.front().audio) {
					fd->segment_queue.front().audio->clear();
					std::lock_guard<std::mutex> pool_lock(fd->pool_mutex);
					fd->buffer_pool.push_back(fd->segment_queue.front().audio);
				}
				fd->segment_queue.pop();
			}
		}

		// 3. Reset VAD state machine and buffers
		fd->vad.speaking = false;
		fd->vad.speech_frames.clear();
		fd->vad.preroll.clear();
		fd->vad.overlap_buffer.clear();
		fd->vad.speech_ms = 0;
		fd->vad.silence_ms = 0;
		fd->vad.last_partial_ms = 0;
		// Advance sentence ID to invalidate any in-flight or delayed server responses
		fd->vad.sentence_id++;

		// 4. Reset Silero VAD state if active
		if (fd->vad_ctx) {
			whisper_vad_reset_state(fd->vad_ctx);
		}

		// 5. Clear animator display state
		if (fd->animator) {
			fd->animator->clear();
		}

		// 6. Push clear to target subtitle source
		update_subtitle_source(fd, "", true, (size_t)-1);
	}
}

obs_source_t* get_active_filter_source(void *filter_ptr) {
	std::lock_guard<std::mutex> lock(s_filters_mutex);
	ai_filter_data* fd = get_filter_locked(filter_ptr);
	if (fd) {
		return fd->context;
	}
	return nullptr;
}

// Handle remote transcription toggle event
static bool on_remote_transcription_toggled(obs_properties_t *props, obs_property_t *p,
                                             obs_data_t *settings)
{
	(void)p;
	bool use_remote = obs_data_get_bool(settings, "use_remote_transcription");
	obs_property_t *model_group = obs_properties_get(props, "grp_models");
	if (model_group)
		obs_property_set_visible(model_group, !use_remote);
	return true;
}

// Build OBS plugin properties UI
obs_properties_t *ai_filter_get_properties(void *data)
{
	(void)data;
	obs_properties_t *props = obs_properties_create();

	// ── Group 1: Local Transcription Engine (Whisper) ─────────────────────────
	obs_properties_t *group_model = obs_properties_create();

	obs_property_t *combo_model =
		obs_properties_add_list(group_model, "model_settings", "Modelo predeterminado:",
		                        OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(combo_model, "Tiny (Rápido)", "ggml-tiny.bin");
	obs_property_list_add_string(combo_model, "Base (Balanceado)", "ggml-base.bin");
	obs_property_list_add_string(combo_model, "Small (Alta Precisión)", "ggml-small.bin");
	obs_properties_add_text(
		group_model, "model_help",
		"Modelos más grandes (Small) ofrecen mayor precisión pero consumen más recursos.",
		OBS_TEXT_INFO);

	obs_properties_add_bool(group_model, "use_custom_model", "Usar modelo personalizado");
	obs_properties_add_path(group_model, "custom_model_path", "O usa un modelo local (.bin):",
	                        OBS_PATH_FILE,
	                        "Modelos Whisper (*.bin);;Todos los archivos (*.*)", NULL);

	obs_properties_add_bool(group_model, "processing_mode", "Usar Tarjeta de Video (GPU)");
	obs_properties_add_text(
		group_model, "gpu_help",
		"Nota: Si falla la transcripción, desmarca esta casilla para usar tu procesador.",
		OBS_TEXT_INFO);

	obs_properties_add_group(props, "grp_models", "1. Motor de Transcripción Local (Whisper)",
	                          OBS_GROUP_NORMAL, group_model);

	// ── Group 2: Input Language ───────────────────────────────────────────────
	obs_properties_t *group_translation = obs_properties_create();

	obs_property_t *combo_in =
		obs_properties_add_list(group_translation, "lang_in", "Idioma que vas a hablar:",
		                        OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(combo_in, "Automático", "auto");
	obs_property_list_add_string(combo_in, "Español", "es");
	obs_property_list_add_string(combo_in, "Inglés", "en");
	obs_property_list_add_string(combo_in, "Polaco", "pl");

	obs_property_t *combo_out =
		obs_properties_add_list(group_translation, "lang_out", "Idioma de Traducción (Salida):",
		                        OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(combo_out, "Mismo que el original", "original");
	obs_property_list_add_string(combo_out, "Inglés", "en");
	obs_property_list_add_string(combo_out, "Español", "es");
	obs_property_list_add_string(combo_out, "Polaco", "pl");

	obs_properties_add_text(
		group_translation, "trans_help",
		"Nota: El motor local (Whisper) solo soporta traducir hacia el Inglés. El servidor remoto soporta todos.", OBS_TEXT_INFO);

	obs_property_t *combo_target =
		obs_properties_add_list(group_translation, "target_source_name", "Componente a usar:",
		                        OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(combo_target, "(No seleccionado / Inactivo)", "");

	obs_enum_sources(
		[](void *param, obs_source_t *source) {
			obs_property_t *list = (obs_property_t *)param;
			if (strcmp(obs_source_get_unversioned_id(source), "fuente_subtitulos_ia") == 0) {
				const char *name = obs_source_get_name(source);
				obs_property_list_add_string(list, name, name);
			}
			return true;
		},
		combo_target);
	
	obs_properties_add_group(props, "grp_translation", "2. Idioma y Visualización", OBS_GROUP_NORMAL,
	                         group_translation);


	// ── Group 4: Remote Transcription via WebSocket ───────────────────────────
	obs_properties_t *group_remote = obs_properties_create();

	obs_properties_add_text(group_remote, "ws_url", "URL del servidor WebSocket:",
	                        OBS_TEXT_DEFAULT);
	obs_properties_add_text(group_remote, "ws_token", "Token de Autenticación (Opcional):",
	                        OBS_TEXT_DEFAULT);

	std::string status_msg = "🔴 Desconectado";
	if (data) {
		ai_filter_data *fd = static_cast<ai_filter_data *>(data);
		if (fd->use_remote_transcription) {
			std::lock_guard<std::mutex> lock(fd->status_mutex);
			status_msg = fd->connection_status;
		} else {
			status_msg = "⚪ Inactivo (Modo local activo)";
		}
	}
	obs_properties_add_text(group_remote, "status_label", ("Estado: " + status_msg).c_str(),
	                        OBS_TEXT_INFO);
	obs_properties_add_button2(group_remote, "connect_btn", "🔌 Conectar / Refrescar",
	                          on_connect_clicked, data);

	obs_properties_add_text(
		group_remote, "remote_info",
		"El servidor debe responder con JSON:\n"
		"{\"text\": \"...\", \"sentence_id\": N, \"is_final\": true}\n"
		"El audio se envía codificado en Opus (16kHz, mono, 24kbps).",
		OBS_TEXT_INFO);

	obs_property_t *remote_group = obs_properties_add_group(
		props, "use_remote_transcription", "3. Traducción Remota (WebSocket)",
		OBS_GROUP_CHECKABLE, group_remote);
	obs_property_set_modified_callback(remote_group, on_remote_transcription_toggled);

	obs_properties_add_int(props, "auto_clear_seconds", "Ocultar tras X segundos de silencio (0=nunca):", 0, 30, 1);

	// ── Group 4: Visualización Avanzada y Muestreo ────────────────────────────
	obs_properties_t *group_partial = obs_properties_create();

	obs_property_t *combo_partial =
		obs_properties_add_list(group_partial, "partial_mode",
		                        "Modo de actualización del texto:",
		                        OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(combo_partial, "Tiempo real  —  500 ms (Fluidez continua)", "realtime");
	obs_property_list_add_string(combo_partial, "Balanceado   —  1300 ms (Recomendado)", "balanced");
	obs_property_list_add_string(combo_partial, "Alta Precisión —  1800 ms (Frases completas)", "precision");
	obs_property_list_add_string(combo_partial, "Personalizado  —  Configurar milisegundos exactos (Mín 500 ms)", "custom");

	obs_properties_add_int_slider(group_partial, "custom_partial_interval_ms",
	                              "Intervalo personalizado (ms):", 500, 5000, 50);

	obs_properties_add_text(
		group_partial, "partial_mode_help",
		"⚠️ Advertencia: No se recomienda modificar el intervalo del muestreo del texto (por defecto es 1300 ms).\n\n"
		"• Balanceado (1300 ms - Recomendado): Equilibrio óptimo entre velocidad y estabilidad.\n"
		"• Tiempo real (500 ms): Fluidez inmediata sin pausas.\n"
		"• Alta Precisión (1800 ms): Espera más audio para mayor contexto y precisión sintáctica.\n"
		"• Personalizado: Permite ajustar libremente los milisegundos de muestreo (Mínimo: 500 ms).",
		OBS_TEXT_INFO);

	obs_properties_add_group(props, "grp_partial", "4. Visualización y Muestreo de Subtítulos",
	                         OBS_GROUP_NORMAL, group_partial);

	// ── Group 5: Advanced Options ─────────────────────────────────────────────
	obs_properties_t *group_advanced = obs_properties_create();
	obs_properties_add_int(group_advanced, "whisper_threads", "Hilos de CPU (Whisper):", 1, 8, 1);
	obs_properties_add_float_slider(group_advanced, "vad_rms", "Umbral de Silencio (RMS)", 0.001, 0.02, 0.001);
	obs_properties_add_int_slider(group_advanced, "vad_min_speech", "Tiempo mínimo de habla (ms)", 100, 2000, 100);
	obs_properties_add_int_slider(group_advanced, "vad_hangover", "Retención de silencio (ms)", 100, 2000, 100);
	
	obs_properties_add_group(props, "grp_advanced", "5. Opciones Avanzadas", OBS_GROUP_NORMAL, group_advanced);

	return props;
}

// Return filter name string
static const char *ai_filter_get_name(void *data)
{
	(void)data;
	return "Traductor";
}

// Apply updated plugin settings
static void ai_filter_update(void *data, obs_data_t *settings)
{
	ai_filter_data *fd = static_cast<ai_filter_data *>(data);

	std::string old_lang_in = fd->current_language;
	std::string old_lang_out = fd->target_language;

	// Update basic settings (no backend restart required)
	fd->target_source_name = obs_data_get_string(settings, "target_source_name");
	fd->current_language = obs_data_get_string(settings, "lang_in");
	fd->target_language = obs_data_get_string(settings, "lang_out");
	fd->local_translation = (fd->target_language == "en");
	fd->use_gpu = obs_data_get_bool(settings, "processing_mode");
	fd->whisper_threads = (int)obs_data_get_int(settings, "whisper_threads");
	fd->auto_clear_seconds = (int)obs_data_get_int(settings, "auto_clear_seconds");
	fd->max_lines = (size_t)obs_data_get_int(settings, "max_lines");
	fd->vad_silence_rms_threshold = (float)obs_data_get_double(settings, "vad_rms");
	fd->vad_min_speech_ms = (size_t)obs_data_get_int(settings, "vad_min_speech");
	fd->vad_silence_hangover_ms = (size_t)obs_data_get_int(settings, "vad_hangover");

	// Map partial_mode string -> synchronized timing presets
	{
		const char *mode = obs_data_get_string(settings, "partial_mode");
		int custom_ms = (int)obs_data_get_int(settings, "custom_partial_interval_ms");
		if (custom_ms < 500) custom_ms = 500;

		if (mode && strcmp(mode, "realtime") == 0) {
			fd->partial_send_interval_ms = 500;
			fd->vad_silence_hangover_ms = 350;
			fd->vad_min_speech_ms = 150;
			if (fd->animator) {
				fd->animator->set_partial_throttle_ms(300);
				fd->animator->set_final_display_lock_ms(150);
			}
		} else if (mode && strcmp(mode, "precision") == 0) {
			fd->partial_send_interval_ms = 1800;
			fd->vad_silence_hangover_ms = 750;
			fd->vad_min_speech_ms = 300;
			if (fd->animator) {
				fd->animator->set_partial_throttle_ms(1000);
				fd->animator->set_final_display_lock_ms(350);
			}
		} else if (mode && strcmp(mode, "custom") == 0) {
			fd->partial_send_interval_ms = (size_t)custom_ms;
			fd->vad_min_speech_ms = (size_t)obs_data_get_int(settings, "vad_min_speech");
			fd->vad_silence_hangover_ms = (size_t)obs_data_get_int(settings, "vad_hangover");
			if (fd->animator) {
				int throttle = (int)fd->partial_send_interval_ms * 2 / 3;
				if (throttle < 300) throttle = 300;
				fd->animator->set_partial_throttle_ms(throttle);
				fd->animator->set_final_display_lock_ms(250);
			}
		} else { // "balanced" or default
			fd->partial_send_interval_ms = 1300;
			fd->vad_silence_hangover_ms = 500;
			fd->vad_min_speech_ms = 250;
			if (fd->animator) {
				fd->animator->set_partial_throttle_ms(800);
				fd->animator->set_final_display_lock_ms(250);
			}
		}

		// Ensure strictly at least 500 ms floor under any circumstance
		if (fd->partial_send_interval_ms < 500) {
			fd->partial_send_interval_ms = 500;
		}

		fd->partial_mode = mode ? mode : "balanced";
	}

	if (fd->animator) {
		fd->animator->set_auto_clear_seconds(fd->auto_clear_seconds);
		fd->animator->set_max_lines(fd->max_lines);
	}

	// Determine Whisper model path
	std::string old_path = fd->current_model_path;
	std::string new_path;
	bool use_custom = obs_data_get_bool(settings, "use_custom_model");
	const char *custom_path = obs_data_get_string(settings, "custom_model_path");

	if (use_custom && custom_path && custom_path[0] != '\0') {
		new_path = custom_path;
	} else {
		const char *model_size = obs_data_get_string(settings, "model_settings");
		char rel[256];
		snprintf(rel, sizeof(rel), "models/%s", (model_size && *model_size) ? model_size : "ggml-base.bin");

		char *cfg_path = obs_module_config_path(rel);
		if (cfg_path && os_file_exists(cfg_path)) {
			new_path = cfg_path;
			bfree(cfg_path);
		} else {
			if (cfg_path) bfree(cfg_path);
			char *abs_path = obs_module_file(rel);
			if (abs_path && os_file_exists(abs_path)) {
				new_path = abs_path;
				bfree(abs_path);
			} else {
				if (abs_path) bfree(abs_path);
				blog(LOG_ERROR, "[AI Translator] Model file not found: %s", rel);
			}
		}
	}

	// Read remote mode settings
	std::string old_show_partial = "true";
	std::string old_full_url = build_full_ws_url(fd->ws_url, fd->ws_token, old_lang_in, old_lang_out, old_show_partial);
	bool old_use_remote = fd->use_remote_transcription;
	bool old_use_gpu = fd->use_gpu;

	bool new_use_remote = obs_data_get_bool(settings, "use_remote_transcription");
	bool new_use_gpu = obs_data_get_bool(settings, "processing_mode");
	std::string new_ws_url = obs_data_get_string(settings, "ws_url");
	std::string new_ws_token = obs_data_get_string(settings, "ws_token");

	std::string new_show_partial = "true";
	std::string new_full_url = build_full_ws_url(new_ws_url, new_ws_token, fd->current_language, fd->target_language, new_show_partial);
	// Save updated values in data struct
	fd->current_model_path = new_path;
	fd->use_remote_transcription = new_use_remote;
	fd->use_gpu = new_use_gpu;
	fd->ws_url = new_ws_url;
	fd->ws_token = new_ws_token;

	// ── Detect backend restart requirement ───────────────────────────────────
	bool has_backend = (fd->processor != nullptr || fd->remote_client != nullptr);
	bool mode_changed = has_backend && (new_use_remote != old_use_remote);
	bool path_changed = has_backend && !new_use_remote && (new_path != old_path);
	bool gpu_changed = has_backend && !new_use_remote && (new_use_gpu != old_use_gpu);

	// If remote mode is active and URL, token, or language parameters changed, update the remote client immediately.
	if (has_backend && !mode_changed && new_use_remote && fd->remote_client) {
		if (new_full_url != old_full_url) {
			blog(LOG_INFO, "[AI Translator] Remote URL updated -> %s", new_full_url.c_str());
			{
				std::lock_guard<std::mutex> slock(fd->status_mutex);
				fd->connection_status = "🟡 Conectando...";
			}
			fd->remote_client->update_url(new_full_url);
		}
		return;
	}

	if (!has_backend || (!mode_changed && !path_changed && !gpu_changed))
		return; // First call or no structural backend change

	blog(LOG_INFO, "[AI Translator] Reconfigure backend (mode=%s, path=%s, gpu=%s)",
	     mode_changed ? "changed" : "-", path_changed ? "changed" : "-", gpu_changed ? "changed" : "-");

	// ── 1. Stop worker thread ────────────────────────────────────────────
	fd->stop_worker.store(true);
	fd->cv.notify_all();
	if (fd->worker_thread.joinable())
		fd->worker_thread.join();

	// ── 2. Destroy previous backend ──────────────────────────────────────────
	if (fd->remote_client) {
		delete fd->remote_client;
		fd->remote_client = nullptr;
	}
	if (fd->processor) {
		delete fd->processor;
		fd->processor = nullptr;
	}

	// ── 3. Create new backend ────────────────────────────────────────────────
	if (new_use_remote) {
		blog(LOG_INFO, "[AI Translator] Start remote mode -> %s", new_full_url.c_str());
		auto result_cb = [fd](const TranscriptionResult &r) {
			std::string texto = sanitize_text(r.text);
			if (!texto.empty() || r.is_final) {
				update_subtitle_source(fd, texto, r.is_final, r.sentence_id);
			}
		};
		auto status_cb = [fd](const std::string &status_text) {
			std::lock_guard<std::mutex> lock(fd->status_mutex);
			fd->connection_status = status_text;
		};
		fd->remote_client = new RemoteTranscriber(new_full_url, result_cb, status_cb);
	} else {
		blog(LOG_INFO, "[AI Translator] Start local mode (Whisper) model: '%s' (GPU: %s)",
		     fd->current_model_path.c_str(), fd->use_gpu ? "ON" : "OFF");
		fd->processor = new audio_processor(fd->current_model_path, fd->use_gpu);
	}

	// ── 4. Restart worker thread ─────────────────────────────────────────────
	fd->stop_worker.store(false);
	fd->worker_thread = std::thread(transcription_worker, fd);
}

// ─────────────────────────────────────────────────────────────────────────────
// ai_filter_create
// ─────────────────────────────────────────────────────────────────────────────
static void update_obs_text_source(ai_filter_data *data, const std::string &display_text,
				    bool is_partial)
{
	if (!data) return;

	obs_source_t *custom_source = nullptr;
	if (data->subtitle_weak_ref) {
		custom_source = obs_weak_source_get_source(data->subtitle_weak_ref);
		if (!custom_source) {
			obs_weak_source_release(data->subtitle_weak_ref);
			data->subtitle_weak_ref = nullptr;
		} else {
			if (strcmp(obs_source_get_name(custom_source), data->target_source_name.c_str()) != 0) {
				obs_source_release(custom_source);
				custom_source = nullptr;
				obs_weak_source_release(data->subtitle_weak_ref);
				data->subtitle_weak_ref = nullptr;
			}
		}
	}
	
	if (!custom_source && !data->target_source_name.empty()) {
		using EnumParam = std::pair<obs_source_t **, std::string>;
		EnumParam param = {&custom_source, data->target_source_name};

		obs_enum_sources(
			[](void *p, obs_source_t *source) {
				auto *search = (EnumParam *)p;
				if (strcmp(obs_source_get_unversioned_id(source), "fuente_subtitulos_ia") == 0 &&
				    strcmp(obs_source_get_name(source), search->second.c_str()) == 0) {
					*(search->first) = obs_source_get_ref(source);
					return false;
				}
				return true;
			},
			&param);
		if (custom_source) {
			data->subtitle_weak_ref = obs_source_get_weak_source(custom_source);
		}
	}

	if (custom_source != nullptr) {
		std::string lang = data->target_language;
		if (lang.empty() || lang == "original") lang = data->current_language;
		if (lang == "auto" || lang.empty()) lang = "AUTO";
		for (auto &c : lang) c = toupper(c);

		obs_data_t *settings = obs_source_get_settings(custom_source);

		// Synchronize max_lines, auto_clear, and layout metrics dynamically from the subtitle source properties
		int src_max_lines = (int)obs_data_get_int(settings, "max_lines");
		int src_auto_clear = (int)obs_data_get_int(settings, "auto_clear_seconds");
		int src_width = (int)obs_data_get_int(settings, "custom_width");
		obs_data_t *font_obj = obs_data_get_obj(settings, "font");
		int src_font_size = font_obj ? (int)obs_data_get_int(font_obj, "size") : 45;
		if (font_obj) obs_data_release(font_obj);

		if (data->animator) {
			if (src_max_lines > 0)
				data->animator->set_max_lines((size_t)src_max_lines);
			if (src_auto_clear > 0)
				data->animator->set_auto_clear_seconds(src_auto_clear);
			if (src_width > 0 && src_font_size > 0)
				data->animator->set_layout_metrics(src_width, src_font_size);
		}

		obs_data_set_string(settings, "text", display_text.c_str());
		obs_data_set_string(settings, "lang_code", lang.c_str());
		
		// Pass is_partial flag so the source can apply partial/final color
		obs_data_set_bool(settings, "_is_partial", is_partial);
		obs_source_update(custom_source, settings);
		obs_data_release(settings);
		obs_source_release(custom_source);
	}
}

static void *ai_filter_create(obs_data_t *settings, obs_source_t *source)
{
	ai_filter_data *data = new ai_filter_data();
	data->context = source;

	// Animator Initialization
	auto update_cb = [data](const std::string &display_text, bool is_partial) {
		update_obs_text_source(data, display_text, is_partial);
	};
	data->animator = new SubtitleAnimator(update_cb);
	data->resampler = nullptr;
	data->resampler_src_rate = 0;
	data->stop_worker.store(false);
	data->processor = nullptr;
	data->remote_client = nullptr;
	data->use_remote_transcription = false;
	data->ws_url = "";
	data->ws_token = "";

	// Read initial settings into data struct
	ai_filter_update(data, settings);
	
	// Clear timer logic is now in transcription_worker

	// Create appropriate backend based on initial configuration
	std::string show_partial_create = "true";
	std::string full_url = build_full_ws_url(data->ws_url, data->ws_token, data->current_language, data->target_language, show_partial_create);
	if (data->use_remote_transcription) {
		blog(LOG_INFO, "[AI Translator] Create with remote mode -> %s", full_url.empty() ? "(empty url)" : full_url.c_str());
		auto result_cb = [data](const TranscriptionResult &r) {
			std::string texto = sanitize_text(r.text);
			if (!texto.empty() || r.is_final) {
				blog(LOG_INFO, "[AI Translator] <- Remote (%s): %s",
				     r.is_final ? "FINAL" : "PARTIAL", texto.c_str());
				update_subtitle_source(data, texto, r.is_final, r.sentence_id);
			}
		};
		auto status_cb = [data](const std::string &status_text) {
			std::lock_guard<std::mutex> lock(data->status_mutex);
			data->connection_status = status_text;
		};
		data->remote_client = new RemoteTranscriber(full_url, result_cb, status_cb);
	} else {
		blog(LOG_INFO, "[AI Translator] Create with local mode (Whisper) model: '%s' (GPU: %s)",
		     data->current_model_path.c_str(), data->use_gpu ? "ON" : "OFF");
		data->processor = new audio_processor(data->current_model_path, data->use_gpu);
	}

	// Initialize Silero VAD
	struct whisper_vad_context_params vad_params = whisper_vad_default_context_params();
	char *vad_model_path = obs_module_file("models/silero_vad.bin");
	if (vad_model_path) {
		data->vad_ctx = whisper_vad_init_from_file_with_params(vad_model_path, vad_params);
		bfree(vad_model_path);
	} else {
		data->vad_ctx = nullptr;
		blog(LOG_WARNING, "[AI Translator] Silero VAD model not found. Using RMS fallback.");
	}

	if (!data->vad_ctx && vad_model_path) {
		blog(LOG_WARNING, "[AI Translator] Failed to load Silero VAD model. Using RMS fallback.");
	}

	// Spawn worker thread for asynchronous transcription
	data->worker_thread = std::thread(transcription_worker, data);

	{
		std::lock_guard<std::mutex> lock(s_filters_mutex);
		s_active_filters.push_back(data);
	}

	data->vad.speech_frames.reserve(16000 * 30);
	data->vad.preroll.reserve(16000);
	data->vad.overlap_buffer.reserve(16000);

	// Overwrite any hallucinated text saved in OBS scene collection from previous sessions
	update_subtitle_source(data, "", true, (size_t)-1);

	return data;
}

// Destroy filter instance and release resources
static void ai_filter_destroy(void *data)
{
	ai_filter_data *fd = static_cast<ai_filter_data *>(data);

	{
		std::lock_guard<std::mutex> lock(s_filters_mutex);
		auto it = std::find(s_active_filters.begin(), s_active_filters.end(), fd);
		if (it != s_active_filters.end()) {
			s_active_filters.erase(it);
		}
	}

	// 1. Stop worker threads
	fd->stop_worker.store(true);
	fd->cv.notify_all();
	if (fd->worker_thread.joinable())
		fd->worker_thread.join();

	if (fd->animator) {
		delete fd->animator;
		fd->animator = nullptr;
	}
		
	if (fd->subtitle_weak_ref) {
		obs_weak_source_release(fd->subtitle_weak_ref);
		fd->subtitle_weak_ref = nullptr;
	}

	// 2. Destroy RemoteTranscriber (destructor waits for network thread)
	//    MUST happen before freeing fd to prevent callbacks to dangling memory.
	if (fd->remote_client) {
		delete fd->remote_client;
		fd->remote_client = nullptr;
	}

	// Clean up segment queue and buffer pool
	{
		std::lock_guard<std::mutex> lock(fd->queue_mutex);
		while (!fd->segment_queue.empty()) {
			if (fd->segment_queue.front().audio) {
				delete fd->segment_queue.front().audio;
			}
			fd->segment_queue.pop();
		}
	}
	{
		std::lock_guard<std::mutex> lock(fd->pool_mutex);
		for (auto* buf : fd->buffer_pool) {
			delete buf;
		}
		fd->buffer_pool.clear();
	}

	// 3. Destroy Whisper processor
	if (fd->processor) {
		delete fd->processor;
		fd->processor = nullptr;
	}

	// 4. Free VAD context and resampler
	if (fd->vad_ctx)
		whisper_vad_free(fd->vad_ctx);
	if (fd->resampler) {
		audio_resampler_destroy(fd->resampler);
		fd->resampler = nullptr;
	}

	// 5. Delete main data structure
	delete fd;
}

// Set default filter settings
static void ai_filter_get_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, "target_source_name", "");
	obs_data_set_default_string(settings, "lang_in", "es");
	obs_data_set_default_string(settings, "lang_out", "en");
	obs_data_set_default_string(settings, "model_settings", "ggml-base.bin");
	obs_data_set_default_string(settings, "custom_model_path", "");
	obs_data_set_default_int(settings, "whisper_threads", 4);
	obs_data_set_default_bool(settings, "use_custom_model", false);
	obs_data_set_default_bool(settings, "processing_mode", false);
	obs_data_set_default_bool(settings, "local_translation", false);
	// Remote mode defaults
	obs_data_set_default_bool(settings, "use_remote_transcription", false);
	obs_data_set_default_string(settings, "ws_url", "");
	obs_data_set_default_string(settings, "ws_token", "");
	obs_data_set_default_int(settings, "auto_clear_seconds", 5);
	obs_data_set_default_int(settings, "max_lines", 2);
	obs_data_set_default_string(settings, "partial_mode", "balanced");
	obs_data_set_default_int(settings, "custom_partial_interval_ms", 1000);
	obs_data_set_default_double(settings, "vad_rms", 0.003);
	obs_data_set_default_int(settings, "vad_min_speech", 250);
	obs_data_set_default_int(settings, "vad_hangover", 500);
}

// Fetch audio buffer from pool
static std::vector<float>* get_audio_buffer(ai_filter_data *data)
{
	std::lock_guard<std::mutex> lock(data->pool_mutex);
	if (!data->buffer_pool.empty()) {
		auto* buf = data->buffer_pool.back();
		data->buffer_pool.pop_back();
		return buf;
	}
	auto* buf = new std::vector<float>();
	buf->reserve(16000 * 30);
	return buf;
}

// Flush active speech segment to queue
static void _flush_segment(ai_filter_data *filter_data)
{
	if (filter_data->vad.speech_ms >= filter_data->vad_min_speech_ms) {
		AudioSegment seg;
		seg.audio = get_audio_buffer(filter_data);
		*seg.audio = filter_data->vad.speech_frames;
		seg.is_final = true;
		seg.sentence_id = filter_data->vad.sentence_id;

		{
			std::lock_guard<std::mutex> lock(filter_data->queue_mutex);
			filter_data->segment_queue.push(seg);
		}
		filter_data->cv.notify_one();

		filter_data->vad.sentence_id++;

		size_t overlap_samples = OVERLAP_MS * 16;
		if (filter_data->vad.speech_frames.size() > overlap_samples) {
			filter_data->vad.overlap_buffer.assign(
				filter_data->vad.speech_frames.end() - overlap_samples,
				filter_data->vad.speech_frames.end());
		} else {
			filter_data->vad.overlap_buffer = filter_data->vad.speech_frames;
		}
	}

	filter_data->vad.speaking = false;
	filter_data->vad.speech_frames.clear();
	filter_data->vad.speech_ms = 0;
	filter_data->vad.silence_ms = 0;
	filter_data->vad.last_partial_ms = 0;

	if (filter_data->vad_ctx)
		whisper_vad_reset_state(filter_data->vad_ctx);
}

// Process incoming audio frame on OBS audio thread
static struct obs_audio_data *ai_filter_audio(void *data, struct obs_audio_data *audio)
{
	ai_filter_data *filter_data = static_cast<ai_filter_data *>(data);

	// Check if parent source is muted or inactive
	obs_source_t *parent = obs_filter_get_parent(filter_data->context);
	if (parent) {
		bool is_muted = obs_source_muted(parent);
		bool is_active = obs_source_active(parent);
		bool should_skip = is_muted || !is_active;

		if (filter_data->was_skipping != should_skip) {
			blog(LOG_INFO, "[AI Translator] Audio processing %s (muted=%d, active=%d)", 
				should_skip ? "PAUSED (Muted/Inactive)" : "RESUMED", is_muted, is_active);
			filter_data->was_skipping = should_skip;
			if (should_skip) {
				filter_data->vad.speaking = false;
				filter_data->vad.speech_ms = 0;
				filter_data->vad.silence_ms = 0;
				if (filter_data->vad_ctx)
					whisper_vad_reset_state(filter_data->vad_ctx);
			}
		}

		if (should_skip || filter_data->is_paused.load()) {
			return audio;
		}
	}

	if (filter_data->is_paused.load()) {
		return audio;
	}

	// Only process if a transcription backend is active
	bool has_backend =
		(filter_data->processor != nullptr || filter_data->remote_client != nullptr);

	if (has_backend) {
		size_t num_samples = audio->frames;

		struct obs_audio_info oai;
		if (obs_get_audio_info(&oai) && num_samples > 0) {
			// Capture sample rate for the worker thread
			filter_data->obs_sample_rate = oai.samples_per_sec;

			// Downmix all available channels to a single mono frame
			std::vector<float> mixed_frame(num_samples, 0.0f);
			uint32_t channels = get_audio_channels(oai.speakers);
			int active_channels = 0;
			
			for (uint32_t c = 0; c < channels && c < MAX_AV_PLANES; c++) {
				if (audio->data[c] != nullptr) {
					float *chan_data = (float *)audio->data[c];
					for (size_t i = 0; i < num_samples; i++) {
						mixed_frame[i] += chan_data[i];
					}
					active_channels++;
				}
			}

			if (active_channels > 1) {
				float inv_channels = 1.0f / (float)active_channels;
				for (size_t i = 0; i < num_samples; i++) {
					mixed_frame[i] *= inv_channels;
				}
			}

			if (active_channels > 0) {
				std::lock_guard<std::mutex> lock(filter_data->raw_pcm_mutex);
				// Bound the ring buffer to avoid unbounded memory growth
				// if the worker thread falls behind (drop oldest frames)
				if (filter_data->pcm_raw_queue.size() < 200) {
					filter_data->pcm_raw_queue.push_back(std::move(mixed_frame));
				}
				filter_data->raw_cv.notify_one();
			}
		}
	}

	return audio;
}

// Register OBS filter source info
extern "C" struct obs_source_info get_ai_filter_info()
{
	struct obs_source_info info = {0};
	info.id = "ai_translation_filter";
	info.type = OBS_SOURCE_TYPE_FILTER;
	info.output_flags = OBS_SOURCE_AUDIO;
	info.get_name = ai_filter_get_name;
	info.get_defaults = ai_filter_get_defaults;
	info.update = ai_filter_update;
	info.create = ai_filter_create;
	info.destroy = ai_filter_destroy;
	info.get_properties = ai_filter_get_properties;
	info.filter_audio = ai_filter_audio;
	return info;
}