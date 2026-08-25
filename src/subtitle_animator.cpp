#include "subtitle_animator.h"
#include <chrono>

// ─────────────────────────────────────────────────────────────────────────────
// SubtitleAnimator — Animate and throttle subtitle display updates
// ─────────────────────────────────────────────────────────────────────────────

SubtitleAnimator::SubtitleAnimator(UpdateCallback callback)
	: m_callback(callback), m_stop(false)
{
	m_last_update_time = std::chrono::steady_clock::now();
	m_worker = std::thread(&SubtitleAnimator::worker_loop, this);
}

SubtitleAnimator::~SubtitleAnimator()
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_stop = true;
	}
	m_cv.notify_all();
	if (m_worker.joinable())
		m_worker.join();
}

#include <algorithm>
#include <cmath>
#include <sstream>
#include <vector>

static size_t count_utf8_characters(const std::string &s)
{
	size_t count = 0;
	for (char c : s) {
		if ((c & 0xC0) != 0x80)
			count++;
	}
	return count;
}

static std::vector<std::string> split_text_into_lines(const std::string &text, int chars_per_line)
{
	std::vector<std::string> lines;
	if (text.empty()) return lines;
	if (chars_per_line <= 0) chars_per_line = 38;

	std::stringstream ss(text);
	std::string paragraph;
	while (std::getline(ss, paragraph)) {
		if (paragraph.empty()) continue;

		std::stringstream words_ss(paragraph);
		std::string word;
		std::string current_line;
		size_t current_line_chars = 0;

		while (words_ss >> word) {
			size_t word_chars = count_utf8_characters(word);

			if (current_line.empty()) {
				current_line = word;
				current_line_chars = word_chars;
			} else if (current_line_chars + 1 + word_chars <= (size_t)chars_per_line) {
				current_line += " " + word;
				current_line_chars += 1 + word_chars;
			} else {
				lines.push_back(current_line);
				current_line = word;
				current_line_chars = word_chars;
			}
		}
		if (!current_line.empty()) {
			lines.push_back(current_line);
		}
	}
	return lines;
}

void SubtitleAnimator::set_max_lines(size_t lines)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_max_lines = (lines > 0) ? lines : 3;
	rebuild_target_strings();
	m_cv.notify_one();
}

void SubtitleAnimator::set_auto_clear_seconds(int seconds)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_auto_clear_seconds = seconds;
}

void SubtitleAnimator::set_layout_metrics(int custom_width, int font_size)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	bool changed = false;
	if (custom_width > 0 && custom_width != m_custom_width) {
		m_custom_width = custom_width;
		changed = true;
	}
	if (font_size > 0 && font_size != m_font_size) {
		m_font_size = font_size;
		changed = true;
	}
	if (changed) {
		rebuild_target_strings();
		m_cv.notify_one();
	}
}

void SubtitleAnimator::set_partial_throttle_ms(int ms)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_partial_throttle_ms = ms;
}

void SubtitleAnimator::set_final_display_lock_ms(int ms)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_final_display_lock_ms = ms;
}

void SubtitleAnimator::clear()
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_confirmed_lines.clear();
		m_active_partial.clear();
		m_committed_prefix.clear();
		m_target_confirmed_string = "";
		m_target_partial_string = "";
		m_last_update_time = std::chrono::steady_clock::now();
	}
	if (m_callback) {
		m_callback("", false);
	}
	m_cv.notify_one();
}

void SubtitleAnimator::trigger_auto_clear()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (m_auto_clear_seconds > 0 &&
	    (!m_target_confirmed_string.empty() || !m_target_partial_string.empty())) {
		auto now = std::chrono::steady_clock::now();
		auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
			now - m_last_update_time).count();

		size_t total_chars = count_utf8_characters(m_target_confirmed_string) +
		                     count_utf8_characters(m_target_partial_string);

		// Adaptive reading rate: ~15-16 chars/second (Netflix/BBC timed text standard)
		// Minimum 2000ms, scaling dynamically with text length, clamped to user setting
		int reading_duration_ms = (int)((float)total_chars / 15.0f * 1000.0f) + 1200;
		int min_ms = 2000;
		int max_ms = std::max(4000, m_auto_clear_seconds * 1000);
		int dynamic_timeout_ms = std::clamp(reading_duration_ms, min_ms, max_ms);

		if (elapsed_ms >= dynamic_timeout_ms) {
			m_confirmed_lines.clear();
			m_active_partial.clear();
			m_committed_prefix.clear();
			m_target_confirmed_string = "";
			m_target_partial_string = "";
			m_cv.notify_one();
		}
	}
}

void SubtitleAnimator::rebuild_target_strings()
{
	// Calculate estimated characters per line based on layout metrics
	float avg_char_width = (float)m_font_size * 0.50f;
	if (avg_char_width <= 0.0f) avg_char_width = 22.5f;
	int chars_per_line = (int)((float)m_custom_width / avg_char_width);
	if (chars_per_line < 10) chars_per_line = 38;

	size_t max_lines = (m_max_lines > 0) ? m_max_lines : 3;

	// 1. Extract and split active partial sentence into lines
	std::vector<std::string> partial_lines;
	m_target_partial_string = "";
	if (!m_active_partial.empty()) {
		std::string raw_partial = m_active_partial.rbegin()->second;
		partial_lines = split_text_into_lines(raw_partial, chars_per_line);
	}

	// 2. Determine how many lines partial takes (up to max_lines)
	size_t num_partial_lines = std::min(partial_lines.size(), max_lines);
	size_t num_confirmed_lines_needed = max_lines - num_partial_lines;

	// 3. Build confirmed target string (top lines in FIFO roll-up order)
	m_target_confirmed_string = "";
	if (num_confirmed_lines_needed > 0 && !m_confirmed_lines.empty()) {
		size_t count = m_confirmed_lines.size();
		size_t start_idx = (count > num_confirmed_lines_needed) ? (count - num_confirmed_lines_needed) : 0;
		for (size_t i = start_idx; i < count; ++i) {
			if (!m_target_confirmed_string.empty())
				m_target_confirmed_string += "\n";
			m_target_confirmed_string += m_confirmed_lines[i];
		}
	}

	// 4. Build partial target string (bottom lines)
	if (num_partial_lines > 0) {
		size_t p_start = partial_lines.size() - num_partial_lines;
		for (size_t i = p_start; i < partial_lines.size(); ++i) {
			if (!m_target_partial_string.empty())
				m_target_partial_string += "\n";
			m_target_partial_string += partial_lines[i];
		}
	}
}

// Update target subtitle text for confirmed or partial results
// sentence_id = -1 triggers hard reset
void SubtitleAnimator::update_text(const std::string &text, bool is_final,
                                   size_t sentence_id)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_last_update_time = std::chrono::steady_clock::now();

	if (sentence_id == (size_t)-1) {
		// Perform hard reset
		m_confirmed_lines.clear();
		m_active_partial.clear();
		m_committed_prefix.clear();
		m_target_confirmed_string = "";
		m_target_partial_string = "";
		m_cv.notify_one();
		return;
	}

	if (is_final) {
		// Clear completed partial entries
		for (auto it = m_active_partial.begin(); it != m_active_partial.end(); ) {
			if (it->first <= sentence_id)
				it = m_active_partial.erase(it);
			else
				++it;
		}

		// Clear committed prefix entries for completed sentences
		for (auto it = m_committed_prefix.begin(); it != m_committed_prefix.end(); ) {
			if (it->first <= sentence_id)
				it = m_committed_prefix.erase(it);
			else
				++it;
		}

		// Split finalized sentence into individual visual lines and push into FIFO line queue
		if (!text.empty()) {
			float avg_char_width = (float)m_font_size * 0.50f;
			if (avg_char_width <= 0.0f) avg_char_width = 22.5f;
			int chars_per_line = (int)((float)m_custom_width / avg_char_width);
			if (chars_per_line < 10) chars_per_line = 38;

			std::vector<std::string> new_lines = split_text_into_lines(text, chars_per_line);
			for (const auto &line : new_lines) {
				if (!line.empty()) {
					m_confirmed_lines.push_back(line);
				}
			}
			while (m_confirmed_lines.size() > 50)
				m_confirmed_lines.pop_front();
		}
		m_last_final_time = std::chrono::steady_clock::now();
	} else {
		// Update active partial entry with committed prefix logic
		if (text.empty()) {
			m_active_partial.erase(sentence_id);
			m_committed_prefix.erase(sentence_id);
		} else {
			auto cp_it = m_committed_prefix.find(sentence_id);
			if (cp_it != m_committed_prefix.end() && !cp_it->second.empty()) {
				const std::string &committed = cp_it->second;
				// Check if new text starts with the committed prefix
				if (text.size() >= committed.size() &&
				    text.compare(0, committed.size(), committed) == 0) {
					// Text grew naturally — accept and extend prefix
					m_committed_prefix[sentence_id] = text;
				} else if (text.size() > committed.size()) {
					// Text is longer but start changed — accept the
					// correction (AI refined the translation)
					m_committed_prefix[sentence_id] = text;
				} else {
					// Text is shorter or same length with different start.
					// Suppress this update — wait for final to correct.
					rebuild_target_strings();
					m_cv.notify_one();
					return;
				}
			} else {
				// First partial for this sentence_id
				m_committed_prefix[sentence_id] = text;
			}
			m_active_partial[sentence_id] = text;
		}

		// Evict stale partial entries
		for (auto it = m_active_partial.begin(); it != m_active_partial.end(); ) {
			if (it->first < sentence_id)
				it = m_active_partial.erase(it);
			else
				++it;
		}
		// Evict stale committed prefix entries
		for (auto it = m_committed_prefix.begin(); it != m_committed_prefix.end(); ) {
			if (it->first < sentence_id)
				it = m_committed_prefix.erase(it);
			else
				++it;
		}
	}

	rebuild_target_strings();
	m_cv.notify_one();
}

// ─────────────────────────────────────────────────────────────────────────────
// worker_loop — display thread
// ─────────────────────────────────────────────────────────────────────────────
void SubtitleAnimator::worker_loop()
{
	std::string current_partial = "";
	std::string current_confirmed = "";
	auto last_partial_tick = std::chrono::steady_clock::now();

	while (true) {
		std::string target_confirmed;
		std::string target_partial;
		int throttle_ms;

		int final_lock_ms;
		std::chrono::steady_clock::time_point last_final_tp;
		{
			std::unique_lock<std::mutex> lock(m_mutex);
			m_cv.wait_for(lock, std::chrono::milliseconds(50),
				[this, &current_partial, &current_confirmed]() {
					return m_stop
					    || m_target_partial_string != current_partial
					    || m_target_confirmed_string != current_confirmed;
				});

			if (m_stop) break;

			target_confirmed = m_target_confirmed_string;
			target_partial   = m_target_partial_string;
			throttle_ms      = m_partial_throttle_ms;
			final_lock_ms    = m_final_display_lock_ms;
			last_final_tp    = m_last_final_time;
		}

		if (target_confirmed == current_confirmed && target_partial == current_partial)
			continue;

		// Apply immediate update when confirmed text changes
		if (target_confirmed != current_confirmed) {
			current_confirmed = target_confirmed;
			current_partial   = target_partial;

			std::string display = "";
			if (!current_confirmed.empty() && !current_partial.empty()) {
				display = current_confirmed + "\n" + current_partial;
			} else if (!current_confirmed.empty()) {
				display = current_confirmed;
			} else if (!current_partial.empty()) {
				display = current_partial;
			}

			if (m_callback) m_callback(display, !current_partial.empty());
			last_partial_tick = std::chrono::steady_clock::now();
			continue;
		}

		// Throttle partial text updates
		auto now  = std::chrono::steady_clock::now();
		auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(
			now - last_partial_tick).count();

		// Display lock: suppress partial updates briefly after a final
		auto since_final = std::chrono::duration_cast<std::chrono::milliseconds>(
			now - last_final_tp).count();
		if (since_final < final_lock_ms) {
			continue;
		}

		// Check if partial text was cleared
		bool partial_cleared = target_partial.empty() && !current_partial.empty();

		if (!partial_cleared && diff < throttle_ms)
			continue;

		current_partial = target_partial;

		std::string display = "";
		if (!current_confirmed.empty() && !current_partial.empty()) {
			display = current_confirmed + "\n" + current_partial;
		} else if (!current_confirmed.empty()) {
			display = current_confirmed;
		} else if (!current_partial.empty()) {
			display = current_partial;
		}

		if (m_callback) m_callback(display, !current_partial.empty());
		last_partial_tick = now;
	}
}

