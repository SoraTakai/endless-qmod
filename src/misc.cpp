#include <algorithm>

#include "UnityEngine/Object.hpp"

#include "GlobalNamespace/PlayerDataModel.hpp"
#include "GlobalNamespace/PlayerDataFileModel.hpp"
#include "GlobalNamespace/BeatmapCharacteristicCollection.hpp"
#include "GlobalNamespace/MenuTransitionsHelper.hpp"
#include "GlobalNamespace/SinglePlayerLevelSelectionFlowCoordinator.hpp"
#include "GlobalNamespace/BeatmapKey.hpp"
#include "GlobalNamespace/StandardLevelScenesTransitionSetupDataSO.hpp"
#include "GlobalNamespace/LevelCompletionResults.hpp"
#include "GlobalNamespace/RecordingToolManager.hpp"
#include "GlobalNamespace/GameplaySetupViewController.hpp"
#include "GlobalNamespace/ColorSchemesSettings.hpp"
#include "GlobalNamespace/BeatmapLevelsModel.hpp"
#include "GlobalNamespace/MainFlowCoordinator.hpp"
#include "GlobalNamespace/ResultsViewController.hpp"
#include "UnityEngine/Resources.hpp"
#include "GlobalNamespace/LevelBar.hpp"
#include "TMPro/TextMeshProUGUI.hpp"


#include "System/Action_2.hpp"
#include "System/Nullable_1.hpp"
#include "System/Collections/Generic/IReadOnlyList_1.hpp"
#include "System/Collections/Generic/IReadOnlyCollection_1.hpp"

#include "custom-types/shared/delegate.hpp"

#include "songcore/shared/SongCore.hpp"

#include "misc.hpp"
#include "main.hpp"


namespace endless {
	std::optional<LevelParams> LevelParams::from_playset_beatmap(PlaysetBeatmap psb) {
		auto level = get_beatmap(psb.id);
		if(level == nullptr) {
			PaperLogger.warn("Level with id '{}' not found.", psb.id);
			return std::nullopt;
		}
		auto characteristic = get_characteristic(psb.characteristic);
		if(characteristic == nullptr) {
			PaperLogger.warn("Characteristic '{}' not found.", psb.characteristic);
			return std::nullopt;
		}
		return LevelParams(level, characteristic, string_to_difficulty(psb.difficulty));
	}
	std::optional<LevelParams> LevelParams::from_beatmap_key(GlobalNamespace::BeatmapKey key) {
		auto level = get_beatmap(key.levelId);
		if(level == nullptr) {
			PaperLogger.warn("Level with id '{}' not found.", key.levelId);
			return std::nullopt;
		}
		return LevelParams(level, key.beatmapCharacteristic, key.difficulty);
	}
	GlobalNamespace::BeatmapDifficulty string_to_difficulty(std::string string) {
		// don't know why C++ doesn't have string switches
		if(string == "Easy")
			return GlobalNamespace::BeatmapDifficulty::Easy;
		if(string == "Normal")
			return GlobalNamespace::BeatmapDifficulty::Normal;
		if(string == "Hard")
			return GlobalNamespace::BeatmapDifficulty::Hard;
		if(string == "Expert")
			return GlobalNamespace::BeatmapDifficulty::Expert;
		if(string == "Expert+")
			return GlobalNamespace::BeatmapDifficulty::ExpertPlus;
		PaperLogger.warn("string_to_difficulty() called on invalid difficulty '{}', defaulting to ExpertPlus.", string);
		return GlobalNamespace::BeatmapDifficulty::ExpertPlus;
	}
	std::string difficulty_to_string(GlobalNamespace::BeatmapDifficulty dif) {
		if(dif == GlobalNamespace::BeatmapDifficulty::Easy)
			return "Easy";
		if(dif == GlobalNamespace::BeatmapDifficulty::Normal)
			return "Normal";
		if(dif == GlobalNamespace::BeatmapDifficulty::Hard)
			return "Hard";
		if(dif == GlobalNamespace::BeatmapDifficulty::Expert)
			return "Expert";
		if(dif == GlobalNamespace::BeatmapDifficulty::ExpertPlus)
			return "Expert+";
		PaperLogger.warn("difficulty_to_string() called on invalid difficulty, with value {}.", static_cast<int32_t>(dif));
		return "Unknown";
	}
	GlobalNamespace::BeatmapCharacteristicSO *get_characteristic(std::string name) {
		auto obj = UnityEngine::Object::FindObjectOfType<GlobalNamespace::PlayerDataModel *>();
		RETURN_IF_NULL(obj, nullptr);
		return obj->_playerDataFileModel->_beatmapCharacteristicCollection->GetBeatmapCharacteristicBySerializedName(name);
	}	
	std::vector<GlobalNamespace::BeatmapCharacteristicSO *>get_characteristics(void) {
		auto obj = UnityEngine::Object::FindObjectOfType<GlobalNamespace::PlayerDataModel *>();
		RETURN_IF_NULL(obj, {});
		auto list = obj->_playerDataFileModel->_beatmapCharacteristicCollection->beatmapCharacteristics;
		int size = reinterpret_cast<System::Collections::Generic::IReadOnlyCollection_1<UnityW<GlobalNamespace::BeatmapCharacteristicSO>> *>(list)->Count;
		std::vector<GlobalNamespace::BeatmapCharacteristicSO *> ret;
		for(int i = 0; i < size; i++)
			ret.push_back(list->get_Item(i));
		return ret;
	}
	bool start_level(LevelParams params) {
		PaperLogger.info("Starting level '{}'...", params.level->songName);
		auto mth = UnityEngine::Object::FindObjectOfType<GlobalNamespace::MenuTransitionsHelper *>();
		RETURN_IF_NULL(mth, false);
		auto splsfc = UnityEngine::Object::FindObjectOfType<GlobalNamespace::SinglePlayerLevelSelectionFlowCoordinator *>();
		RETURN_IF_NULL(splsfc, false);
		auto beatmapKey = GlobalNamespace::BeatmapKey(params.characteristic, params.difficulty, params.level->levelID);
		mth->StartStandardLevel(
			"Endless",
			beatmapKey,
			params.level,
			splsfc->_gameplaySetupViewController->environmentOverrideSettings,
			splsfc->_gameplaySetupViewController->colorSchemesSettings->GetOverrideColorScheme(),
			params.level->GetColorScheme(params.characteristic, params.difficulty),
			splsfc->gameplayModifiers,
			splsfc->playerSettings,
			nullptr,
			splsfc->_environmentsListModel,
			"Exit Endless",
			false,
			false,
			nullptr,
			nullptr,
			// ::System::Action_2< ::UnityW< ::GlobalNamespace::StandardLevelScenesTransitionSetupDataSO>, ::GlobalNamespace::LevelCompletionResults *> *
			custom_types::MakeDelegate<
				System::Action_2<UnityW<GlobalNamespace::StandardLevelScenesTransitionSetupDataSO>, GlobalNamespace::LevelCompletionResults *>*
			>(
				std::function([splsfc](UnityW<GlobalNamespace::StandardLevelScenesTransitionSetupDataSO> slstss, GlobalNamespace::LevelCompletionResults *lcr) -> void {
					splsfc->HandleStandardLevelDidFinish(slstss, lcr);
				})
			),
			nullptr,
			System::Nullable_1<GlobalNamespace::RecordingToolManager::SetupData>(false, {})
		);
		PaperLogger.info("Level started.");
		return true;
	}
    const std::vector<std::string> incompatible_mods = {"Replay"};
    std::vector<std::string> enabled_incompatible_mods = {};
    void check_for_incompatible_mods(void) {
		enabled_incompatible_mods.clear();
		for(auto &mod_result : modloader::get_all()) {
			if(auto mod_info = std::get_if<modloader::ModData>(&mod_result)) {
				auto id = mod_info->info.id;
				for(std::string iid : incompatible_mods) {
					if(id == iid) {
						enabled_incompatible_mods.push_back(id);
						PaperLogger.warn("Incompatible mod {} found.", id);
						goto continue_outer; // no labelled continue in C++
					}
				}
			}
			continue_outer:;
		}
    }
    static GlobalNamespace::BeatmapLevelsModel *get_beatmap_levels_model(void) {
      auto mfcs = UnityEngine::Resources::FindObjectsOfTypeAll<GlobalNamespace::MainFlowCoordinator *>();
      if(mfcs.size() == 0)
        return nullptr;
      return mfcs[0]->_beatmapLevelsModel;
    }
    GlobalNamespace::BeatmapLevel *get_beatmap(std::string id) {
      // try builtin levels
      auto bmlm = get_beatmap_levels_model();
      auto level = bmlm == nullptr ? nullptr : bmlm->GetBeatmapLevel(id);
      if(level != nullptr)
        return level;
      // try songcore
      level = static_cast<GlobalNamespace::BeatmapLevel *>(SongCore::API::Loading::GetLevelByLevelID(id));
      if(level != nullptr)
        return level;
      return nullptr;
    }
	GlobalNamespace::LevelBar *create_level_bar(UnityEngine::Transform *parent, LevelParams params) {
		auto level_bar = UnityEngine::Object::Instantiate(UnityEngine::Resources::FindObjectsOfTypeAll<GlobalNamespace::ResultsViewController *>()[0]->_levelBar, parent);
		level_bar->hide = false;
		level_bar->_showDifficultyAndCharacteristic = true;
		level_bar->Setup(params.level, params.difficulty, params.characteristic);
		return level_bar;
	}
}
