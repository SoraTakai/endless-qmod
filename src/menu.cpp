#include "TMPro/FontStyles.hpp"

#include "beatsaber-hook/shared/utils/hooking.hpp"

#include "bsml/shared/BSML.hpp"
#include "UnityEngine/UI/VerticalLayoutGroup.hpp"
#include "UnityEngine/UI/Image.hpp"
#include "UnityEngine/Transform.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/Object.hpp"
#include "UnityEngine/Resources.hpp"
#include "GlobalNamespace/LevelBar.hpp"
#include "GlobalNamespace/LevelCollectionNavigationController.hpp"
#include "GlobalNamespace/LevelListTableCell.hpp"

#include "modconfig.hpp"
#include "menu.hpp"
#include "main.hpp"
#include "endless.hpp"
#include "misc.hpp"

static std::optional<endless::PlaysetBeatmap> selected_level = std::nullopt;

static void update_selected_level(GlobalNamespace::LevelCollectionNavigationController *self) {
	auto key = self->beatmapKey;
	if(!key.IsValid()) {
		selected_level = std::nullopt;
		return;
	}
	auto pbm = endless::PlaysetBeatmap();
	pbm.id = static_cast<std::string>(key.levelId);
	pbm.difficulty = endless::difficulty_to_string(key.difficulty);
	pbm.characteristic = static_cast<std::string>(key.beatmapCharacteristic->_serializedName);
	selected_level = pbm;
}

MAKE_HOOK_MATCH(
	LevelCollectionNavigationController_HandleLevelCollectionViewControllerDidSelectLevel, // what a fucking name 
	&GlobalNamespace::LevelCollectionNavigationController::HandleLevelCollectionViewControllerDidSelectLevel, 
	void, 
	GlobalNamespace::LevelCollectionNavigationController *self,
	GlobalNamespace::LevelCollectionViewController *viewController,
	GlobalNamespace::BeatmapLevel *level
) {
	LevelCollectionNavigationController_HandleLevelCollectionViewControllerDidSelectLevel(self, viewController, level);
	update_selected_level(self);
}

MAKE_HOOK_MATCH(
	LevelCollectionNavigationController_HandleLevelDetailViewControllerDidChangeDifficultyBeatmap, // holy shit an even longer name. why would you name a method like this
	&GlobalNamespace::LevelCollectionNavigationController::HandleLevelDetailViewControllerDidChangeDifficultyBeatmap, 
	void, 
	GlobalNamespace::LevelCollectionNavigationController *self,
	GlobalNamespace::StandardLevelDetailViewController *viewController
) {
	LevelCollectionNavigationController_HandleLevelDetailViewControllerDidChangeDifficultyBeatmap(self, viewController);
	update_selected_level(self);
}

namespace endless {
	PlaylistCore::Playlist *selected_playlist = nullptr;
	int selected_playset = -1;

  static bool set_selected_playlist_by_name(std::string const& playlist_name, bool save, bool normalize_missing) {
    selected_playlist = nullptr;
    if(playlist_name == "All") {
      if(save)
        getModConfig().selected_playlist.SetValue("All");
      return true;
    }
    for(PlaylistCore::Playlist *playlist : PlaylistCore::GetLoadedPlaylists()) {
      if(playlist->name != playlist_name)
        continue;
      selected_playlist = playlist;
      if(save)
        getModConfig().selected_playlist.SetValue(playlist_name);
      return true;
    }
    if(save)
      getModConfig().selected_playlist.SetValue(normalize_missing && PlaylistCore::hasLoaded ? "All" : playlist_name);
    return false;
  }

	void register_menu_hooks(void) {
		INSTALL_HOOK(PaperLogger, LevelCollectionNavigationController_HandleLevelCollectionViewControllerDidSelectLevel);
		INSTALL_HOOK(PaperLogger, LevelCollectionNavigationController_HandleLevelDetailViewControllerDidChangeDifficultyBeatmap);
	}
	
	// add an object's parent to a tab
	template<class T>
	static T tab_add_parent(std::shared_ptr<std::vector<UnityW<UnityEngine::GameObject>>> tab, T object) {
		tab->push_back(object->transform->parent->gameObject);
		return object;
	}
	
	// add an object to a tab
	template<class T>
	static T tab_add(std::shared_ptr<std::vector<UnityW<UnityEngine::GameObject>>> tab, T object) {
		tab->push_back(object->gameObject);
		return object;
	}
	
	// sets a tab's visibility
	static void tab_set_visible(std::shared_ptr<std::vector<UnityW<UnityEngine::GameObject>>> tab, bool visible) {
		for(auto go : *tab)
			go->active = visible;
	}

	void did_activate(UnityEngine::GameObject *self, bool firstActivation) {
		if(!firstActivation)
			return;

		// this entire function is extremely jank and I hate all of it. Please, do not copy this code it's so bad.
		// this is my punishment for using BSML::Lite and not learning how to properly use BSML I guess

		auto container = BSML::Lite::CreateScrollableSettingsContainer(self->transform);
		// add incompatible mods warning if applicable
		if(enabled_incompatible_mods.size() > 0) {
			std::stringstream stream;
			stream << "The following incompatible mods are enabled:\n";
			for(std::size_t i = 0; i < enabled_incompatible_mods.size(); i++) {
				if(i != 0)
					stream << ", ";
				stream << enabled_incompatible_mods[i];
			}
			stream << "\nThese mods are known to cause crashes or other problems with Endless.";
			stream << "\nIt is HIGHLY recommended to disable them before continuing.";
			BSML::Lite::CreateText(container->transform, stream.str(), TMPro::FontStyles::Underline, {0, 0}, {0, 25});
		}
		// sliders for NPS, currently disabled because implementing them would be more of a pain than I thought it'd be
		// BSML::Lite::CreateSliderSetting(container->transform, "Minimum NPS", 0.1, getModConfig().min_nps.GetValue(), 0.0, 20.0, [](float val) {
		// 	getModConfig().min_nps.SetValue(val);
		// });
		// BSML::Lite::CreateSliderSetting(container->transform, "Maximum NPS", 0.1, getModConfig().max_nps.GetValue(), 0.0, 20.0, [](float val) {
		// 	getModConfig().max_nps.SetValue(val);
		// });
		BSML::Lite::CreateToggle(container->transform, "Continue on Fail", getModConfig().continue_on_fail.GetValue(), [](bool value) {
			getModConfig().continue_on_fail.SetValue(value);
		});
		BSML::Lite::CreateToggle(container->transform, "Endless HUD Enabled", getModConfig().hud_enabled.GetValue(), [](bool value) {
			getModConfig().hud_enabled.SetValue(value);
		});	
		auto sequential_toggle = BSML::Lite::CreateToggle(container->transform, "Sequential", getModConfig().sequential.GetValue(), [](bool value) {
			getModConfig().sequential.SetValue(value);
		});	
		BSML::Lite::AddHoverHint(sequential_toggle, "Play maps one after another. Most useful in playset mode.");
		BSML::Lite::CreateToggle(container->transform, "End After All Songs Finish", getModConfig().end_after_all.GetValue(), [](bool value) {
			getModConfig().end_after_all.SetValue(value);
		});	
		
		auto automatic_tab = std::make_shared<std::vector<UnityW<UnityEngine::GameObject>>>();
		auto playset_tab = std::make_shared<std::vector<UnityW<UnityEngine::GameObject>>>();
		auto playset_tab_extra = std::make_shared<std::vector<UnityW<UnityEngine::GameObject>>>(); // things that only get displayed when an actual playset is selected
		auto level_bars = std::make_shared<std::vector<UnityW<UnityEngine::GameObject>>>();
		
		std::vector<std::string_view> tabs = {"Automatic", "Playset"};
		std::span<std::string_view> tab_span { tabs }; // don't know why doing this is necessary /here/ but nowhere else.
		auto tsc = BSML::Lite::CreateTextSegmentedControl(container->transform, tab_span, [=](int idx){
			tab_set_visible(automatic_tab, idx == 0);
			tab_set_visible(playset_tab, idx == 1);
			tab_set_visible(playset_tab_extra, idx == 1 && selected_playset != -1);
			tab_set_visible(level_bars, idx == 1);
		});
		// automatic
		{
      // playlist
      std::string selected_playlist_name = getModConfig().selected_playlist.GetValue();
      set_selected_playlist_by_name(selected_playlist_name, false, false);
      PaperLogger.info("Restoring playlist setting '{}'...", selected_playlist_name);
      auto playlist_dropdown_ready = std::make_shared<bool>(false);
      std::vector<std::string_view> playlist_names = {"All"};
      auto playlist_dropdown = tab_add_parent(automatic_tab, BSML::Lite::CreateDropdown(container->transform, "Playlist", "All", playlist_names, [playlist_dropdown_ready](StringW string) {
        std::string playlist_name = static_cast<std::string>(string);
        if(*playlist_dropdown_ready)
          set_selected_playlist_by_name(playlist_name, true, false);
        else
          set_selected_playlist_by_name(playlist_name, false, false);
      }));
      int selected_playlist_index = 0;
      std::vector<std::string> loaded_playlist_names = {"All"};
      for(PlaylistCore::Playlist *playlist : PlaylistCore::GetLoadedPlaylists()) {
        loaded_playlist_names.push_back(playlist->name);
        if(playlist->name == selected_playlist_name)
          selected_playlist_index = loaded_playlist_names.size()-1;
      }
      if(selected_playlist_name != "All" && selected_playlist_index == 0) {
        loaded_playlist_names.push_back(selected_playlist_name);
        selected_playlist_index = loaded_playlist_names.size()-1;
      }
      auto loaded_playlist_names_shared = std::make_shared<std::vector<std::string>>(loaded_playlist_names);
      auto playlist_list = ListW<System::Object *>::New();
      playlist_list->EnsureCapacity(loaded_playlist_names.size());
      for(auto const& playlist_name : loaded_playlist_names)
        playlist_list->Add(static_cast<System::Object *>(StringW(playlist_name).convert()));
      playlist_dropdown->values = playlist_list;
      playlist_dropdown->UpdateChoices();
      playlist_dropdown->dropdown->SelectCellWithIdx(selected_playlist_index);
      *playlist_dropdown_ready = true;
			// this could break if playlists are updated while in-game. FIXME

			// difficulty
      std::vector<std::string_view> difficulties{"Any", "Easy", "Normal", "Hard", "Expert", "Expert+"};
			tab_add_parent(automatic_tab, BSML::Lite::CreateDropdown(container->transform, "Difficulty", getModConfig().difficulty.GetValue(), difficulties, [](StringW string) {
				getModConfig().difficulty.SetValue(string);
			}));
			// characteristic
			// two vectors are necessary so that we can have a vector of string_views and not have them get clobbered over in memory.
			std::vector<std::string> characteristics;
			for(auto characteristic : get_characteristics()) {
				if(characteristic->_serializedName == "MissingCharacteristic")
					continue; // don't want that one
				characteristics.push_back(characteristic->_serializedName);
			}
      std::vector<std::string_view> characteristics_views{"Any"};
      characteristics_views.insert(characteristics_views.end(), characteristics.begin(), characteristics.end());
			tab_add_parent(automatic_tab, BSML::Lite::CreateDropdown(container->transform, "Characteristic", getModConfig().characteristic.GetValue(), characteristics_views, [](StringW string) {
				getModConfig().characteristic.SetValue(string);
			}));

			// mods
			std::vector<std::string_view> allow_state{"Allowed", "Required", "Forbidden"};
			tab_add_parent(automatic_tab, BSML::Lite::CreateDropdown(container->transform, "Noodle Extensions", getModConfig().noodle_extensions.GetValue(), allow_state, [](StringW string) {
				getModConfig().noodle_extensions.SetValue(string);
			}));
			tab_add_parent(automatic_tab, BSML::Lite::CreateDropdown(container->transform, "Chroma", getModConfig().chroma.GetValue(), allow_state, [](StringW string) {
				getModConfig().chroma.SetValue(string);
			}));

      // start button
      tab_add(automatic_tab, BSML::Lite::CreateUIButton(container->transform, "Start!", [playlist_dropdown, loaded_playlist_names_shared]() {
        int selected_index = playlist_dropdown->dropdown->get_selectedIndex();
        if(selected_index < 0 || selected_index >= loaded_playlist_names_shared->size())
          selected_index = 0;
        std::string playlist_name = (*loaded_playlist_names_shared)[selected_index];
        PaperLogger.info("Saving playlist setting '{}' before start.", playlist_name);
        set_selected_playlist_by_name(playlist_name, true, true);
				calculate_levels(true);
				start_endless();
			}));
		}
		// playset
		{
			auto add_level_bar = [=](LevelParams params) {
				auto level_bar = tab_add(level_bars, create_level_bar(container->transform, params));
				tab_add(level_bars, BSML::Lite::CreateUIButton(container->transform, "Remove", [=]() {
					auto playsets = getModConfig().playsets.GetValue();
					auto beatmaps = &playsets[selected_playset].beatmaps; // beatmaps in the currently selected playset
					// find index of the level bar in level_bars
					int level_bars_index = -1;
					for(int i = 0; i < level_bars->size(); i++) {
						if((*level_bars)[i] == level_bar->gameObject) {
							level_bars_index = i;
							break;
						}
					}
					if(level_bars_index == -1) {
						PaperLogger.warn("Level bar not found!");
						return;
					}
					// find index of the beatmap in beatmaps
					int beatmap_index = -1;
					for(int i = 0; i < beatmaps->size(); i++) {
						auto pbm = (*beatmaps)[i];
						if(pbm.id == params.level->levelID && pbm.difficulty == difficulty_to_string(params.difficulty) && pbm.characteristic == params.characteristic->_serializedName) {
							beatmap_index = i;
						}
					}
					if(beatmap_index == -1) {
						PaperLogger.warn("Playset beatmap not found!");
						return;
					}
					// delete ui
					UnityEngine::Object::Destroy((*level_bars)[level_bars_index]);
					UnityEngine::Object::Destroy((*level_bars)[level_bars_index+1]); // sneaky way to delete this button
					// remove beatmap from playset
					level_bars->erase(level_bars->begin() + level_bars_index, level_bars->begin() + level_bars_index + 2);
					beatmaps->erase(beatmaps->begin() + beatmap_index);
					getModConfig().playsets.SetValue(playsets);
				}));
			};
			std::vector<std::string_view> playset_names = {"<None>"};
      auto find_playset_index = [](std::vector<Playset> const& playsets, std::string const& playset_name) {
        for(int i = 0; i < playsets.size(); i++) {
          if(playsets[i].name == playset_name)
            return i;
        }
        return -1;
      };
      auto suppress_playset_dropdown_callback = std::make_shared<bool>(false);
      auto apply_playset_selection = [=](std::string const& playset_name, bool save) {
        auto playsets = getModConfig().playsets.GetValue();
        selected_playset = find_playset_index(playsets, playset_name);
        for(auto go : *level_bars)
          UnityEngine::Object::Destroy(go);
        level_bars->clear();
        tab_set_visible(playset_tab_extra, selected_playset != -1);
        if(selected_playset == -1) {
          if(save)
            getModConfig().selected_playset.SetValue("<None>");
          return;
        }
        if(save)
          getModConfig().selected_playset.SetValue(playsets[selected_playset].name);
        for(auto psb : playsets[selected_playset].beatmaps) {
          auto params = LevelParams::from_playset_beatmap(psb);
          if(!params.has_value()) {
            continue;
          }
          add_level_bar(params.value());
        }
      };
      std::string selected_playset_name = getModConfig().selected_playset.GetValue();
      auto playsets = getModConfig().playsets.GetValue();
      selected_playset = find_playset_index(playsets, selected_playset_name);
      if(selected_playset == -1) {
        selected_playset_name = "<None>";
        getModConfig().selected_playset.SetValue(selected_playset_name);
      } else {
        selected_playset_name = playsets[selected_playset].name;
      }
      auto playset_dropdown = tab_add_parent(playset_tab, BSML::Lite::CreateDropdown(container->transform, "Playset", "<None>", playset_names, [apply_playset_selection, suppress_playset_dropdown_callback](StringW string) {
        if(*suppress_playset_dropdown_callback)
          return;
        apply_playset_selection(static_cast<std::string>(string), true);
      }));
			#define UPDATE_PLAYSET_DROPDOWN() do { \
				std::vector<std::string> _playsets = {"<None>"}; \
				for(Playset playset : getModConfig().playsets.GetValue()) { \
					_playsets.push_back(playset.name); \
				} \
				auto list = ListW<System::Object *>::New(); \
				list->EnsureCapacity(_playsets.size()); \
				for(auto name : _playsets) { \
					list->Add(static_cast<System::Object *>(StringW(name).convert())); \
				} \
				playset_dropdown->values = list; \
				playset_dropdown->UpdateChoices(); \
      } while(0)
      UPDATE_PLAYSET_DROPDOWN();
      *suppress_playset_dropdown_callback = true;
      playset_dropdown->dropdown->SelectCellWithIdx(selected_playset == -1 ? 0 : selected_playset+1);
      *suppress_playset_dropdown_callback = false;
      apply_playset_selection(selected_playset_name, false);
      auto new_playset_name = tab_add(playset_tab, BSML::Lite::CreateStringSetting(container->transform, "New Playset Name", ""));
			auto hgroup = tab_add(playset_tab, BSML::Lite::CreateHorizontalLayoutGroup(container->transform));
			BSML::Lite::CreateUIButton(hgroup->transform, "Create New Playset", [=]() {
				std::string name = new_playset_name->text;
				if(name == "")
					return;
				// make sure name is not in use
				for(Playset playset : getModConfig().playsets.GetValue()) {
					if(playset.name == name)
						return;
				}
				// add playset
				auto playsets = getModConfig().playsets.GetValue();
				Playset playset;
				playset.name = name;
          playsets.push_back(playset);
          getModConfig().playsets.SetValue(playsets);
          UPDATE_PLAYSET_DROPDOWN();
          *suppress_playset_dropdown_callback = true;
          playset_dropdown->dropdown->SelectCellWithIdx(playsets.size());
          *suppress_playset_dropdown_callback = false;
        apply_playset_selection(name, true);
        });
			tab_add(playset_tab_extra, BSML::Lite::CreateUIButton(hgroup->transform, "Delete Playset", [=]() {
				if(selected_playset == -1)
					return;
				auto playsets = getModConfig().playsets.GetValue();
          playsets.erase(playsets.begin() + selected_playset); // nope, can't just have a remove_at() function, need to do this bullshit. istg C++ is like if they _tried_ to make the stupidest fucking language ever.
          getModConfig().playsets.SetValue(playsets);
          UPDATE_PLAYSET_DROPDOWN();
          *suppress_playset_dropdown_callback = true;
          playset_dropdown->dropdown->SelectCellWithIdx(0);
          *suppress_playset_dropdown_callback = false;
        apply_playset_selection("<None>", true);
        }));
			// start button
			tab_add(playset_tab_extra, BSML::Lite::CreateUIButton(hgroup->transform, "Start!", []() {
				calculate_levels(false);
				start_endless();
			}));	
			std::string playset_text = "--- Levels in this playset ---\nTo add levels, first select the level you want to add (as if you were\nabout to play it). Then, press the button below to add it to the playset.";
			tab_add(playset_tab_extra, BSML::Lite::CreateText(container->transform, playset_text, TMPro::FontStyles::Normal, {0, 0}, {0, 16}));
			tab_add(playset_tab_extra, BSML::Lite::CreateUIButton(container->transform, "Add Level to Playset", [=]() {
				if(selected_playset != -1 && selected_level) {
					auto level = selected_level.value();
					auto playsets = getModConfig().playsets.GetValue();
					auto beatmaps = &playsets[selected_playset].beatmaps;
					// make sure that the selected_level is not already in beatmaps
					for(auto pbm : *beatmaps)
						if(pbm == level)
							return;
					// add level to beatmaps
					beatmaps->push_back(level);
					getModConfig().playsets.SetValue(playsets);
					auto params = LevelParams::from_playset_beatmap(selected_level.value());
					if(!params.has_value())
						return;
					add_level_bar(params.value());
				}
			}));
			#undef UPDATE_PLAYSET_DROPDOWN
			
		}
		tab_set_visible(playset_tab, false);
		tab_set_visible(playset_tab_extra, false);
    tab_set_visible(level_bars, false);
	}
}
