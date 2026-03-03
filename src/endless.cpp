#include <cstdlib>
#include <cctype>
#include <vector>
#include <random>
#include <unordered_map>

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

  static std::mt19937& get_random_engine() {
    static std::mt19937 engine(std::random_device { }());
    return engine;
  }

  static bool mod_allow_state_matches(bool has_mod, std::string const& allow_state) {
    if(allow_state == "Forbidden")
      return !has_mod;
    if(allow_state == "Required")
      return has_mod;
    return true;
  }

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

  static GlobalNamespace::BeatmapCharacteristicSO *get_preferred_expertplus_characteristic_for_level(GlobalNamespace::BeatmapLevel *level, GlobalNamespace::BeatmapCharacteristicSO *lawless_characteristic, GlobalNamespace::BeatmapCharacteristicSO *standard_characteristic, std::vector<GlobalNamespace::BeatmapCharacteristicSO *> const& characteristics) {
    auto expert_plus = GlobalNamespace::BeatmapDifficulty::ExpertPlus;
    if(level_has_difficulty(level, lawless_characteristic, expert_plus))
      return lawless_characteristic;
    if(level_has_difficulty(level, standard_characteristic, expert_plus))
      return standard_characteristic;
    for(auto characteristic : characteristics) {
      if(characteristic == nullptr || characteristic->_serializedName == "MissingCharacteristic")
        continue;
      if(level_has_difficulty(level, characteristic, expert_plus))
        return characteristic;
    }
    return nullptr;
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

  static std::unordered_map<std::string, PlaylistCore::BPSong *> build_playlist_song_lookup(PlaylistCore::Playlist *playlist) {
    std::unordered_map<std::string, PlaylistCore::BPSong *> lookup;
    if(playlist == nullptr)
      return lookup;
    lookup.reserve(playlist->playlistJSON.Songs.size());
    for(auto &song : playlist->playlistJSON.Songs)
      lookup[normalize_level_id(song.LevelID)] = &song;
    return lookup;
  }

  static PlaylistCore::BPSong *find_playlist_song_for_level(std::unordered_map<std::string, PlaylistCore::BPSong *> const& playlist_song_lookup, GlobalNamespace::BeatmapLevel *level) {
    if(level == nullptr)
      return nullptr;
    auto it = playlist_song_lookup.find(normalize_level_id(static_cast<std::string>(level->levelID)));
    return it == playlist_song_lookup.end() ? nullptr : it->second;
  }

  static std::unordered_map<std::string, GlobalNamespace::BeatmapCharacteristicSO *> build_characteristic_lookup(std::vector<GlobalNamespace::BeatmapCharacteristicSO *> const& characteristics) {
    std::unordered_map<std::string, GlobalNamespace::BeatmapCharacteristicSO *> lookup;
    lookup.reserve(characteristics.size());
    for(auto characteristic : characteristics) {
      if(characteristic == nullptr || characteristic->_serializedName == "MissingCharacteristic")
        continue;
      lookup[characteristic->_serializedName] = characteristic;
    }
    return lookup;
  }

  static GlobalNamespace::BeatmapCharacteristicSO *find_characteristic_from_lookup(std::unordered_map<std::string, GlobalNamespace::BeatmapCharacteristicSO *> const& characteristic_lookup, std::string const& name) {
    auto it = characteristic_lookup.find(name);
    if(it == characteristic_lookup.end())
      return nullptr;
    return it->second;
  }

  static GlobalNamespace::BeatmapCharacteristicSO *get_default_characteristic_for_level(GlobalNamespace::BeatmapLevel *level, GlobalNamespace::BeatmapCharacteristicSO *standard_characteristic, std::vector<GlobalNamespace::BeatmapCharacteristicSO *> const& characteristics) {
    if(level_has_characteristic(level, standard_characteristic))
      return standard_characteristic;
    for(auto characteristic : characteristics) {
      if(characteristic == nullptr || characteristic->_serializedName == "MissingCharacteristic")
        continue;
      if(level_has_characteristic(level, characteristic))
        return characteristic;
    }
    return nullptr;
  }

  static GlobalNamespace::BeatmapCharacteristicSO *get_valid_characteristic_for_level(GlobalNamespace::BeatmapLevel *level, std::unordered_map<std::string, GlobalNamespace::BeatmapCharacteristicSO *> const& characteristic_lookup, std::string const& value) {
    if(value.empty())
      return nullptr;
    auto characteristic = find_characteristic_from_lookup(characteristic_lookup, value);
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
    for(auto characteristic : characteristics) {
      if(characteristic == nullptr || characteristic->_serializedName == "MissingCharacteristic")
        continue;
      add_characteristic(characteristic);
    }
    return ret;
  }

  static void prioritize_default_characteristic(std::vector<GlobalNamespace::BeatmapCharacteristicSO *>& characteristic_priority, GlobalNamespace::BeatmapCharacteristicSO *playlist_characteristic, GlobalNamespace::BeatmapCharacteristicSO *default_characteristic) {
    if(default_characteristic == nullptr)
      return;
    int playlist_index = -1;
    int default_index = -1;
    for(int i = 0; i < characteristic_priority.size(); i++) {
      if(playlist_index == -1 && characteristic_priority[i] == playlist_characteristic)
        playlist_index = i;
      if(default_index == -1 && characteristic_priority[i] == default_characteristic)
        default_index = i;
    }
    int default_target_index = 0;
    if(playlist_characteristic != nullptr && playlist_index == 0 && default_characteristic != playlist_characteristic)
      default_target_index = 1;
    if(default_index == -1) {
      if(default_target_index > characteristic_priority.size())
        default_target_index = characteristic_priority.size();
      characteristic_priority.insert(characteristic_priority.begin()+default_target_index, default_characteristic);
      return;
    }
    if(default_index == default_target_index)
      return;
    auto default_item = characteristic_priority[default_index];
    characteristic_priority.erase(characteristic_priority.begin()+default_index);
    if(default_index < default_target_index)
      default_target_index--;
    characteristic_priority.insert(characteristic_priority.begin()+default_target_index, default_item);
  }

  static std::optional<LevelParams> resolve_level_params(GlobalNamespace::BeatmapLevel *level, PlaylistCore::BPSong *playlist_song, std::optional<GlobalNamespace::BeatmapDifficulty> selected_difficulty, GlobalNamespace::BeatmapCharacteristicSO *selected_characteristic, GlobalNamespace::BeatmapCharacteristicSO *standard_characteristic, std::vector<GlobalNamespace::BeatmapCharacteristicSO *> const& characteristics, std::unordered_map<std::string, GlobalNamespace::BeatmapCharacteristicSO *> const& characteristic_lookup) {
    if(level == nullptr)
      return std::nullopt;
    auto playlist_song_difficulty = get_playlist_song_difficulty(playlist_song);
    GlobalNamespace::BeatmapCharacteristicSO *playlist_characteristic = nullptr;
    if(playlist_song_difficulty.has_value())
      playlist_characteristic = get_valid_characteristic_for_level(level, characteristic_lookup, playlist_song_difficulty->characteristic);
    if(playlist_characteristic == nullptr)
      playlist_characteristic = get_valid_characteristic_for_level(level, characteristic_lookup, get_playlist_song_characteristic_name(playlist_song));
    auto characteristic_priority = get_characteristic_priority(level, selected_characteristic, playlist_characteristic, characteristics);
    if(selected_characteristic == nullptr) {
      auto default_characteristic = get_default_characteristic_for_level(level, standard_characteristic, characteristics);
      prioritize_default_characteristic(characteristic_priority, playlist_characteristic, default_characteristic);
    }
    if(characteristic_priority.size() == 0)
      return std::nullopt;
    auto resolved_difficulty = selected_difficulty;
    bool is_unknown_difficulty = !selected_difficulty.has_value() && !playlist_song_difficulty.has_value();
    if(is_unknown_difficulty) {
      if(selected_characteristic != nullptr) {
        if(level_has_difficulty(level, selected_characteristic, GlobalNamespace::BeatmapDifficulty::ExpertPlus))
          return LevelParams{level, selected_characteristic, GlobalNamespace::BeatmapDifficulty::ExpertPlus};
      } else {
        auto lawless_characteristic = find_characteristic_from_lookup(characteristic_lookup, "Lawless");
        auto expertplus_characteristic = get_preferred_expertplus_characteristic_for_level(level, lawless_characteristic, standard_characteristic, characteristics);
        if(expertplus_characteristic != nullptr)
          return LevelParams{level, expertplus_characteristic, GlobalNamespace::BeatmapDifficulty::ExpertPlus};
      }
    }
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

  static bool level_passes_mod_filters(LevelParams level_params, bool requires_mod_presence_filter, std::string const& noodle_allow_state, std::string const& chroma_allow_state) {
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
            if(requires_mod_presence_filter) {
              if(requirement == "Noodle Extensions")
                has_noodle = true;
              else if(requirement == "Chroma")
                has_chroma = true;
            }
          }
          if(requires_mod_presence_filter) {
            for(std::string suggestion : bcdbd.value().get().suggestions) {
              if(suggestion == "Noodle Extensions")
                has_noodle = true;
              else if(suggestion == "Chroma")
                has_chroma = true;
            }
          }
        }
      }
    }
    if(!requires_mod_presence_filter)
      return true;
    if(!mod_allow_state_matches(has_noodle, noodle_allow_state))
      return false;
    if(!mod_allow_state_matches(has_chroma, chroma_allow_state))
      return false;
    return true;
  }

  struct LevelCalculationContext {
    bool automatic = false;
    std::size_t index = 0;
    std::optional<GlobalNamespace::BeatmapDifficulty> selected_difficulty = std::nullopt;
    GlobalNamespace::BeatmapCharacteristicSO *selected_characteristic = nullptr;
    GlobalNamespace::BeatmapCharacteristicSO *standard_characteristic = nullptr;
    std::string noodle_allow_state = "Allowed";
    std::string chroma_allow_state = "Allowed";
    bool requires_mod_presence_filter = false;
    std::vector<GlobalNamespace::BeatmapCharacteristicSO *> characteristics;
    std::unordered_map<std::string, GlobalNamespace::BeatmapCharacteristicSO *> characteristic_lookup;
    std::unordered_map<std::string, PlaylistCore::BPSong *> playlist_song_lookup;
    PlaylistCore::Playlist *playlist = nullptr;
    std::size_t playlist_song_lookup_index = 0;
    std::size_t playlist_level_copy_index = 0;
    bool source_levels_ready = false;
    bool initialization_complete = false;
    bool pending_level_resolved = false;
    std::optional<LevelParams> pending_level_params = std::nullopt;
    std::vector<GlobalNamespace::BeatmapLevel *> automatic_levels;
    std::vector<PlaysetBeatmap> playset_beatmaps;
  };

  static bool is_budget_exhausted(std::chrono::steady_clock::time_point chunk_start, int64_t budget_ms) {
    if(budget_ms < 0)
      return false;
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-chunk_start).count() >= budget_ms;
  }

  static bool initialize_level_calculation(LevelCalculationContext& context, bool automatic) {
    context = { };
    context.automatic = automatic;
    if(!SongCore::API::Loading::AreSongsLoaded())
      return false;
    if(context.automatic) {
      auto difficulty_setting = getModConfig().difficulty.GetValue();
      auto characteristic_setting = getModConfig().characteristic.GetValue();
      context.noodle_allow_state = getModConfig().noodle_extensions.GetValue();
      context.chroma_allow_state = getModConfig().chroma.GetValue();
      context.requires_mod_presence_filter = context.noodle_allow_state != "Allowed" || context.chroma_allow_state != "Allowed";
      context.selected_difficulty = difficulty_setting == "Any" ? std::nullopt : std::optional(string_to_difficulty(difficulty_setting));
      context.characteristics = get_characteristics();
      context.characteristic_lookup = build_characteristic_lookup(context.characteristics);
      context.standard_characteristic = find_characteristic_from_lookup(context.characteristic_lookup, "Standard");
      if(characteristic_setting != "Any") {
        context.selected_characteristic = find_characteristic_from_lookup(context.characteristic_lookup, characteristic_setting);
        if(context.selected_characteristic == nullptr) {
          PaperLogger.warn("selected_characteristic is null.");
          return false;
        }
      }
      context.playlist = selected_playlist;
      if(context.playlist != nullptr && context.playlist->playlistCS != nullptr)
        context.automatic_levels.reserve(context.playlist->playlistCS->beatmapLevels.size());
      if(context.playlist != nullptr)
        context.playlist_song_lookup.reserve(context.playlist->playlistJSON.Songs.size());
    } else {
      if(selected_playset == -1)
        return false;
      auto playsets = getModConfig().playsets.GetValue();
      if(selected_playset < 0 || static_cast<std::size_t>(selected_playset) >= playsets.size())
        return false;
      context.playset_beatmaps = playsets[selected_playset].beatmaps;
    }
    return true;
  }

  static bool process_initialization_chunk(LevelCalculationContext& context, int64_t budget_ms) {
    if(context.initialization_complete)
      return true;
    if(!context.automatic) {
      context.initialization_complete = true;
      return true;
    }
    auto chunk_start = std::chrono::steady_clock::now();
    if(context.playlist == nullptr && !context.source_levels_ready) {
      auto all_levels = SongCore::API::Loading::GetAllLevels();
      if(context.automatic_levels.capacity() == 0)
        context.automatic_levels.reserve(all_levels.size());
      while(context.playlist_level_copy_index < all_levels.size()) {
        context.automatic_levels.push_back(all_levels[context.playlist_level_copy_index++]);
        if(is_budget_exhausted(chunk_start, budget_ms))
          return false;
      }
      context.source_levels_ready = true;
    }
    if(context.playlist != nullptr) {
      while(context.playlist_song_lookup_index < context.playlist->playlistJSON.Songs.size()) {
        auto &song = context.playlist->playlistJSON.Songs[context.playlist_song_lookup_index++];
        context.playlist_song_lookup[normalize_level_id(song.LevelID)] = &song;
        if(is_budget_exhausted(chunk_start, budget_ms))
          return false;
      }
      if(!context.source_levels_ready) {
        if(context.playlist->playlistCS == nullptr) {
          context.source_levels_ready = true;
        } else {
          while(context.playlist_level_copy_index < context.playlist->playlistCS->beatmapLevels.size()) {
            context.automatic_levels.push_back(context.playlist->playlistCS->beatmapLevels[context.playlist_level_copy_index++]);
            if(is_budget_exhausted(chunk_start, budget_ms))
              return false;
          }
          context.source_levels_ready = true;
        }
      }
    }
    context.initialization_complete = true;
    return true;
  }

  static bool process_level_calculation_chunk(LevelCalculationContext& context, std::vector<LevelParams>& out_levels, int64_t budget_ms) {
    if(!process_initialization_chunk(context, budget_ms))
      return false;
    auto chunk_start = std::chrono::steady_clock::now();
    if(context.automatic) {
      while(context.index < context.automatic_levels.size()) {
        if(is_budget_exhausted(chunk_start, budget_ms))
          return false;
        if(!context.pending_level_resolved) {
          auto level = context.automatic_levels[context.index];
          auto playlist_song = find_playlist_song_for_level(context.playlist_song_lookup, level);
          context.pending_level_params = resolve_level_params(level, playlist_song, context.selected_difficulty, context.selected_characteristic, context.standard_characteristic, context.characteristics, context.characteristic_lookup);
          context.pending_level_resolved = true;
          if(is_budget_exhausted(chunk_start, budget_ms))
            return false;
        }
        if(context.pending_level_params.has_value()) {
          if(is_budget_exhausted(chunk_start, budget_ms))
            return false;
          if(level_passes_mod_filters(context.pending_level_params.value(), context.requires_mod_presence_filter, context.noodle_allow_state, context.chroma_allow_state))
            out_levels.push_back(context.pending_level_params.value());
        }
        context.pending_level_params = std::nullopt;
        context.pending_level_resolved = false;
        context.index++;
        if(is_budget_exhausted(chunk_start, budget_ms))
          return false;
      }
      return true;
    }
    while(context.index < context.playset_beatmaps.size()) {
      if(is_budget_exhausted(chunk_start, budget_ms))
        return false;
      auto lp = LevelParams::from_playset_beatmap(context.playset_beatmaps[context.index]);
      if(lp)
        out_levels.push_back(lp.value());
      else
        PaperLogger.warn("Found invalid level in playset.");
      context.index++;
      if(is_budget_exhausted(chunk_start, budget_ms))
        return false;
    }
    return true;
  }

  static bool process_shuffle_chunk(std::vector<LevelParams>& levels, std::size_t& shuffle_index, int64_t budget_ms) {
    auto chunk_start = std::chrono::steady_clock::now();
    while(shuffle_index > 1) {
      if(is_budget_exhausted(chunk_start, budget_ms))
        return false;
      shuffle_index--;
      std::uniform_int_distribution<std::size_t> distribution(0, shuffle_index);
      std::size_t swap_index = distribution(get_random_engine());
      if(swap_index != shuffle_index)
        std::swap(levels[shuffle_index], levels[swap_index]);
      if(is_budget_exhausted(chunk_start, budget_ms))
        return false;
    }
    return true;
  }

  static void finalize_level_calculation(std::chrono::steady_clock::time_point calculation_start, bool should_shuffle) {
    if(should_shuffle && !getModConfig().sequential.GetValue())
      std::shuffle(state.levels.begin(), state.levels.end(), get_random_engine());
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-calculation_start).count();
    PaperLogger.info("Calculated {} levels in {} ms.", state.levels.size(), elapsed_ms);
  }

  static bool menu_start_calculation_in_progress = false;

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
    auto calculation_start = std::chrono::steady_clock::now();
    state.levels = {};
    LevelCalculationContext context;
    if(!initialize_level_calculation(context, automatic))
      return;
    process_level_calculation_chunk(context, state.levels, -1);
    finalize_level_calculation(calculation_start, true);
  }

  static custom_types::Helpers::Coroutine start_endless_from_menu_coroutine(bool automatic) {
    co_yield nullptr;
    PaperLogger.info("Calculating levels...");
    auto calculation_start = std::chrono::steady_clock::now();
    state.levels = {};
    LevelCalculationContext context;
    if(!initialize_level_calculation(context, automatic)) {
      menu_start_calculation_in_progress = false;
      co_return;
    }
    while(!process_level_calculation_chunk(context, state.levels, 4))
      co_yield nullptr;
    if(!getModConfig().sequential.GetValue()) {
      std::size_t shuffle_index = state.levels.size();
      while(!process_shuffle_chunk(state.levels, shuffle_index, 4))
        co_yield nullptr;
    }
    finalize_level_calculation(calculation_start, false);
    menu_start_calculation_in_progress = false;
    start_endless();
  }

  void start_endless_from_menu(bool automatic) {
    if(menu_start_calculation_in_progress) {
      PaperLogger.warn("Start ignored because calculation is already in progress.");
      return;
    }
    auto mth = UnityEngine::Object::FindObjectOfType<GlobalNamespace::MenuTransitionsHelper *>();
    RETURN_IF_NULL(mth,);
    menu_start_calculation_in_progress = true;
    mth->StartCoroutine(custom_types::Helpers::CoroutineHelper::New(start_endless_from_menu_coroutine(automatic)));
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
        std::shuffle(state.levels.begin(), state.levels.end(), get_random_engine());
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
