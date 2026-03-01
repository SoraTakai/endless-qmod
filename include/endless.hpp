#pragma once

#include <optional>
#include <chrono>

#include "GlobalNamespace/BeatmapLevel.hpp"
#include "GlobalNamespace/BeatmapDifficulty.hpp"
#include "GlobalNamespace/BeatmapCharacteristicSO.hpp"

#include "HMUI/CurvedTextMeshPro.hpp"

#include "misc.hpp"

namespace endless {
	// state
	struct State {
		// whether endless mode is currently activated
		bool activated = false;
		// time endless mode was started
		std::chrono::steady_clock::time_point start_time;
		// total score ammased from levels before the current one
		int score;
		// HUD text
		SafePtrUnity<HMUI::CurvedTextMeshPro> time_text;
		SafePtrUnity<HMUI::CurvedTextMeshPro> score_text;
		// current level index (used in sequential mode)
		size_t level_index;
		// pre-calculated level params (calculated in `calculate_levels()`)
		std::vector<LevelParams> levels;
	};
	extern State state;
    // calculates levels
    void calculate_levels(bool automatic);
    // calculates levels in chunks and starts endless mode from menu context
    void start_endless_from_menu(bool automatic);
    // starts endless mode
    void start_endless(void);
	// starts the next level in endless mode. Returns whether it actually started
	bool next_level(void);
	// Updates the score text with the current score on the map currently being played
	void set_score_text(int score);
	// Returns the next level, if one can be found
	std::optional<LevelParams> get_next_level();
	// registers hooks
	void register_endless_hooks(void);
}
