#include <cstdlib>
#include <cctype>
#include <vector>
#include <random>

#include "UnityEngine/Object.hpp"

#include "GlobalNamespace/BeatmapDifficulty.hpp"
#include "GlobalNamespace/BeatmapCharacteristicSO.hpp"
#include "GlobalNamespace/BeatmapBasicData.hpp"
#include "GlobalNamespace/MenuTransitionsHelper.hpp"
#include "GlobalNamespace/StandardLevelScenesTransitionSetupDataSO.hpp"
#include "GlobalNamespace/PauseMenuManager.hpp"
#include "GlobalNamespace/GameScenesManager.hpp"
#include "GlobalNamespace/LevelBar.hpp"
#include "GlobalNamespace/ScoreUIController.hpp"
#include "GlobalNamespace/CoreGameHUDController.hpp"
#include "GlobalNamespace/ResultsViewController.hpp"
#include "GlobalNamespace/ScoreFormatter.hpp"
#include "GlobalNamespace/MultiplayerLobbyController.hpp"

#include "Zenject/DiContainer.hpp"

#include "UnityEngine/Transform.hpp"
#include "UnityEngine/RectTransform.hpp"
#include "UnityEngine/Canvas.hpp"
#include "UnityEngine/UI/Button.hpp"
#include "UnityEngine/WaitForSecondsRealtime.hpp"

#include "TMPro/TextMeshProUGUI.hpp"

#include "HMUI/CurvedTextMeshPro.hpp"

#include "System/Action_1.hpp"
#include "System/Collections/IEnumerator.hpp"

#include "custom-types/shared/delegate.hpp"

#include "songcore/shared/SongCore.hpp"
#include "songcore/shared/SongLoader/CustomBeatmapLevel.hpp"

#include "bsml/shared/BSML.hpp"

#include "endless.hpp"
#include "main.hpp"
#include "modconfig.hpp"
#include "menu.hpp"


// whether endless mode should be continued
static bool endless_should_continue(GlobalNamespace::LevelCompletionResults *lcr) {
	return 
		(lcr->levelEndStateType == GlobalNamespace::LevelCompletionResults::LevelEndStateType::Cleared) ||
		(lcr->levelEndStateType == GlobalNamespace::LevelCompletionResults::LevelEndStateType::Failed && getModConfig().continue_on_fail.GetValue());
}

MAKE_HOOK_MATCH(PauseMenuManager_Start, &GlobalNamespace::PauseMenuManager::Start, void, GlobalNamespace::PauseMenuManager *self) {
	PauseMenuManager_Start(self);
	static SafePtrUnity<UnityEngine::UI::Button> button = SafePtrUnity<UnityEngine::UI::Button>();
	if(!button) {
		// create skip button
		auto canvas = self->_levelBar->transform->parent->parent->GetComponent<UnityEngine::Canvas *>();
		RETURN_IF_NULL(canvas,);
		button.emplace(BSML::Lite::CreateUIButton(canvas->transform, "Skip", {86, -55}, [self]() {
			auto mth = UnityEngine::Object::FindObjectOfType<GlobalNamespace::MenuTransitionsHelper *>();
			RETURN_IF_NULL(mth,);
			self->enabled = false;
			mth->_gameScenesManager->PopScenes(0.f, nullptr, custom_types::MakeDelegate<System::Action_1<Zenject::DiContainer*>*>(std::function([self](Zenject::DiContainer *unused) {
				endless::next_level();
			})));
		}));
	}
	button->gameObject->SetActive(endless::state.activated);
}

MAKE_HOOK_MATCH(PauseMenuManager_MenuButtonPressed, &GlobalNamespace::PauseMenuManager::MenuButtonPressed, void, GlobalNamespace::PauseMenuManager *self) {
	PauseMenuManager_MenuButtonPressed(self);
	endless::state.activated = false;
}


MAKE_HOOK_MATCH(ScoreUIController_Start, &GlobalNamespace::ScoreUIController::Start, void, GlobalNamespace::ScoreUIController *self) {
	ScoreUIController_Start(self);
	if(!endless::state.time_text) {
		auto canvas = UnityEngine::Object::FindObjectOfType<GlobalNamespace::CoreGameHUDController *>()->_energyPanelGO->GetComponentInChildren<UnityEngine::Canvas *>();
		RETURN_IF_NULL(canvas,);
		endless::state.time_text.emplace(BSML::Lite::CreateText(canvas->transform, "", TMPro::FontStyles::Normal, 14.f, {-50, 208}, {100, 5}));
		endless::state.score_text.emplace(BSML::Lite::CreateText(canvas->transform, "", TMPro::FontStyles::Normal, 14.f, {70, 208}, {100, 5}));
	}
	bool hud_enabled = endless::state.activated && getModConfig().hud_enabled.GetValue();
	endless::state.time_text->gameObject->SetActive(hud_enabled);
	endless::state.score_text->gameObject->SetActive(hud_enabled);
	if(hud_enabled)
		endless::set_score_text(0);
}

MAKE_HOOK_MATCH(ScoreUIController_UpdateScore, &GlobalNamespace::ScoreUIController::UpdateScore, void,
	GlobalNamespace::ScoreUIController *self,
	int multipliedScore,
	int modifiedScore
) {
	ScoreUIController_UpdateScore(self, multipliedScore, modifiedScore);
	if(!endless::state.activated)
		return;
	endless::set_score_text(modifiedScore);
}

MAKE_HOOK_MATCH(MenuTransitionsHelper_HandleMainGameSceneDidFinish, &GlobalNamespace::MenuTransitionsHelper::HandleMainGameSceneDidFinish, void, 
	GlobalNamespace::MenuTransitionsHelper *self,
	GlobalNamespace::StandardLevelScenesTransitionSetupDataSO *slstsdSO,
	GlobalNamespace::LevelCompletionResults *lcr
) {
	// TODO: this doesn't record scores or anything
	if(endless::state.activated && endless_should_continue(lcr)) {
		if(lcr->levelEndStateType == GlobalNamespace::LevelCompletionResults::LevelEndStateType::Cleared)
			endless::state.score += lcr->modifiedScore;
		self->_gameScenesManager->PopScenes(0.f, nullptr, custom_types::MakeDelegate<System::Action_1<Zenject::DiContainer*>*>(std::function([self](Zenject::DiContainer *unused) {
			if(!endless::next_level())
				endless::state.activated = false;
		})));
		return;
	}
	// endless::state.activated = false;
	MenuTransitionsHelper_HandleMainGameSceneDidFinish(self, slstsdSO, lcr);
}

MAKE_HOOK_MATCH(ResultsViewController_SetDataToUI, &GlobalNamespace::ResultsViewController::SetDataToUI, void, GlobalNamespace::ResultsViewController *self) {
	
	// objects to deactivate/reactivate
	std::vector<UnityEngine::GameObject *> objects = {
		self->_levelBar->gameObject,
		self->_newHighScoreText->gameObject
	};
	// add things that aren't the score to the list of objects 
	// This also removes the label that literally says "Score". FIXME
	{
		auto child_to_keep = self->_scoreText->gameObject->transform->parent;
		auto parent = child_to_keep->parent;
		for(std::size_t i = 0; i < parent->childCount; i++) {
			auto child = parent->GetChild(i);
			if(child != child_to_keep)
				objects.push_back(child->gameObject);
		}
	}
	
	// reactivate things in case they were deactivated
	for(UnityEngine::GameObject *go : objects)
		go->SetActive(true);
	
	// call original
	ResultsViewController_SetDataToUI(self);

	// modify menu in endless
	if(endless::state.activated && self->_levelCompletionResults->levelEndStateType == GlobalNamespace::LevelCompletionResults::LevelEndStateType::Failed) {
		// set score text
		self->_scoreText->text = GlobalNamespace::ScoreFormatter::Format(endless::state.score+self->_levelCompletionResults->modifiedScore);

		// activate/deactivate things
		self->_scoreText->gameObject->transform->parent->parent->gameObject->SetActive(true);
		self->_scoreText->gameObject->transform->parent->gameObject->SetActive(true);
		self->_scoreText->gameObject->SetActive(true);
		
		for(UnityEngine::GameObject *go : objects)
			go->SetActive(false);

		// deactivate endless now that we're done
		endless::state.activated = false;
	}
}



namespace endless {
	State state;

  struct PlaylistSongDifficultyInfo {
    GlobalNamespace::BeatmapDifficulty difficulty;
    std::string characteristic;
  };

  static const std::vector<GlobalNamespace::BeatmapDifficulty>& get_difficulty_order_desc() {
    static const std::vector<GlobalNamespace::BeatmapDifficulty> order = {
      GlobalNamespace::BeatmapDifficulty::ExpertPlus,
      GlobalNamespace::BeatmapDifficulty::Expert,
      GlobalNamespace::BeatmapDifficulty::Hard,
      GlobalNamespace::BeatmapDifficulty::Normal,
      GlobalNamespace::BeatmapDifficulty::Easy
    };
    return order;
  }

  static bool level_has_difficulty(GlobalNamespace::BeatmapLevel *level, GlobalNamespace::BeatmapCharacteristicSO *characteristic, GlobalNamespace::BeatmapDifficulty difficulty) {
    if(level == nullptr || characteristic == nullptr)
      return false;
    return level->GetDifficultyBeatmapData(characteristic, difficulty) != nullptr;
  }

  static bool level_has_characteristic(GlobalNamespace::BeatmapLevel *level, GlobalNamespace::BeatmapCharacteristicSO *characteristic) {
    for(auto difficulty : get_difficulty_order_desc()) {
      if(level_has_difficulty(level, characteristic, difficulty))
        return true;
    }
    return false;
  }

  static std::optional<GlobalNamespace::BeatmapDifficulty> get_hardest_difficulty(GlobalNamespace::BeatmapLevel *level, GlobalNamespace::BeatmapCharacteristicSO *characteristic) {
    for(auto difficulty : get_difficulty_order_desc()) {
      if(level_has_difficulty(level, characteristic, difficulty))
        return difficulty;
    }
    return std::nullopt;
  }

  static std::string normalize_difficulty_name(std::string value) {
    std::string normalized;
    normalized.reserve(value.size());
    for(char c : value) {
      if(c == '+') {
        normalized += "plus";
        continue;
      }
      if(!std::isalnum(static_cast<unsigned char>(c)))
        continue;
      normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return normalized;
  }

  static std::string normalize_level_id(std::string value) {
    std::string normalized;
    normalized.reserve(value.size());
    for(char c : value)
      normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return normalized;
  }

  static std::optional<GlobalNamespace::BeatmapDifficulty> canonicalize_difficulty(std::string value) {
    auto normalized = normalize_difficulty_name(value);
    if(normalized == "0")
      return GlobalNamespace::BeatmapDifficulty::Easy;
    if(normalized == "1")
      return GlobalNamespace::BeatmapDifficulty::Normal;
    if(normalized == "2")
      return GlobalNamespace::BeatmapDifficulty::Hard;
    if(normalized == "3")
      return GlobalNamespace::BeatmapDifficulty::Expert;
    if(normalized == "4")
      return GlobalNamespace::BeatmapDifficulty::ExpertPlus;
    if(normalized == "easy")
      return GlobalNamespace::BeatmapDifficulty::Easy;
    if(normalized == "normal")
      return GlobalNamespace::BeatmapDifficulty::Normal;
    if(normalized == "hard")
      return GlobalNamespace::BeatmapDifficulty::Hard;
    if(normalized == "expert")
      return GlobalNamespace::BeatmapDifficulty::Expert;
    if(normalized == "expertplus")
      return GlobalNamespace::BeatmapDifficulty::ExpertPlus;
    return std::nullopt;
  }

  static std::optional<PlaylistSongDifficultyInfo> get_playlist_song_difficulty(PlaylistCore::BPSong *song) {
    if(song == nullptr || !song->Difficulties || song->Difficulties->size() == 0)
      return std::nullopt;
    std::optional<PlaylistSongDifficultyInfo> ret = std::nullopt;
    for(auto const &song_difficulty : song->Difficulties.value()) {
      auto difficulty = canonicalize_difficulty(song_difficulty.Name);
      if(!difficulty.has_value())
        continue;
      if(!ret.has_value() || static_cast<int32_t>(difficulty.value()) > static_cast<int32_t>(ret->difficulty))
        ret = PlaylistSongDifficultyInfo{difficulty.value(), song_difficulty.Characteristic};
    }
    return ret;
  }

  static std::string get_playlist_song_characteristic_name(PlaylistCore::BPSong *song) {
    if(song == nullptr || !song->Difficulties || song->Difficulties->size() == 0)
      return "";
    for(auto const &song_difficulty : song->Difficulties.value()) {
      if(song_difficulty.Characteristic.empty())
        continue;
      return song_difficulty.Characteristic;
    }
    return "";
  }

  static PlaylistCore::BPSong *find_playlist_song_for_level(PlaylistCore::Playlist *playlist, GlobalNamespace::BeatmapLevel *level) {
    if(playlist == nullptr || level == nullptr)
      return nullptr;
    auto level_id = normalize_level_id(static_cast<std::string>(level->levelID));
    for(auto &song : playlist->playlistJSON.Songs) {
      if(normalize_level_id(song.LevelID) == level_id)
        return &song;
    }
    return nullptr;
  }

  static GlobalNamespace::BeatmapCharacteristicSO *get_default_characteristic_for_level(GlobalNamespace::BeatmapLevel *level, std::vector<GlobalNamespace::BeatmapCharacteristicSO *> const& characteristics) {
    auto standard = get_characteristic("Standard");
    if(level_has_characteristic(level, standard))
      return standard;
    for(auto characteristic : characteristics) {
      if(characteristic == nullptr || characteristic->_serializedName == "MissingCharacteristic")
        continue;
      if(level_has_characteristic(level, characteristic))
        return characteristic;
    }
    return nullptr;
  }

  static GlobalNamespace::BeatmapCharacteristicSO *get_valid_characteristic_for_level(GlobalNamespace::BeatmapLevel *level, std::string value) {
    if(value.empty())
      return nullptr;
    auto characteristic = get_characteristic(value);
    if(!level_has_characteristic(level, characteristic))
      return nullptr;
    return characteristic;
  }

  static std::vector<GlobalNamespace::BeatmapCharacteristicSO *> get_characteristic_priority(GlobalNamespace::BeatmapLevel *level, GlobalNamespace::BeatmapCharacteristicSO *selected_characteristic, GlobalNamespace::BeatmapCharacteristicSO *playlist_characteristic, std::vector<GlobalNamespace::BeatmapCharacteristicSO *> const& characteristics) {
    std::vector<GlobalNamespace::BeatmapCharacteristicSO *> ret;
    auto add_characteristic = [&ret, level](GlobalNamespace::BeatmapCharacteristicSO *characteristic) {
      if(characteristic == nullptr || !level_has_characteristic(level, characteristic))
        return;
      for(auto existing : ret) {
        if(existing == characteristic)
          return;
      }
      ret.push_back(characteristic);
    };
    if(selected_characteristic != nullptr) {
      add_characteristic(selected_characteristic);
      return ret;
    }
    add_characteristic(playlist_characteristic);
    add_characteristic(get_default_characteristic_for_level(level, characteristics));
    for(auto characteristic : characteristics) {
      if(characteristic == nullptr || characteristic->_serializedName == "MissingCharacteristic")
        continue;
      add_characteristic(characteristic);
    }
    return ret;
  }

  static std::optional<LevelParams> resolve_level_params(GlobalNamespace::BeatmapLevel *level, PlaylistCore::BPSong *playlist_song, std::optional<GlobalNamespace::BeatmapDifficulty> selected_difficulty, GlobalNamespace::BeatmapCharacteristicSO *selected_characteristic, std::vector<GlobalNamespace::BeatmapCharacteristicSO *> const& characteristics) {
    if(level == nullptr)
      return std::nullopt;
    auto playlist_song_difficulty = get_playlist_song_difficulty(playlist_song);
    GlobalNamespace::BeatmapCharacteristicSO *playlist_characteristic = nullptr;
    if(playlist_song_difficulty.has_value())
      playlist_characteristic = get_valid_characteristic_for_level(level, playlist_song_difficulty->characteristic);
    if(playlist_characteristic == nullptr)
      playlist_characteristic = get_valid_characteristic_for_level(level, get_playlist_song_characteristic_name(playlist_song));
    auto characteristic_priority = get_characteristic_priority(level, selected_characteristic, playlist_characteristic, characteristics);
    if(characteristic_priority.size() == 0)
      return std::nullopt;
    auto resolved_difficulty = selected_difficulty;
    if(!resolved_difficulty.has_value() && playlist_song_difficulty.has_value())
      resolved_difficulty = playlist_song_difficulty->difficulty;
    for(auto characteristic : characteristic_priority) {
      if(resolved_difficulty.has_value() && level_has_difficulty(level, characteristic, resolved_difficulty.value()))
        return LevelParams{level, characteristic, resolved_difficulty.value()};
      if(selected_difficulty.has_value())
        continue;
      auto hardest = get_hardest_difficulty(level, characteristic);
      if(hardest.has_value())
        return LevelParams{level, characteristic, hardest.value()};
    }
    return std::nullopt;
  }

  static bool level_passes_mod_filters(LevelParams level_params, double min_nps, double max_nps) {
    // make sure combination exists
    auto data = level_params.level->GetDifficultyBeatmapData(level_params.characteristic, level_params.difficulty);
    if(data == nullptr)
      return false;
    // check NPS
    // {
    //   if(level_params.level->songDuration == 0.f)
    //     return false; // I doubt this will ever be true but if some evil bastard decides to make 0s long map this will make sure it doesn't crash
    //   float nps = static_cast<float>(data->notesCount)/level_params.level->songDuration;
    //   PaperLogger.debug("nps: {}/{} = {}", data->notesCount, level_params.level->songDuration, nps);
    //   if(nps < min_nps || nps > max_nps)
    //     return false;
    // }
    auto custom_level_opt = il2cpp_utils::try_cast<SongCore::SongLoader::CustomBeatmapLevel>(level_params.level);
    auto custom_level = custom_level_opt == std::nullopt ? nullptr : custom_level_opt.value();
    bool has_noodle = false;
    bool has_chroma = false;
    if(custom_level != nullptr) {
      auto csdi = custom_level->CustomSaveDataInfo;
      if(csdi.has_value()) {
        auto bcdbd = csdi->get().TryGetCharacteristicAndDifficulty(level_params.characteristic->_serializedName, level_params.difficulty);
        if(bcdbd != std::nullopt) {
          for(std::string requirement : bcdbd.value().get().requirements) {
            if(!SongCore::API::Capabilities::IsCapabilityRegistered(requirement))
              return false;
            if(requirement == "Noodle Extensions")
              has_noodle = true;
            else if(requirement == "Chroma")
              has_chroma = true;
          }
          for(std::string suggestion : bcdbd.value().get().suggestions) {
            if(suggestion == "Noodle Extensions")
              has_noodle = true;
            else if(suggestion == "Chroma")
              has_chroma = true;
          }
        }
      }
    }
    auto is_fine = [](bool has_mod, std::string allow_state) -> bool {
      if(allow_state == "Forbidden")
        return !has_mod;
      if(allow_state == "Required")
        return has_mod;
      return true;
    };
    if(!is_fine(has_noodle, getModConfig().noodle_extensions.GetValue()))
      return false;
    if(!is_fine(has_chroma, getModConfig().chroma.GetValue()))
      return false;
    return true;
  }
	
	// coroutine to update the time text
	static custom_types::Helpers::Coroutine update_time_coroutine() {
		while(true) {
			if(endless::state.score_text) {
				float elapsed = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now()-state.start_time).count()/1000.f;
				int32_t seconds = static_cast<int32_t>(elapsed)%60;
				int32_t minutes = static_cast<int32_t>(elapsed/60.f)%60;
				// I wonder if anyone will actually ever get to hours
				int32_t hours = static_cast<int32_t>(elapsed/3600.f);
				std::stringstream stream;
				stream << "Total Time ";
				if(hours == 0) {
					stream << minutes << ":" << std::setfill('0') << std::setw(2) << seconds;
				} else {
					stream << hours << ":" << std::setfill('0') << std::setw(2) << minutes << std::setfill('0') << std::setw(2) << seconds;
				}
				state.time_text->text = stream.str();
			}
			if(!state.activated)
				co_return;
			else
				co_yield reinterpret_cast<System::Collections::IEnumerator *>(UnityEngine::WaitForSecondsRealtime::New_ctor(1));
		}
	}
	
	bool next_level(void) {
		auto levelParams = get_next_level();
		if(levelParams == std::nullopt) {
			PaperLogger.warn("No available levels could be found.");
			return false;
		}
		PaperLogger.info("Starting next level...");
		if(!start_level(levelParams.value())) {
			PaperLogger.warn("Level could not be started.");
			return false;
		}
		return true;
	}

	void calculate_levels(bool automatic) {
		PaperLogger.info("Calculating levels...");
		state.levels = {};
		if(!SongCore::API::Loading::AreSongsLoaded())
			return;
		if(automatic) {
			// get info
			auto min_nps = getModConfig().min_nps.GetValue();
			auto max_nps = getModConfig().max_nps.GetValue();
      auto selected_difficulty = getModConfig().difficulty.GetValue() == "Any" ? std::nullopt : std::optional(string_to_difficulty(getModConfig().difficulty.GetValue()));
      GlobalNamespace::BeatmapCharacteristicSO *selected_characteristic = nullptr;
      if(getModConfig().characteristic.GetValue() != "Any") {
        selected_characteristic = get_characteristic(getModConfig().characteristic.GetValue());
        RETURN_IF_NULL(selected_characteristic,);
      }
      auto characteristics = get_characteristics();
			// get levels in the selected playlist
			std::vector<GlobalNamespace::BeatmapLevel *> levels;
			if(selected_playlist == nullptr)
				for(auto level : SongCore::API::Loading::GetAllLevels())
					levels.push_back(level);
			else {
				for(auto level : selected_playlist->playlistCS->beatmapLevels)
					levels.push_back(level);
			}
      for(auto level : levels) {
        auto playlist_song = find_playlist_song_for_level(selected_playlist, level);
        auto level_params = resolve_level_params(level, playlist_song, std::nullopt, nullptr, characteristics);
        if(!level_params.has_value())
          continue;
        if(selected_difficulty.has_value() && level_params->difficulty != selected_difficulty.value())
          continue;
        if(selected_characteristic != nullptr && level_params->characteristic->_serializedName != selected_characteristic->_serializedName)
          continue;
        if(!level_passes_mod_filters(level_params.value(), min_nps, max_nps))
          continue;
        state.levels.push_back(level_params.value());
      }
		} else {
			// playset
			if(selected_playset == -1)
				return;
			auto beatmaps = getModConfig().playsets.GetValue()[selected_playset].beatmaps;
			for(auto pbm : beatmaps) {
				auto lp = LevelParams::from_playset_beatmap(pbm);
				if(lp)
					state.levels.push_back(lp.value());
				else
					PaperLogger.warn("Found invalid level in playset.");
			}
		}
		// shuffle
		if(!getModConfig().sequential.GetValue())
			std::shuffle(state.levels.begin(), state.levels.end(), std::default_random_engine { std::random_device {}() });
	}

	void start_endless(void) {
		// start endless
		PaperLogger.info("Starting endless mode...");
		if(UnityEngine::Object::FindObjectOfType<GlobalNamespace::MultiplayerLobbyController *>() != nullptr) {
			// player is in multiplayer
			PaperLogger.info("Player is in multiplayer, probably shouldn't start Endless.");
			return;
		}
		state.level_index = 0;
		if(!next_level())
			return;
		state.activated = true;
		state.start_time = std::chrono::steady_clock::now();
		state.score = 0;
		// start timer
		{
			auto mth = UnityEngine::Object::FindObjectOfType<GlobalNamespace::MenuTransitionsHelper *>();
			if(mth != nullptr)
				mth->StartCoroutine(custom_types::Helpers::CoroutineHelper::New(update_time_coroutine()));
			else
				PaperLogger.warn("mth is null");
		}
		PaperLogger.info("Endless mode started!");
	}	
	
	void set_score_text(int score) {
		if(!endless::state.score_text)
			return;
		int total_score = score+endless::state.score;
		std::string text = "Total Score ";
		text += std::to_string(total_score);
		endless::state.score_text->text = text;
	}

	std::optional<LevelParams> get_next_level() {
		if(state.levels.size() == 0)
			return std::nullopt;
		if(state.level_index >= state.levels.size()) {
			if(getModConfig().end_after_all.GetValue())
				return std::nullopt;
			if(!getModConfig().sequential.GetValue())
				std::shuffle(state.levels.begin(), state.levels.end(), std::default_random_engine { std::random_device {}() });
			state.level_index = 0;
		}
		return state.levels[state.level_index++];
	}
	void register_endless_hooks() {
		INSTALL_HOOK(PaperLogger, PauseMenuManager_Start);
		INSTALL_HOOK(PaperLogger, PauseMenuManager_MenuButtonPressed);

		INSTALL_HOOK(PaperLogger, ScoreUIController_Start);
		INSTALL_HOOK(PaperLogger, ScoreUIController_UpdateScore);

		INSTALL_HOOK(PaperLogger, MenuTransitionsHelper_HandleMainGameSceneDidFinish);

		INSTALL_HOOK(PaperLogger, ResultsViewController_SetDataToUI);
	}
}
