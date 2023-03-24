//  SPDX-License-Identifier: MIT
//
//  EmulationStation Desktop Edition
//  GuiMenu.cpp
//
//  Main menu.
//  Some submenus are covered in separate source files.
//

#include "guis/GuiMenu.h"

#if defined(_WIN64)
// Why this is needed here is anyone's guess but without it the compilation fails.
#include <winsock2.h>
#endif

#include "CollectionSystemsManager.h"
#include "EmulationStation.h"
#include "FileFilterIndex.h"
#include "FileSorts.h"
#include "Scripting.h"
#include "SystemData.h"
#include "UIModeController.h"
#include "VolumeControl.h"
#include "components/OptionListComponent.h"
#include "components/SliderComponent.h"
#include "components/SwitchComponent.h"
#include "guis/GuiAlternativeEmulators.h"
#include "guis/GuiCollectionSystemsOptions.h"
#include "guis/GuiDetectDevice.h"
#include "guis/GuiMediaViewerOptions.h"
#include "guis/GuiMsgBox.h"
#include "guis/GuiScraperMenu.h"
#include "guis/GuiScreensaverOptions.h"
#include "guis/GuiTextEditKeyboardPopup.h"
#include "guis/GuiTextEditPopup.h"
#include "guis/GuiThemeDownloader.h"
#include "utils/PlatformUtil.h"

#include <SDL2/SDL_events.h>
#include <algorithm>

GuiMenu::GuiMenu()
    : mRenderer {Renderer::getInstance()}
    , mMenu {"主菜单"}
{
    bool isFullUI {UIModeController::getInstance()->isUIModeFull()};

    if (isFullUI)
        addEntry("抓取", 0x777777FF, true, [this] { openScraperOptions(); });

    if (isFullUI)
        addEntry("UI设置", 0x777777FF, true, [this] { openUIOptions(); });

    addEntry("声音设置", 0x777777FF, true, [this] { openSoundOptions(); });

    if (isFullUI)
        addEntry("输入设备设置", 0x777777FF, true, [this] { openInputDeviceOptions(); });

    if (isFullUI)
        addEntry("游戏专辑设置", 0x777777FF, true, [this] { openCollectionSystemOptions(); });

    if (isFullUI)
        addEntry("其他设置", 0x777777FF, true, [this] { openOtherOptions(); });

    if (!Settings::getInstance()->getBool("ForceKiosk") &&
        Settings::getInstance()->getString("UIMode") != "kiosk") {
#if defined(__APPLE__)
        addEntry("QUIT EMULATIONSTATION", 0x777777FF, false, [this] { openQuitMenu(); });
#else
        if (Settings::getInstance()->getBool("ShowQuitMenu"))
            addEntry("退出", 0x777777FF, true, [this] { openQuitMenu(); });
        else
            addEntry("退出EmulationStation", 0x777777FF, false, [this] { openQuitMenu(); });
#endif
    }

    addChild(&mMenu);
    addVersionInfo();
    setSize(mMenu.getSize());
    setPosition((mRenderer->getScreenWidth() - mSize.x) / 2.0f,
                std::round(mRenderer->getScreenHeight() * 0.13f));
}

GuiMenu::~GuiMenu()
{
    // This is required for the situation where scrolling started just before the menu
    // was openened. Without this, the scrolling would run until manually stopped after
    // the menu has been closed.
    ViewController::getInstance()->stopScrolling();

    ViewController::getInstance()->startViewVideos();
}

void GuiMenu::openScraperOptions()
{
    // Open the scraper menu.
    mWindow->pushGui(new GuiScraperMenu("抓取"));
}

void GuiMenu::openUIOptions()
{
    auto s = new GuiSettings("UI设置");

    // Theme options section.

    std::map<std::string, ThemeData::ThemeSet, ThemeData::StringComparator> themeSets {
        ThemeData::getThemeSets()};
    std::map<std::string, ThemeData::ThemeSet, ThemeData::StringComparator>::const_iterator
        selectedSet;

    auto themeSet =
        std::make_shared<OptionListComponent<std::string>>(getHelpStyle(), "主题集", false);

    // TODO: Enable and possibly move somewhere else when the theme downloader is implemented.
    //    ComponentListRow themeDownloaderInputRow;
    //    themeDownloaderInputRow.elements.clear();
    //    themeDownloaderInputRow.addElement(std::make_shared<TextComponent>("THEME DOWNLOADER",
    //                                                                  Font::get(FONT_SIZE_MEDIUM),
    //                                                                  0x777777FF),
    //                                       true);
    //    themeDownloaderInputRow.addElement(makeArrow(), false);
    //    themeDownloaderInputRow.makeAcceptInputHandler(
    //        std::bind(&GuiMenu::openThemeDownloader, this, s));
    //    s->addRow(themeDownloaderInputRow);

    // Theme set.
    if (!themeSets.empty()) {
        selectedSet = themeSets.find(Settings::getInstance()->getString("ThemeSet"));
        if (selectedSet == themeSets.cend())
            selectedSet = themeSets.cbegin();
        std::vector<std::pair<std::string, std::pair<std::string, ThemeData::ThemeSet>>>
            themeSetsSorted;
        std::string sortName;
        for (auto& theme : themeSets) {
            if (theme.second.capabilities.themeName != "")
                sortName = theme.second.capabilities.themeName;
            else
                sortName = theme.first;
            themeSetsSorted.emplace_back(std::make_pair(Utils::String::toUpper(sortName),
                                                        std::make_pair(theme.first, theme.second)));
        }
        std::sort(themeSetsSorted.begin(), themeSetsSorted.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        for (auto it = themeSetsSorted.cbegin(); it != themeSetsSorted.cend(); ++it) {
            // If required, abbreviate the theme set name so it doesn't overlap the setting name.
            const float maxNameLength {mSize.x * 0.62f};
            std::string themeName {(*it).first};
            if ((*it).second.second.capabilities.legacyTheme)
                themeName.append(" [LEGACY]");
            themeSet->add(themeName, it->second.first, (*it).second.first == selectedSet->first,
                          maxNameLength);
        }
        s->addWithLabel("主题集", themeSet);
        s->addSaveFunc([this, themeSet, s] {
            if (themeSet->getSelected() != Settings::getInstance()->getString("ThemeSet")) {
                Scripting::fireEvent("theme-changed", themeSet->getSelected(),
                                     Settings::getInstance()->getString("ThemeSet"));
                Settings::getInstance()->setString("ThemeSet", themeSet->getSelected());
                mWindow->setChangedThemeSet();
                // This is required so that the custom collection system does not disappear
                // if the user is editing a custom collection when switching theme sets.
                if (CollectionSystemsManager::getInstance()->isEditing())
                    CollectionSystemsManager::getInstance()->exitEditMode();
                s->setNeedsSaving();
                s->setNeedsReloading();
                s->setNeedsGoToStart();
                s->setNeedsCollectionsUpdate();
                s->setInvalidateCachedBackground();
            }
        });
    }

    // Theme variants.
    auto themeVariant =
        std::make_shared<OptionListComponent<std::string>>(getHelpStyle(), "主题变种", false);
    s->addWithLabel("主题变种", themeVariant);
    s->addSaveFunc([themeVariant, s] {
        if (themeVariant->getSelected() != Settings::getInstance()->getString("ThemeVariant")) {
            Settings::getInstance()->setString("ThemeVariant", themeVariant->getSelected());
            s->setNeedsSaving();
            s->setNeedsReloading();
            s->setInvalidateCachedBackground();
        }
    });

    auto themeVariantsFunc = [=](const std::string& selectedTheme,
                                 const std::string& selectedVariant) {
        std::map<std::string, ThemeData::ThemeSet, ThemeData::StringComparator>::const_iterator
            currentSet {themeSets.find(selectedTheme)};
        if (currentSet == themeSets.cend())
            return;
        // We need to recreate the OptionListComponent entries.
        themeVariant->clearEntries();
        int selectableVariants {0};
        for (auto& variant : currentSet->second.capabilities.variants) {
            if (variant.selectable)
                ++selectableVariants;
        }
        if (selectableVariants > 0) {
            for (auto& variant : currentSet->second.capabilities.variants) {
                if (variant.selectable) {
                    // If required, abbreviate the variant name so it doesn't overlap the
                    // setting name.
                    const float maxNameLength {mSize.x * 0.62f};
                    themeVariant->add(variant.label, variant.name, variant.name == selectedVariant,
                                      maxNameLength);
                }
            }
            if (themeVariant->getSelectedObjects().size() == 0)
                themeVariant->selectEntry(0);
        }
        else {
            if (currentSet->second.capabilities.legacyTheme)
                themeVariant->add("Legacy theme set", "none", true);
            else
                themeVariant->add("None defined", "none", true);
            themeVariant->setEnabled(false);
            themeVariant->setOpacity(DISABLED_OPACITY);
            themeVariant->getParent()
                ->getChild(themeVariant->getChildIndex() - 1)
                ->setOpacity(DISABLED_OPACITY);
        }
    };

    themeVariantsFunc(Settings::getInstance()->getString("ThemeSet"),
                      Settings::getInstance()->getString("ThemeVariant"));

    // Theme color schemes.
    auto themeColorScheme =
        std::make_shared<OptionListComponent<std::string>>(getHelpStyle(), "主题颜色模式", false);
    s->addWithLabel("主题颜色模式", themeColorScheme);
    s->addSaveFunc([themeColorScheme, s] {
        if (themeColorScheme->getSelected() !=
            Settings::getInstance()->getString("ThemeColorScheme")) {
            Settings::getInstance()->setString("ThemeColorScheme", themeColorScheme->getSelected());
            s->setNeedsSaving();
            s->setNeedsReloading();
            s->setInvalidateCachedBackground();
        }
    });

    auto themeColorSchemesFunc = [=](const std::string& selectedTheme,
                                     const std::string& selectedColorScheme) {
        std::map<std::string, ThemeData::ThemeSet, ThemeData::StringComparator>::const_iterator
            currentSet {themeSets.find(selectedTheme)};
        if (currentSet == themeSets.cend())
            return;
        // We need to recreate the OptionListComponent entries.
        themeColorScheme->clearEntries();
        if (currentSet->second.capabilities.colorSchemes.size() > 0) {
            for (auto& colorScheme : currentSet->second.capabilities.colorSchemes) {
                // If required, abbreviate the color scheme name so it doesn't overlap the
                // setting name.
                const float maxNameLength {mSize.x * 0.52f};
                themeColorScheme->add(colorScheme.label, colorScheme.name,
                                      colorScheme.name == selectedColorScheme, maxNameLength);
            }
            if (themeColorScheme->getSelectedObjects().size() == 0)
                themeColorScheme->selectEntry(0);
        }
        else {
            if (currentSet->second.capabilities.legacyTheme)
                themeColorScheme->add("Legacy theme set", "none", true);
            else
                themeColorScheme->add("None defined", "none", true);
            themeColorScheme->setEnabled(false);
            themeColorScheme->setOpacity(DISABLED_OPACITY);
            themeColorScheme->getParent()
                ->getChild(themeColorScheme->getChildIndex() - 1)
                ->setOpacity(DISABLED_OPACITY);
        }
    };

    themeColorSchemesFunc(Settings::getInstance()->getString("ThemeSet"),
                          Settings::getInstance()->getString("ThemeColorScheme"));

    // Theme aspect ratios.
    auto themeAspectRatio =
        std::make_shared<OptionListComponent<std::string>>(getHelpStyle(), "主题纵横比", false);
    s->addWithLabel("主题纵横比", themeAspectRatio);
    s->addSaveFunc([themeAspectRatio, s] {
        if (themeAspectRatio->getSelected() !=
            Settings::getInstance()->getString("ThemeAspectRatio")) {
            Settings::getInstance()->setString("ThemeAspectRatio", themeAspectRatio->getSelected());
            s->setNeedsSaving();
            s->setNeedsReloading();
            s->setInvalidateCachedBackground();
        }
    });

    auto themeAspectRatiosFunc = [=](const std::string& selectedTheme,
                                     const std::string& selectedAspectRatio) {
        std::map<std::string, ThemeData::ThemeSet, ThemeData::StringComparator>::const_iterator
            currentSet {themeSets.find(selectedTheme)};
        if (currentSet == themeSets.cend())
            return;
        // We need to recreate the OptionListComponent entries.
        themeAspectRatio->clearEntries();
        if (currentSet->second.capabilities.aspectRatios.size() > 0) {
            for (auto& aspectRatio : currentSet->second.capabilities.aspectRatios)
                themeAspectRatio->add(ThemeData::getAspectRatioLabel(aspectRatio), aspectRatio,
                                      aspectRatio == selectedAspectRatio);
            if (themeAspectRatio->getSelectedObjects().size() == 0)
                themeAspectRatio->selectEntry(0);
        }
        else {
            if (currentSet->second.capabilities.legacyTheme)
                themeAspectRatio->add("Legacy theme set", "none", true);
            else
                themeAspectRatio->add("None defined", "none", true);
            themeAspectRatio->setEnabled(false);
            themeAspectRatio->setOpacity(DISABLED_OPACITY);
            themeAspectRatio->getParent()
                ->getChild(themeAspectRatio->getChildIndex() - 1)
                ->setOpacity(DISABLED_OPACITY);
        }
    };

    themeAspectRatiosFunc(Settings::getInstance()->getString("ThemeSet"),
                          Settings::getInstance()->getString("ThemeAspectRatio"));

    // Theme transitions.
    auto themeTransitions =
        std::make_shared<OptionListComponent<std::string>>(getHelpStyle(), "主题过渡", false);
    std::string selectedThemeTransitions {Settings::getInstance()->getString("ThemeTransitions")};
    themeTransitions->add("AUTOMATIC", "automatic", selectedThemeTransitions == "automatic");
    // If there are no objects returned, then there must be a manually modified entry in the
    // configuration file. Simply set theme transitions to "automatic" in this case.
    if (themeTransitions->getSelectedObjects().size() == 0)
        themeTransitions->selectEntry(0);
    s->addWithLabel("主题过渡", themeTransitions);
    s->addSaveFunc([themeTransitions, s] {
        if (themeTransitions->getSelected() !=
            Settings::getInstance()->getString("ThemeTransitions")) {
            Settings::getInstance()->setString("ThemeTransitions", themeTransitions->getSelected());
            ThemeData::setThemeTransitions();
            s->setNeedsSaving();
        }
    });

    auto themeTransitionsFunc = [=](const std::string& selectedTheme,
                                    const std::string& selectedThemeTransitions) {
        std::map<std::string, ThemeData::ThemeSet, ThemeData::StringComparator>::const_iterator
            currentSet {themeSets.find(selectedTheme)};
        if (currentSet == themeSets.cend())
            return;
        // We need to recreate the OptionListComponent entries.
        themeTransitions->clearEntries();
        if (currentSet->second.capabilities.legacyTheme) {
            themeTransitions->add("Legacy theme set", "automatic", true);
            themeTransitions->setEnabled(false);
            themeTransitions->setOpacity(DISABLED_OPACITY);
            themeTransitions->getParent()
                ->getChild(themeTransitions->getChildIndex() - 1)
                ->setOpacity(DISABLED_OPACITY);
        }
        else {
            themeTransitions->add("自动", "automatic", "automatic" == selectedThemeTransitions);
            if (currentSet->second.capabilities.transitions.size() == 1 &&
                currentSet->second.capabilities.transitions.front().selectable) {
                std::string label;
                if (currentSet->second.capabilities.transitions.front().label == "")
                    label = "THEME PROFILE";
                else
                    label = currentSet->second.capabilities.transitions.front().label;
                const std::string transitions {
                    currentSet->second.capabilities.transitions.front().name};
                themeTransitions->add(label, transitions, transitions == selectedThemeTransitions);
            }
            else {
                for (size_t i {0}; i < currentSet->second.capabilities.transitions.size(); ++i) {
                    if (!currentSet->second.capabilities.transitions[i].selectable)
                        continue;
                    std::string label;
                    if (currentSet->second.capabilities.transitions[i].label == "")
                        label = "THEME PROFILE " + std::to_string(i + 1);
                    else
                        label = currentSet->second.capabilities.transitions[i].label;
                    const std::string transitions {
                        currentSet->second.capabilities.transitions[i].name};
                    themeTransitions->add(label, transitions,
                                          transitions == selectedThemeTransitions);
                }
            }
            if (std::find(currentSet->second.capabilities.suppressedTransitionProfiles.cbegin(),
                          currentSet->second.capabilities.suppressedTransitionProfiles.cend(),
                          "builtin-instant") ==
                currentSet->second.capabilities.suppressedTransitionProfiles.cend()) {
                themeTransitions->add("INSTANT (BUILT-IN)", "builtin-instant",
                                      "builtin-instant" == selectedThemeTransitions);
            }
            if (std::find(currentSet->second.capabilities.suppressedTransitionProfiles.cbegin(),
                          currentSet->second.capabilities.suppressedTransitionProfiles.cend(),
                          "builtin-slide") ==
                currentSet->second.capabilities.suppressedTransitionProfiles.cend()) {
                themeTransitions->add("SLIDE (BUILT-IN)", "builtin-slide",
                                      "builtin-slide" == selectedThemeTransitions);
            }
            if (std::find(currentSet->second.capabilities.suppressedTransitionProfiles.cbegin(),
                          currentSet->second.capabilities.suppressedTransitionProfiles.cend(),
                          "builtin-fade") ==
                currentSet->second.capabilities.suppressedTransitionProfiles.cend()) {
                themeTransitions->add("FADE (BUILT-IN)", "builtin-fade",
                                      "builtin-fade" == selectedThemeTransitions);
            }
            if (themeTransitions->getSelectedObjects().size() == 0)
                themeTransitions->selectEntry(0);

            if (themeTransitions->getNumEntries() == 1) {
                themeTransitions->setEnabled(false);
                themeTransitions->setOpacity(DISABLED_OPACITY);
                themeTransitions->getParent()
                    ->getChild(themeTransitions->getChildIndex() - 1)
                    ->setOpacity(DISABLED_OPACITY);
            }
            else {
                themeTransitions->setEnabled(true);
                themeTransitions->setOpacity(1.0f);
                themeTransitions->getParent()
                    ->getChild(themeTransitions->getChildIndex() - 1)
                    ->setOpacity(1.0f);
            }
        }
    };

    themeTransitionsFunc(Settings::getInstance()->getString("ThemeSet"),
                         Settings::getInstance()->getString("ThemeTransitions"));

    // Legacy gamelist view style.
    auto gamelistViewStyle = std::make_shared<OptionListComponent<std::string>>(
        getHelpStyle(), "传统游戏列表视图样式", false);
    std::string selectedViewStyle {Settings::getInstance()->getString("GamelistViewStyle")};
    gamelistViewStyle->add("自动", "automatic", selectedViewStyle == "automatic");
    gamelistViewStyle->add("基础", "basic", selectedViewStyle == "basic");
    gamelistViewStyle->add("详细", "detailed", selectedViewStyle == "detailed");
    gamelistViewStyle->add("视频", "video", selectedViewStyle == "video");
    // If there are no objects returned, then there must be a manually modified entry in the
    // configuration file. Simply set the view style to "automatic" in this case.
    if (gamelistViewStyle->getSelectedObjects().size() == 0)
        gamelistViewStyle->selectEntry(0);
    s->addWithLabel("传统游戏列表视图样式", gamelistViewStyle);
    s->addSaveFunc([gamelistViewStyle, s] {
        if (gamelistViewStyle->getSelected() !=
            Settings::getInstance()->getString("GamelistViewStyle")) {
            Settings::getInstance()->setString("GamelistViewStyle",
                                               gamelistViewStyle->getSelected());
            s->setNeedsSaving();
            s->setNeedsReloading();
            s->setInvalidateCachedBackground();
        }
    });

    // Legacy theme transitions.
    auto legacyThemeTransitions =
        std::make_shared<OptionListComponent<std::string>>(getHelpStyle(), "传统主题过渡", false);
    const std::string& selectedLegacyThemeTransitions {
        Settings::getInstance()->getString("LegacyThemeTransitions")};
    legacyThemeTransitions->add("INSTANT", "builtin-instant",
                                selectedLegacyThemeTransitions == "builtin-instant");
    legacyThemeTransitions->add("SLIDE", "builtin-slide",
                                selectedLegacyThemeTransitions == "builtin-slide");
    legacyThemeTransitions->add("FADE", "builtin-fade",
                                selectedLegacyThemeTransitions == "builtin-fade");
    // If there are no objects returned, then there must be a manually modified entry in the
    // configuration file. Simply set legacy theme transitions to "builtin-instant" in this case.
    if (legacyThemeTransitions->getSelectedObjects().size() == 0)
        legacyThemeTransitions->selectEntry(0);
    s->addWithLabel("传统主题过渡", legacyThemeTransitions);
    s->addSaveFunc([legacyThemeTransitions, s] {
        if (legacyThemeTransitions->getSelected() !=
            Settings::getInstance()->getString("LegacyThemeTransitions")) {
            Settings::getInstance()->setString("LegacyThemeTransitions",
                                               legacyThemeTransitions->getSelected());
            ThemeData::setThemeTransitions();
            s->setNeedsSaving();
        }
    });

    // Quick system select (navigate between systems in the gamelist view).
    auto quickSystemSelect =
        std::make_shared<OptionListComponent<std::string>>(getHelpStyle(), "快速选择系统", false);
    std::string selectedQuickSelect {Settings::getInstance()->getString("QuickSystemSelect")};
    quickSystemSelect->add("LEFT/RIGHT OR SHOULDERS", "leftrightshoulders",
                           selectedQuickSelect == "leftrightshoulders");
    quickSystemSelect->add("LEFT/RIGHT OR TRIGGERS", "leftrighttriggers",
                           selectedQuickSelect == "leftrighttriggers");
    quickSystemSelect->add("SHOULDERS", "shoulders", selectedQuickSelect == "shoulders");
    quickSystemSelect->add("TRIGGERS", "triggers", selectedQuickSelect == "triggers");
    quickSystemSelect->add("LEFT/RIGHT", "leftright", selectedQuickSelect == "leftright");
    quickSystemSelect->add("DISABLED", "disabled", selectedQuickSelect == "disabled");
    // If there are no objects returned, then there must be a manually modified entry in the
    // configuration file. Simply set the quick system select to "leftrightshoulders" in this case.
    if (quickSystemSelect->getSelectedObjects().size() == 0)
        quickSystemSelect->selectEntry(0);
    s->addWithLabel("快速选择系统", quickSystemSelect);
    s->addSaveFunc([quickSystemSelect, s] {
        if (quickSystemSelect->getSelected() !=
            Settings::getInstance()->getString("QuickSystemSelect")) {
            Settings::getInstance()->setString("QuickSystemSelect",
                                               quickSystemSelect->getSelected());
            s->setNeedsSaving();
        }
    });

    // Optionally start in selected system/gamelist.
    auto startupSystem =
        std::make_shared<OptionListComponent<std::string>>(getHelpStyle(), "默认游戏列表", false);
    startupSystem->add("无", "", Settings::getInstance()->getString("StartupSystem") == "");
    for (auto it = SystemData::sSystemVector.cbegin(); // Line break.
         it != SystemData::sSystemVector.cend(); ++it) {
        // If required, abbreviate the system name so it doesn't overlap the setting name.
        float maxNameLength {mSize.x * 0.51f};
        startupSystem->add((*it)->getFullName(), (*it)->getName(),
                           Settings::getInstance()->getString("StartupSystem") == (*it)->getName(),
                           maxNameLength);
    }
    // This can probably not happen but as an extra precaution select the "NONE" entry if no
    // entry is selected.
    if (startupSystem->getSelectedObjects().size() == 0)
        startupSystem->selectEntry(0);
    s->addWithLabel("默认游戏列表", startupSystem);
    s->addSaveFunc([startupSystem, s] {
        if (startupSystem->getSelected() != Settings::getInstance()->getString("StartupSystem")) {
            Settings::getInstance()->setString("StartupSystem", startupSystem->getSelected());
            s->setNeedsSaving();
        }
    });

    // Default gamelist sort order.
    std::string sortOrder;
    auto defaultSortOrder = std::make_shared<OptionListComponent<const FileData::SortType*>>(
        getHelpStyle(), "默认排序顺序", false);
    // Exclude the System sort options.
    unsigned int numSortTypes {static_cast<unsigned int>(FileSorts::SortTypes.size() - 2)};
    for (unsigned int i = 0; i < numSortTypes; ++i) {
        if (FileSorts::SortTypes[i].description ==
            Settings::getInstance()->getString("DefaultSortOrder")) {
            sortOrder = FileSorts::SortTypes[i].description;
            break;
        }
    }
    // If an invalid sort order was defined in es_settings.xml, then apply the default
    // sort order 'filename, ascending'.
    if (sortOrder == "")
        sortOrder = Settings::getInstance()->getDefaultString("DefaultSortOrder");
    for (unsigned int i = 0; i < numSortTypes; ++i) {
        const FileData::SortType& sort {FileSorts::SortTypes[i]};
        if (sort.description == sortOrder)
            defaultSortOrder->add(sort.description, &sort, true);
        else
            defaultSortOrder->add(sort.description, &sort, false);
    }
    s->addWithLabel("默认排序顺序", defaultSortOrder);
    s->addSaveFunc([defaultSortOrder, sortOrder, s] {
        std::string selectedSortOrder {defaultSortOrder.get()->getSelected()->description};
        if (selectedSortOrder != sortOrder) {
            Settings::getInstance()->setString("DefaultSortOrder", selectedSortOrder);
            s->setNeedsSaving();
            s->setNeedsSorting();
            s->setNeedsSortingCollections();
            s->setInvalidateCachedBackground();
        }
    });

    // Open menu effect.
    auto menuOpeningEffect =
        std::make_shared<OptionListComponent<std::string>>(getHelpStyle(), "菜单打开特效", false);
    std::string selectedMenuEffect {Settings::getInstance()->getString("MenuOpeningEffect")};
    menuOpeningEffect->add("扩大", "scale-up", selectedMenuEffect == "scale-up");
    menuOpeningEffect->add("无", "none", selectedMenuEffect == "none");
    // If there are no objects returned, then there must be a manually modified entry in the
    // configuration file. Simply set the opening effect to "scale-up" in this case.
    if (menuOpeningEffect->getSelectedObjects().size() == 0)
        menuOpeningEffect->selectEntry(0);
    s->addWithLabel("菜单打开特效", menuOpeningEffect);
    s->addSaveFunc([menuOpeningEffect, s] {
        if (menuOpeningEffect->getSelected() !=
            Settings::getInstance()->getString("MenuOpeningEffect")) {
            Settings::getInstance()->setString("MenuOpeningEffect",
                                               menuOpeningEffect->getSelected());
            s->setNeedsSaving();
        }
    });

    // Launch screen duration.
    auto launchScreenDuration =
        std::make_shared<OptionListComponent<std::string>>(getHelpStyle(), "载入屏幕时长", false);
    std::string selectedDuration {Settings::getInstance()->getString("LaunchScreenDuration")};
    launchScreenDuration->add("标准", "normal", selectedDuration == "normal");
    launchScreenDuration->add("简短", "brief", selectedDuration == "brief");
    launchScreenDuration->add("延长", "long", selectedDuration == "long");
    launchScreenDuration->add("禁用", "disabled", selectedDuration == "disabled");
    // If there are no objects returned, then there must be a manually modified entry in the
    // configuration file. Simply set the duration to "normal" in this case.
    if (launchScreenDuration->getSelectedObjects().size() == 0)
        launchScreenDuration->selectEntry(0);
    s->addWithLabel("载入屏幕时长", launchScreenDuration);
    s->addSaveFunc([launchScreenDuration, s] {
        if (launchScreenDuration->getSelected() !=
            Settings::getInstance()->getString("LaunchScreenDuration")) {
            Settings::getInstance()->setString("LaunchScreenDuration",
                                               launchScreenDuration->getSelected());
            s->setNeedsSaving();
        }
    });

    // UI mode.
    auto uiMode =
        std::make_shared<OptionListComponent<std::string>>(getHelpStyle(), "UI模式", false);
    std::vector<std::string> uiModes;
    uiModes.push_back("full");
    uiModes.push_back("kiosk");
    uiModes.push_back("kid");
    std::string setMode;
    if (Settings::getInstance()->getBool("ForceKiosk"))
        setMode = "kiosk";
    else if (Settings::getInstance()->getBool("ForceKid"))
        setMode = "kid";
    else
        setMode = Settings::getInstance()->getString("UIMode");
    for (auto it = uiModes.cbegin(); it != uiModes.cend(); ++it)
        uiMode->add(*it, *it, setMode == *it);
    s->addWithLabel("UI模式", uiMode);
    s->addSaveFunc([uiMode, this, s] {
        std::string selectedMode {uiMode->getSelected()};
        // If any of the force flags are set, then always apply and save the setting.
        if (selectedMode == Settings::getInstance()->getString("UIMode") &&
            !Settings::getInstance()->getBool("ForceFull") &&
            !Settings::getInstance()->getBool("ForceKiosk") &&
            !Settings::getInstance()->getBool("ForceKid")) {
            return;
        }
        else if (selectedMode != "full") {
            std::string msg {"您正在将UI更改为受限模式\n'" + Utils::String::toUpper(selectedMode) +
                             "'\n"};
            if (selectedMode == "kiosk") {
                msg.append("这将隐藏大多数菜单选项以防止\n");
                msg.append("对系统的更改\n");
            }
            else {
                msg.append("这会将可用游戏限制为标记为\n");
                msg.append("适合儿童的游戏\n");
            }
            msg.append("要解锁并返回完整用户界面，请输入此代码: \n");
            msg.append(UIModeController::getInstance()->getFormattedPassKeyStr() + "\n\n");
            msg.append("你想继续吗?");
            mWindow->pushGui(new GuiMsgBox(
                this->getHelpStyle(), msg, "YES",
                [this, selectedMode] {
                    LOG(LogDebug) << "GuiMenu::openUISettings(): Setting UI mode to '"
                                  << selectedMode << "'.";
                    Settings::getInstance()->setString("UIMode", selectedMode);
                    Settings::getInstance()->setBool("ForceFull", false);
                    Settings::getInstance()->setBool("ForceKiosk", false);
                    Settings::getInstance()->setBool("ForceKid", false);
                    Settings::getInstance()->saveFile();
                    if (CollectionSystemsManager::getInstance()->isEditing())
                        CollectionSystemsManager::getInstance()->exitEditMode();
                    UIModeController::getInstance()->setCurrentUIMode(selectedMode);
                    for (auto it = SystemData::sSystemVector.cbegin();
                         it != SystemData::sSystemVector.cend(); ++it) {
                        if ((*it)->getThemeFolder() == "custom-collections") {
                            for (FileData* customSystem :
                                 (*it)->getRootFolder()->getChildrenListToDisplay())
                                customSystem->getSystem()->getIndex()->resetFilters();
                        }
                        (*it)->sortSystem();
                        (*it)->getIndex()->resetFilters();
                    }
                    ViewController::getInstance()->reloadAll();
                    ViewController::getInstance()->goToSystem(SystemData::sSystemVector.front(),
                                                              false);
                    mWindow->invalidateCachedBackground();
                },
                "NO", nullptr));
        }
        else {
            LOG(LogDebug) << "GuiMenu::openUISettings(): Setting UI mode to '" << selectedMode
                          << "'.";
            Settings::getInstance()->setString("UIMode", uiMode->getSelected());
            Settings::getInstance()->setBool("ForceFull", false);
            Settings::getInstance()->setBool("ForceKiosk", false);
            Settings::getInstance()->setBool("ForceKid", false);
            UIModeController::getInstance()->setCurrentUIMode("full");
            s->setNeedsSaving();
            s->setNeedsSorting();
            s->setNeedsSortingCollections();
            s->setNeedsResetFilters();
            s->setNeedsReloading();
            s->setNeedsGoToSystem(SystemData::sSystemVector.front());
            s->setInvalidateCachedBackground();
        }
    });

    // Random entry button.
    auto randomEntryButton =
        std::make_shared<OptionListComponent<std::string>>(getHelpStyle(), "随机输入按钮", false);
    const std::string selectedRandomEntryButton {
        Settings::getInstance()->getString("RandomEntryButton")};
    randomEntryButton->add("仅游戏", "games", selectedRandomEntryButton == "games");
    randomEntryButton->add("游戏和系统", "gamessystems",
                           selectedRandomEntryButton == "gamessystems");
    randomEntryButton->add("禁用", "disabled", selectedRandomEntryButton == "disabled");
    // If there are no objects returned, then there must be a manually modified entry in the
    // configuration file. Simply set the random entry button to "games" in this case.
    if (randomEntryButton->getSelectedObjects().size() == 0)
        randomEntryButton->selectEntry(0);
    s->addWithLabel("随机输入按钮", randomEntryButton);
    s->addSaveFunc([randomEntryButton, s] {
        if (randomEntryButton->getSelected() !=
            Settings::getInstance()->getString("RandomEntryButton")) {
            Settings::getInstance()->setString("RandomEntryButton",
                                               randomEntryButton->getSelected());
            s->setNeedsSaving();
        }
    });

    // Media viewer.
    ComponentListRow mediaViewerRow;
    mediaViewerRow.elements.clear();
    mediaViewerRow.addElement(
        std::make_shared<TextComponent>("媒体查看器设置", Font::get(FONT_SIZE_MEDIUM), 0x777777FF),
        true);
    mediaViewerRow.addElement(makeArrow(), false);
    mediaViewerRow.makeAcceptInputHandler(std::bind(&GuiMenu::openMediaViewerOptions, this));
    s->addRow(mediaViewerRow);

    // Screensaver.
    ComponentListRow screensaverRow;
    screensaverRow.elements.clear();
    screensaverRow.addElement(
        std::make_shared<TextComponent>("屏保设置", Font::get(FONT_SIZE_MEDIUM), 0x777777FF), true);
    screensaverRow.addElement(makeArrow(), false);
    screensaverRow.makeAcceptInputHandler(std::bind(&GuiMenu::openScreensaverOptions, this));
    s->addRow(screensaverRow);

    // Enable theme variant triggers.
    auto themeVariantTriggers = std::make_shared<SwitchComponent>();
    themeVariantTriggers->setState(Settings::getInstance()->getBool("ThemeVariantTriggers"));
    s->addWithLabel("启用可变主题触发器", themeVariantTriggers);
    s->addSaveFunc([themeVariantTriggers, s] {
        if (themeVariantTriggers->getState() !=
            Settings::getInstance()->getBool("ThemeVariantTriggers")) {
            Settings::getInstance()->setBool("ThemeVariantTriggers",
                                             themeVariantTriggers->getState());
            s->setNeedsSaving();
            s->setNeedsReloading();
            s->setInvalidateCachedBackground();
        }
    });

    // Blur background when the menu is open.
    auto menuBlurBackground = std::make_shared<SwitchComponent>();
    if (mRenderer->getScreenRotation() == 90 || mRenderer->getScreenRotation() == 270) {
        // TODO: Add support for non-blurred background when rotating screen 90 or 270 degrees.
        menuBlurBackground->setState(true);
        s->addWithLabel("当菜单打开时背景模糊", menuBlurBackground);
        menuBlurBackground->setEnabled(false);
        menuBlurBackground->setOpacity(DISABLED_OPACITY);
        menuBlurBackground->getParent()
            ->getChild(menuBlurBackground->getChildIndex() - 1)
            ->setOpacity(DISABLED_OPACITY);
    }
    else {
        menuBlurBackground->setState(Settings::getInstance()->getBool("MenuBlurBackground"));
        s->addWithLabel("当菜单打开时背景模糊", menuBlurBackground);
        s->addSaveFunc([menuBlurBackground, s] {
            if (menuBlurBackground->getState() !=
                Settings::getInstance()->getBool("MenuBlurBackground")) {
                Settings::getInstance()->setBool("MenuBlurBackground",
                                                 menuBlurBackground->getState());
                s->setNeedsSaving();
                s->setInvalidateCachedBackground();
            }
        });
    }

    // Display pillarboxes (and letterboxes) for videos in the gamelists.
    auto gamelistVideoPillarbox = std::make_shared<SwitchComponent>();
    gamelistVideoPillarbox->setState(Settings::getInstance()->getBool("GamelistVideoPillarbox"));
    s->addWithLabel("为游戏列表视频显示PillarBoxes", gamelistVideoPillarbox);
    s->addSaveFunc([gamelistVideoPillarbox, s] {
        if (gamelistVideoPillarbox->getState() !=
            Settings::getInstance()->getBool("GamelistVideoPillarbox")) {
            Settings::getInstance()->setBool("GamelistVideoPillarbox",
                                             gamelistVideoPillarbox->getState());
            s->setNeedsSaving();
        }
    });

    // Render scanlines for videos in the gamelists.
    auto gamelistVideoScanlines = std::make_shared<SwitchComponent>();
    gamelistVideoScanlines->setState(Settings::getInstance()->getBool("GamelistVideoScanlines"));
    s->addWithLabel("为游戏列表视频渲染扫描线", gamelistVideoScanlines);
    s->addSaveFunc([gamelistVideoScanlines, s] {
        if (gamelistVideoScanlines->getState() !=
            Settings::getInstance()->getBool("GamelistVideoScanlines")) {
            Settings::getInstance()->setBool("GamelistVideoScanlines",
                                             gamelistVideoScanlines->getState());
            s->setNeedsSaving();
        }
    });

    // Sort folders on top of the gamelists.
    auto foldersOnTop = std::make_shared<SwitchComponent>();
    foldersOnTop->setState(Settings::getInstance()->getBool("FoldersOnTop"));
    s->addWithLabel("在游戏列表顶部排序文件夹", foldersOnTop);
    s->addSaveFunc([foldersOnTop, s] {
        if (foldersOnTop->getState() != Settings::getInstance()->getBool("FoldersOnTop")) {
            Settings::getInstance()->setBool("FoldersOnTop", foldersOnTop->getState());
            s->setNeedsSaving();
            s->setNeedsSorting();
            s->setInvalidateCachedBackground();
        }
    });

    // Sort favorites on top of non-favorites in the gamelists.
    auto favoritesFirst = std::make_shared<SwitchComponent>();
    favoritesFirst->setState(Settings::getInstance()->getBool("FavoritesFirst"));
    s->addWithLabel("将收藏的游戏放在其他游戏前面", favoritesFirst);
    s->addSaveFunc([favoritesFirst, s] {
        if (favoritesFirst->getState() != Settings::getInstance()->getBool("FavoritesFirst")) {
            Settings::getInstance()->setBool("FavoritesFirst", favoritesFirst->getState());
            s->setNeedsSaving();
            s->setNeedsSorting();
            s->setNeedsSortingCollections();
            s->setInvalidateCachedBackground();
        }
    });

    // Enable gamelist star markings for favorite games.
    auto favoritesStar = std::make_shared<SwitchComponent>();
    favoritesStar->setState(Settings::getInstance()->getBool("FavoritesStar"));
    s->addWithLabel("为喜欢的游戏添加星标", favoritesStar);
    s->addSaveFunc([favoritesStar, s] {
        if (favoritesStar->getState() != Settings::getInstance()->getBool("FavoritesStar")) {
            Settings::getInstance()->setBool("FavoritesStar", favoritesStar->getState());
            s->setNeedsSaving();
            s->setNeedsReloading();
            s->setInvalidateCachedBackground();
        }
    });

    // Enable quick list scrolling overlay.
    auto listScrollOverlay = std::make_shared<SwitchComponent>();
    listScrollOverlay->setState(Settings::getInstance()->getBool("ListScrollOverlay"));
    s->addWithLabel("启用文本列表快速滚动覆盖", listScrollOverlay);
    s->addSaveFunc([listScrollOverlay, s] {
        if (listScrollOverlay->getState() !=
            Settings::getInstance()->getBool("ListScrollOverlay")) {
            Settings::getInstance()->setBool("ListScrollOverlay", listScrollOverlay->getState());
            s->setNeedsSaving();
        }
    });

    // Enable virtual (on-screen) keyboard.
    auto virtualKeyboard = std::make_shared<SwitchComponent>();
    virtualKeyboard->setState(Settings::getInstance()->getBool("VirtualKeyboard"));
    s->addWithLabel("启用虚拟键盘", virtualKeyboard);
    s->addSaveFunc([virtualKeyboard, s] {
        if (virtualKeyboard->getState() != Settings::getInstance()->getBool("VirtualKeyboard")) {
            Settings::getInstance()->setBool("VirtualKeyboard", virtualKeyboard->getState());
            s->setNeedsSaving();
            s->setInvalidateCachedBackground();
        }
    });

    // Enable the 'Y' button for tagging games as favorites.
    auto favoritesAddButton = std::make_shared<SwitchComponent>();
    favoritesAddButton->setState(Settings::getInstance()->getBool("FavoritesAddButton"));
    s->addWithLabel("启用切换收藏夹按钮", favoritesAddButton);
    s->addSaveFunc([favoritesAddButton, s] {
        if (Settings::getInstance()->getBool("FavoritesAddButton") !=
            favoritesAddButton->getState()) {
            Settings::getInstance()->setBool("FavoritesAddButton", favoritesAddButton->getState());
            s->setNeedsSaving();
        }
    });

    // Gamelist filters.
    auto gamelistFilters = std::make_shared<SwitchComponent>();
    gamelistFilters->setState(Settings::getInstance()->getBool("GamelistFilters"));
    s->addWithLabel("启用游戏列表过滤器", gamelistFilters);
    s->addSaveFunc([gamelistFilters, s] {
        if (Settings::getInstance()->getBool("GamelistFilters") != gamelistFilters->getState()) {
            Settings::getInstance()->setBool("GamelistFilters", gamelistFilters->getState());
            s->setNeedsSaving();
            s->setNeedsReloading();
        }
    });

    // On-screen help prompts.
    auto showHelpPrompts = std::make_shared<SwitchComponent>();
    showHelpPrompts->setState(Settings::getInstance()->getBool("ShowHelpPrompts"));
    s->addWithLabel("屏幕上显示帮助", showHelpPrompts);
    s->addSaveFunc([showHelpPrompts, s] {
        if (Settings::getInstance()->getBool("ShowHelpPrompts") != showHelpPrompts->getState()) {
            Settings::getInstance()->setBool("ShowHelpPrompts", showHelpPrompts->getState());
            s->setNeedsSaving();
        }
    });

    // When the theme set entries are scrolled or selected, update the relevant rows.
    auto scrollThemeSetFunc = [=](const std::string& themeName, bool firstRun = false) {
        auto selectedSet = themeSets.find(themeName);
        if (selectedSet == themeSets.cend())
            return;
        if (!firstRun) {
            themeVariantsFunc(themeName, themeVariant->getSelected());
            themeColorSchemesFunc(themeName, themeColorScheme->getSelected());
            themeAspectRatiosFunc(themeName, themeAspectRatio->getSelected());
            themeTransitionsFunc(themeName, themeTransitions->getSelected());
        }
        int selectableVariants {0};
        for (auto& variant : selectedSet->second.capabilities.variants) {
            if (variant.selectable)
                ++selectableVariants;
        }
        if (!selectedSet->second.capabilities.legacyTheme && selectableVariants > 0) {
            themeVariant->setEnabled(true);
            themeVariant->setOpacity(1.0f);
            themeVariant->getParent()
                ->getChild(themeVariant->getChildIndex() - 1)
                ->setOpacity(1.0f);
        }
        else {
            themeVariant->setEnabled(false);
            themeVariant->setOpacity(DISABLED_OPACITY);
            themeVariant->getParent()
                ->getChild(themeVariant->getChildIndex() - 1)
                ->setOpacity(DISABLED_OPACITY);
        }
        if (!selectedSet->second.capabilities.legacyTheme &&
            selectedSet->second.capabilities.colorSchemes.size() > 0) {
            themeColorScheme->setEnabled(true);
            themeColorScheme->setOpacity(1.0f);
            themeColorScheme->getParent()
                ->getChild(themeColorScheme->getChildIndex() - 1)
                ->setOpacity(1.0f);
        }
        else {
            themeColorScheme->setEnabled(false);
            themeColorScheme->setOpacity(DISABLED_OPACITY);
            themeColorScheme->getParent()
                ->getChild(themeColorScheme->getChildIndex() - 1)
                ->setOpacity(DISABLED_OPACITY);
        }
        if (!selectedSet->second.capabilities.legacyTheme &&
            selectedSet->second.capabilities.aspectRatios.size() > 0) {
            themeAspectRatio->setEnabled(true);
            themeAspectRatio->setOpacity(1.0f);
            themeAspectRatio->getParent()
                ->getChild(themeAspectRatio->getChildIndex() - 1)
                ->setOpacity(1.0f);
        }
        else {
            themeAspectRatio->setEnabled(false);
            themeAspectRatio->setOpacity(DISABLED_OPACITY);
            themeAspectRatio->getParent()
                ->getChild(themeAspectRatio->getChildIndex() - 1)
                ->setOpacity(DISABLED_OPACITY);
        }
        if (!selectedSet->second.capabilities.legacyTheme) {
            gamelistViewStyle->setEnabled(false);
            gamelistViewStyle->setOpacity(DISABLED_OPACITY);
            gamelistViewStyle->getParent()
                ->getChild(gamelistViewStyle->getChildIndex() - 1)
                ->setOpacity(DISABLED_OPACITY);

            legacyThemeTransitions->setEnabled(false);
            legacyThemeTransitions->setOpacity(DISABLED_OPACITY);
            legacyThemeTransitions->getParent()
                ->getChild(legacyThemeTransitions->getChildIndex() - 1)
                ->setOpacity(DISABLED_OPACITY);

            // Pillarboxes are theme-controlled for newer themes.
            gamelistVideoPillarbox->setEnabled(false);
            gamelistVideoPillarbox->setOpacity(DISABLED_OPACITY);
            gamelistVideoPillarbox->getParent()
                ->getChild(gamelistVideoPillarbox->getChildIndex() - 1)
                ->setOpacity(DISABLED_OPACITY);

            // Scanlines are theme-controlled for newer themes.
            gamelistVideoScanlines->setEnabled(false);
            gamelistVideoScanlines->setOpacity(DISABLED_OPACITY);
            gamelistVideoScanlines->getParent()
                ->getChild(gamelistVideoScanlines->getChildIndex() - 1)
                ->setOpacity(DISABLED_OPACITY);
        }
        else {
            gamelistViewStyle->setEnabled(true);
            gamelistViewStyle->setOpacity(1.0f);
            gamelistViewStyle->getParent()
                ->getChild(gamelistViewStyle->getChildIndex() - 1)
                ->setOpacity(1.0f);

            themeTransitions->setEnabled(false);
            themeTransitions->setOpacity(DISABLED_OPACITY);
            themeTransitions->getParent()
                ->getChild(themeTransitions->getChildIndex() - 1)
                ->setOpacity(DISABLED_OPACITY);

            legacyThemeTransitions->setEnabled(true);
            legacyThemeTransitions->setOpacity(1.0f);
            legacyThemeTransitions->getParent()
                ->getChild(legacyThemeTransitions->getChildIndex() - 1)
                ->setOpacity(1.0f);

            gamelistVideoPillarbox->setEnabled(true);
            gamelistVideoPillarbox->setOpacity(1.0f);
            gamelistVideoPillarbox->getParent()
                ->getChild(gamelistVideoPillarbox->getChildIndex() - 1)
                ->setOpacity(1.0f);

            gamelistVideoScanlines->setEnabled(true);
            gamelistVideoScanlines->setOpacity(1.0f);
            gamelistVideoScanlines->getParent()
                ->getChild(gamelistVideoScanlines->getChildIndex() - 1)
                ->setOpacity(1.0f);
        }
    };

    scrollThemeSetFunc(selectedSet->first, true);
    themeSet->setCallback(scrollThemeSetFunc);

    s->setSize(mSize);
    mWindow->pushGui(s);
}

void GuiMenu::openSoundOptions()
{
    auto s = new GuiSettings("声音设置");

// TODO: Hide the volume slider on macOS and BSD Unix until the volume control logic has been
// implemented for these operating systems.
#if !defined(__APPLE__) && !defined(__FreeBSD__) && !defined(__OpenBSD__) && !defined(__NetBSD__)
    // System volume.
    // The reason to create the VolumeControl object every time instead of making it a singleton
    // is that this is the easiest way to detect new default audio devices or changes to the
    // audio volume done by the operating system. And we don't really need this object laying
    // around anyway as it's only used here.
    VolumeControl volumeControl;
    int currentVolume {volumeControl.getVolume()};

    auto systemVolume = std::make_shared<SliderComponent>(0.0f, 100.0f, 1.0f, "%");
    systemVolume->setValue(static_cast<float>(currentVolume));
    s->addWithLabel("系统音量", systemVolume);
    s->addSaveFunc([systemVolume, currentVolume] {
        // No need to create the VolumeControl object unless the volume has actually been changed.
        if (static_cast<int>(systemVolume->getValue()) != currentVolume) {
            VolumeControl volumeControl;
            volumeControl.setVolume(static_cast<int>(std::round(systemVolume->getValue())));
        }
    });
#endif

    // Volume for navigation sounds.
    auto soundVolumeNavigation = std::make_shared<SliderComponent>(0.0f, 100.0f, 1.0f, "%");
    soundVolumeNavigation->setValue(
        static_cast<float>(Settings::getInstance()->getInt("SoundVolumeNavigation")));
    s->addWithLabel("导航音量", soundVolumeNavigation);
    s->addSaveFunc([soundVolumeNavigation, s] {
        if (soundVolumeNavigation->getValue() !=
            static_cast<float>(Settings::getInstance()->getInt("SoundVolumeNavigation"))) {
            Settings::getInstance()->setInt("SoundVolumeNavigation",
                                            static_cast<int>(soundVolumeNavigation->getValue()));
            s->setNeedsSaving();
        }
    });

    // Volume for videos.
    auto soundVolumeVideos = std::make_shared<SliderComponent>(0.0f, 100.0f, 1.0f, "%");
    soundVolumeVideos->setValue(
        static_cast<float>(Settings::getInstance()->getInt("SoundVolumeVideos")));
    s->addWithLabel("视频音量", soundVolumeVideos);
    s->addSaveFunc([soundVolumeVideos, s] {
        if (soundVolumeVideos->getValue() !=
            static_cast<float>(Settings::getInstance()->getInt("SoundVolumeVideos"))) {
            Settings::getInstance()->setInt("SoundVolumeVideos",
                                            static_cast<int>(soundVolumeVideos->getValue()));
            s->setNeedsSaving();
        }
    });

    if (UIModeController::getInstance()->isUIModeFull()) {
        // Play audio for gamelist videos.
        auto viewsVideoAudio = std::make_shared<SwitchComponent>();
        viewsVideoAudio->setState(Settings::getInstance()->getBool("ViewsVideoAudio"));
        s->addWithLabel("为游戏列表和系统视图视频播放音频", viewsVideoAudio);
        s->addSaveFunc([viewsVideoAudio, s] {
            if (viewsVideoAudio->getState() !=
                Settings::getInstance()->getBool("ViewsVideoAudio")) {
                Settings::getInstance()->setBool("ViewsVideoAudio", viewsVideoAudio->getState());
                s->setNeedsSaving();
            }
        });

        // Play audio for media viewer videos.
        auto mediaViewerVideoAudio = std::make_shared<SwitchComponent>();
        mediaViewerVideoAudio->setState(Settings::getInstance()->getBool("MediaViewerVideoAudio"));
        s->addWithLabel("为媒体查看器视频播放音频", mediaViewerVideoAudio);
        s->addSaveFunc([mediaViewerVideoAudio, s] {
            if (mediaViewerVideoAudio->getState() !=
                Settings::getInstance()->getBool("MediaViewerVideoAudio")) {
                Settings::getInstance()->setBool("MediaViewerVideoAudio",
                                                 mediaViewerVideoAudio->getState());
                s->setNeedsSaving();
            }
        });

        // Play audio for screensaver videos.
        auto screensaverVideoAudio = std::make_shared<SwitchComponent>();
        screensaverVideoAudio->setState(Settings::getInstance()->getBool("ScreensaverVideoAudio"));
        s->addWithLabel("为屏保视频播放音频", screensaverVideoAudio);
        s->addSaveFunc([screensaverVideoAudio, s] {
            if (screensaverVideoAudio->getState() !=
                Settings::getInstance()->getBool("ScreensaverVideoAudio")) {
                Settings::getInstance()->setBool("ScreensaverVideoAudio",
                                                 screensaverVideoAudio->getState());
                s->setNeedsSaving();
            }
        });

        // Navigation sounds.
        auto navigationSounds = std::make_shared<SwitchComponent>();
        navigationSounds->setState(Settings::getInstance()->getBool("NavigationSounds"));
        s->addWithLabel("启用导航音效", navigationSounds);
        s->addSaveFunc([navigationSounds, s] {
            if (navigationSounds->getState() !=
                Settings::getInstance()->getBool("NavigationSounds")) {
                Settings::getInstance()->setBool("NavigationSounds", navigationSounds->getState());
                s->setNeedsSaving();
            }
        });
    }

    s->setSize(mSize);
    mWindow->pushGui(s);
}

void GuiMenu::openInputDeviceOptions()
{
    auto s = new GuiSettings("输入设备设置");

    // Controller type.
    auto inputControllerType =
        std::make_shared<OptionListComponent<std::string>>(getHelpStyle(), "控制器类型", false);
    std::string selectedPlayer {Settings::getInstance()->getString("InputControllerType")};
    inputControllerType->add("XBOX", "xbox", selectedPlayer == "xbox");
    inputControllerType->add("XBOX 360", "xbox360", selectedPlayer == "xbox360");
    inputControllerType->add("PLAYSTATION 1/2/3", "ps123", selectedPlayer == "ps123");
    inputControllerType->add("PLAYSTATION 4", "ps4", selectedPlayer == "ps4");
    inputControllerType->add("PLAYSTATION 5", "ps5", selectedPlayer == "ps5");
    inputControllerType->add("SWITCH PRO", "switchpro", selectedPlayer == "switchpro");
    inputControllerType->add("SNES", "snes", selectedPlayer == "snes");
    // If there are no objects returned, then there must be a manually modified entry in the
    // configuration file. Simply set the controller type to "xbox" in this case.
    if (inputControllerType->getSelectedObjects().size() == 0)
        inputControllerType->selectEntry(0);
    s->addWithLabel("控制器类型", inputControllerType);
    s->addSaveFunc([inputControllerType, s] {
        if (inputControllerType->getSelected() !=
            Settings::getInstance()->getString("InputControllerType")) {
            Settings::getInstance()->setString("InputControllerType",
                                               inputControllerType->getSelected());
            s->setNeedsSaving();
        }
    });

    // Whether to only accept input from the first controller.
    auto inputOnlyFirstController = std::make_shared<SwitchComponent>();
    inputOnlyFirstController->setState(
        Settings::getInstance()->getBool("InputOnlyFirstController"));
    s->addWithLabel("只接受来自第一个控制器的输入", inputOnlyFirstController);
    s->addSaveFunc([inputOnlyFirstController, s] {
        if (Settings::getInstance()->getBool("InputOnlyFirstController") !=
            inputOnlyFirstController->getState()) {
            Settings::getInstance()->setBool("InputOnlyFirstController",
                                             inputOnlyFirstController->getState());
            s->setNeedsSaving();
        }
    });

    // Whether to ignore keyboard input (except the quit shortcut).
    auto inputIgnoreKeyboard = std::make_shared<SwitchComponent>();
    inputIgnoreKeyboard->setState(Settings::getInstance()->getBool("InputIgnoreKeyboard"));
    s->addWithLabel("忽略键盘输入", inputIgnoreKeyboard);
    s->addSaveFunc([inputIgnoreKeyboard, s] {
        if (Settings::getInstance()->getBool("InputIgnoreKeyboard") !=
            inputIgnoreKeyboard->getState()) {
            Settings::getInstance()->setBool("InputIgnoreKeyboard",
                                             inputIgnoreKeyboard->getState());
            s->setNeedsSaving();
        }
    });

    // Configure keyboard and controllers.
    ComponentListRow configureInputRow;
    configureInputRow.elements.clear();
    configureInputRow.addElement(std::make_shared<TextComponent>(
                                     "配置键盘和控制器", Font::get(FONT_SIZE_MEDIUM), 0x777777FF),
                                 true);
    configureInputRow.addElement(makeArrow(), false);
    configureInputRow.makeAcceptInputHandler(std::bind(&GuiMenu::openConfigInput, this, s));
    s->addRow(configureInputRow);

    s->setSize(mSize);
    mWindow->pushGui(s);
}

void GuiMenu::openConfigInput(GuiSettings* settings)
{
    // Always save the settings before starting the input configuration, in case the
    // controller type was changed.
    settings->save();
    // Also unset the save flag so that a double saving does not take place when closing
    // the input device settings menu later on.
    settings->setNeedsSaving(false);

    std::string message;
    if (mRenderer->getIsVerticalOrientation()) {
        message = "键盘和控制器是自动配置的\n"
                  "但使用此配置工具您可以覆盖默认按钮映射\n"
                  "(这不会影响帮助提示)继续吗？\n";
    }
    else {
        message = "键盘和控制器是自动配置的\n"
                  "但使用此配置工具您可以覆盖默认按钮映射\n"
                  "(这不会影响帮助提示)继续吗？\n";
    }

    Window* window {mWindow};
    window->pushGui(new GuiMsgBox(
        getHelpStyle(), message, "确认",
        [window] { window->pushGui(new GuiDetectDevice(false, false, nullptr)); }, "取消",
        nullptr));
}

void GuiMenu::openOtherOptions()
{
    auto s = new GuiSettings("其他设置");

    // Alternative emulators GUI.
    ComponentListRow alternativeEmulatorsRow;
    alternativeEmulatorsRow.elements.clear();
    alternativeEmulatorsRow.addElement(
        std::make_shared<TextComponent>("可替代模拟器", Font::get(FONT_SIZE_MEDIUM), 0x777777FF),
        true);
    alternativeEmulatorsRow.addElement(makeArrow(), false);
    alternativeEmulatorsRow.makeAcceptInputHandler(
        std::bind([this] { mWindow->pushGui(new GuiAlternativeEmulators); }));
    s->addRow(alternativeEmulatorsRow);

    // Game media directory.
    ComponentListRow rowMediaDir;
    auto mediaDirectory =
        std::make_shared<TextComponent>("游戏媒体目录", Font::get(FONT_SIZE_MEDIUM), 0x777777FF);
    auto bracketMediaDirectory = std::make_shared<ImageComponent>();
    bracketMediaDirectory->setResize(
        glm::vec2 {0.0f, Font::get(FONT_SIZE_MEDIUM)->getLetterHeight()});
    bracketMediaDirectory->setImage(":/graphics/arrow.svg");
    rowMediaDir.addElement(mediaDirectory, true);
    rowMediaDir.addElement(bracketMediaDirectory, false);
    std::string titleMediaDir {"输入游戏媒体目录"};
    std::string mediaDirectoryStaticText {"默认目录:"};
    std::string defaultDirectoryText {"~/.emulationstation/downloaded_media/"};
    std::string initValueMediaDir {Settings::getInstance()->getString("MediaDirectory")};
    bool multiLineMediaDir {false};
    auto updateValMediaDir = [this](const std::string& newVal) {
        Settings::getInstance()->setString("MediaDirectory", newVal);
        Settings::getInstance()->saveFile();
        ViewController::getInstance()->reloadAll();
        mWindow->invalidateCachedBackground();
    };
    rowMediaDir.makeAcceptInputHandler([this, s, titleMediaDir, mediaDirectoryStaticText,
                                        defaultDirectoryText, initValueMediaDir, updateValMediaDir,
                                        multiLineMediaDir] {
        if (Settings::getInstance()->getBool("VirtualKeyboard")) {
            mWindow->pushGui(new GuiTextEditKeyboardPopup(
                getHelpStyle(), s->getMenu().getPosition().y, titleMediaDir,
                Settings::getInstance()->getString("MediaDirectory"), updateValMediaDir,
                multiLineMediaDir, "保存", "保存更改?", mediaDirectoryStaticText,
                defaultDirectoryText, "load default directory"));
        }
        else {
            mWindow->pushGui(new GuiTextEditPopup(
                getHelpStyle(), titleMediaDir, Settings::getInstance()->getString("MediaDirectory"),
                updateValMediaDir, multiLineMediaDir, "保存", "保存更改?", mediaDirectoryStaticText,
                defaultDirectoryText, "load default directory"));
        }
    });
    s->addRow(rowMediaDir);

    // Maximum VRAM.
    auto maxVram = std::make_shared<SliderComponent>(128.0f, 2048.0f, 16.0f, "MiB");
    maxVram->setValue(static_cast<float>(Settings::getInstance()->getInt("MaxVRAM")));
    s->addWithLabel("虚拟内存限制", maxVram);
    s->addSaveFunc([maxVram, s] {
        if (maxVram->getValue() != Settings::getInstance()->getInt("MaxVRAM")) {
            Settings::getInstance()->setInt("MaxVRAM",
                                            static_cast<int>(std::round(maxVram->getValue())));
            s->setNeedsSaving();
        }
    });

#if !defined(USE_OPENGLES)
    // Anti-aliasing (MSAA).
    auto antiAliasing =
        std::make_shared<OptionListComponent<std::string>>(getHelpStyle(), "反锯齿(MSAA)", false);
    const std::string& selectedAntiAliasing {
        std::to_string(Settings::getInstance()->getInt("AntiAliasing"))};
    antiAliasing->add("DISABLED", "0", selectedAntiAliasing == "0");
    antiAliasing->add("2X", "2", selectedAntiAliasing == "2");
    antiAliasing->add("4X", "4", selectedAntiAliasing == "4");
    // If there are no objects returned, then there must be a manually modified entry in the
    // configuration file. Simply set anti-aliasing to "0" in this case.
    if (antiAliasing->getSelectedObjects().size() == 0)
        antiAliasing->selectEntry(0);
    s->addWithLabel("反锯齿(MSAA) (需重启)", antiAliasing);
    s->addSaveFunc([antiAliasing, s] {
        if (antiAliasing->getSelected() !=
            std::to_string(Settings::getInstance()->getInt("AntiAliasing"))) {
            Settings::getInstance()->setInt("AntiAliasing",
                                            atoi(antiAliasing->getSelected().c_str()));
            s->setNeedsSaving();
        }
    });
#endif

    // Display/monitor.
    auto displayIndex = std::make_shared<OptionListComponent<std::string>>(
        getHelpStyle(), "显示/监视器索引", false);
    std::vector<std::string> displayIndexEntry;
    displayIndexEntry.push_back("1");
    displayIndexEntry.push_back("2");
    displayIndexEntry.push_back("3");
    displayIndexEntry.push_back("4");
    for (auto it = displayIndexEntry.cbegin(); it != displayIndexEntry.cend(); ++it)
        displayIndex->add(*it, *it,
                          Settings::getInstance()->getInt("DisplayIndex") == atoi((*it).c_str()));
    s->addWithLabel("显示/监视器索引(需重启)", displayIndex);
    s->addSaveFunc([displayIndex, s] {
        if (atoi(displayIndex->getSelected().c_str()) !=
            Settings::getInstance()->getInt("DisplayIndex")) {
            Settings::getInstance()->setInt("DisplayIndex",
                                            atoi(displayIndex->getSelected().c_str()));
            s->setNeedsSaving();
        }
    });

    // Screen contents rotation.
    auto screenRotate =
        std::make_shared<OptionListComponent<std::string>>(getHelpStyle(), "旋转屏幕", false);
    const std::string& selectedScreenRotate {
        std::to_string(Settings::getInstance()->getInt("ScreenRotate"))};
    screenRotate->add("禁用", "0", selectedScreenRotate == "0");
    screenRotate->add("90度", "90", selectedScreenRotate == "90");
    screenRotate->add("180度", "180", selectedScreenRotate == "180");
    screenRotate->add("270度", "270", selectedScreenRotate == "270");
    // If there are no objects returned, then there must be a manually modified entry in the
    // configuration file. Simply set screen rotation to "0" in this case.
    if (screenRotate->getSelectedObjects().size() == 0)
        screenRotate->selectEntry(0);
    s->addWithLabel("旋转屏幕(需重启)", screenRotate);
    s->addSaveFunc([screenRotate, s] {
        if (screenRotate->getSelected() !=
            std::to_string(Settings::getInstance()->getInt("ScreenRotate"))) {
            Settings::getInstance()->setInt("ScreenRotate",
                                            atoi(screenRotate->getSelected().c_str()));
            s->setNeedsSaving();
        }
    });

    // Keyboard quit shortcut.
    auto keyboardQuitShortcut =
        std::make_shared<OptionListComponent<std::string>>(getHelpStyle(), "键盘退出热键", false);
    std::string selectedShortcut {Settings::getInstance()->getString("KeyboardQuitShortcut")};
#if defined(_WIN64) || defined(__unix__)
    keyboardQuitShortcut->add("Alt + F4", "AltF4", selectedShortcut == "AltF4");
    keyboardQuitShortcut->add("Ctrl + Q", "CtrlQ", selectedShortcut == "CtrlQ");
    keyboardQuitShortcut->add("Alt + Q", "AltQ", selectedShortcut == "AltQ");
#endif
#if defined(__APPLE__)
    keyboardQuitShortcut->add("\u2318 + Q", "CmdQ", selectedShortcut == "CmdQ");
    keyboardQuitShortcut->add("Ctrl + Q", "CtrlQ", selectedShortcut == "CtrlQ");
    keyboardQuitShortcut->add("Alt + Q", "AltQ", selectedShortcut == "AltQ");
#endif
    keyboardQuitShortcut->add("F4", "F4", selectedShortcut == "F4");
    // If there are no objects returned, then there must be a manually modified entry in the
    // configuration file. Simply set the keyboard quit shortcut to the first entry in this case.
    if (keyboardQuitShortcut->getSelectedObjects().size() == 0)
        keyboardQuitShortcut->selectEntry(0);
    s->addWithLabel("键盘退出热键", keyboardQuitShortcut);
    s->addSaveFunc([keyboardQuitShortcut, s] {
        if (keyboardQuitShortcut->getSelected() !=
            Settings::getInstance()->getString("KeyboardQuitShortcut")) {
            Settings::getInstance()->setString("KeyboardQuitShortcut",
                                               keyboardQuitShortcut->getSelected());
            s->setNeedsSaving();
        }
    });

    // When to save game metadata.
    auto saveGamelistsMode = std::make_shared<OptionListComponent<std::string>>(
        getHelpStyle(), "何时保存游戏元数据", false);
    std::vector<std::string> saveModes;
    saveModes.push_back("on exit");
    saveModes.push_back("always");
    saveModes.push_back("never");
    for (auto it = saveModes.cbegin(); it != saveModes.cend(); ++it) {
        saveGamelistsMode->add(*it, *it,
                               Settings::getInstance()->getString("SaveGamelistsMode") == *it);
    }
    s->addWithLabel("何时保存游戏元数据", saveGamelistsMode);
    s->addSaveFunc([saveGamelistsMode, s] {
        if (saveGamelistsMode->getSelected() !=
            Settings::getInstance()->getString("SaveGamelistsMode")) {
            Settings::getInstance()->setString("SaveGamelistsMode",
                                               saveGamelistsMode->getSelected());
            // Always save the gamelist.xml files if switching to "always" as there may
            // be changes that will otherwise be lost.
            if (Settings::getInstance()->getString("SaveGamelistsMode") == "always") {
                for (auto it = SystemData::sSystemVector.cbegin();
                     it != SystemData::sSystemVector.cend(); ++it)
                    (*it)->writeMetaData();
            }
            s->setNeedsSaving();
        }
    });

#if defined(APPLICATION_UPDATER)
    // Application updater frequency.
    auto applicationUpdaterFrequency =
        std::make_shared<OptionListComponent<std::string>>(getHelpStyle(), "更新程序频率", false);
    const std::string& selectedUpdaterFrequency {
        Settings::getInstance()->getString("ApplicationUpdaterFrequency")};
    applicationUpdaterFrequency->add("总是", "always", selectedUpdaterFrequency == "always");
    applicationUpdaterFrequency->add("每天", "daily", selectedUpdaterFrequency == "daily");
    applicationUpdaterFrequency->add("每周", "weekly", selectedUpdaterFrequency == "weekly");
    applicationUpdaterFrequency->add("每月", "monthly", selectedUpdaterFrequency == "monthly");
    applicationUpdaterFrequency->add("从不", "never", selectedUpdaterFrequency == "never");
    // If there are no objects returned, then there must be a manually modified entry in the
    // configuration file. Simply set updater frequency to "always" in this case.
    if (applicationUpdaterFrequency->getSelectedObjects().size() == 0)
        applicationUpdaterFrequency->selectEntry(0);
    s->addWithLabel("检查更新频率", applicationUpdaterFrequency);
    s->addSaveFunc([applicationUpdaterFrequency, s] {
        if (applicationUpdaterFrequency->getSelected() !=
            Settings::getInstance()->getString("ApplicationUpdaterFrequency")) {
            Settings::getInstance()->setString("ApplicationUpdaterFrequency",
                                               applicationUpdaterFrequency->getSelected());
            s->setNeedsSaving();
        }
    });
#endif

#if defined(APPLICATION_UPDATER)
#if defined(IS_PRERELEASE)
    // Add a dummy entry to indicate that this setting is always enabled when running a prerelease.
    auto applicationUpdaterPrereleases = std::make_shared<SwitchComponent>();
    applicationUpdaterPrereleases->setState(true);
    s->addWithLabel("INCLUDE PRERELEASES IN UPDATE CHECKS", applicationUpdaterPrereleases);
    applicationUpdaterPrereleases->setEnabled(false);
    applicationUpdaterPrereleases->setOpacity(DISABLED_OPACITY);
    applicationUpdaterPrereleases->getParent()
        ->getChild(applicationUpdaterPrereleases->getChildIndex() - 1)
        ->setOpacity(DISABLED_OPACITY);
#else
    // Whether to include prereleases when checking for application updates.
    auto applicationUpdaterPrereleases = std::make_shared<SwitchComponent>();
    applicationUpdaterPrereleases->setState(
        Settings::getInstance()->getBool("ApplicationUpdaterPrereleases"));
    s->addWithLabel("检查更新时包括预发布版", applicationUpdaterPrereleases);
    s->addSaveFunc([applicationUpdaterPrereleases, s] {
        if (applicationUpdaterPrereleases->getState() !=
            Settings::getInstance()->getBool("ApplicationUpdaterPrereleases")) {
            Settings::getInstance()->setBool("ApplicationUpdaterPrereleases",
                                             applicationUpdaterPrereleases->getState());
            s->setNeedsSaving();
        }
    });
#endif
#endif

#if defined(_WIN64)
    // Hide taskbar during the program session.
    auto hide_taskbar = std::make_shared<SwitchComponent>();
    hide_taskbar->setState(Settings::getInstance()->getBool("HideTaskbar"));
    s->addWithLabel("HIDE TASKBAR (REQUIRES RESTART)", hide_taskbar);
    s->addSaveFunc([hide_taskbar, s] {
        if (hide_taskbar->getState() != Settings::getInstance()->getBool("HideTaskbar")) {
            Settings::getInstance()->setBool("HideTaskbar", hide_taskbar->getState());
            s->setNeedsSaving();
        }
    });
#endif

    // Run ES in the background when a game has been launched.
    auto runInBackground = std::make_shared<SwitchComponent>();
    runInBackground->setState(Settings::getInstance()->getBool("RunInBackground"));
    s->addWithLabel("后台运行(当游戏加载后)", runInBackground);
    s->addSaveFunc([runInBackground, s] {
        if (runInBackground->getState() != Settings::getInstance()->getBool("RunInBackground")) {
            Settings::getInstance()->setBool("RunInBackground", runInBackground->getState());
            s->setNeedsSaving();
        }
    });

#if defined(VIDEO_HW_DECODING)
    // Whether to enable hardware decoding for the FFmpeg video player.
    auto videoHardwareDecoding = std::make_shared<SwitchComponent>();
    videoHardwareDecoding->setState(Settings::getInstance()->getBool("VideoHardwareDecoding"));
    s->addWithLabel("VIDEO HARDWARE DECODING (EXPERIMENTAL)", videoHardwareDecoding);
    s->addSaveFunc([videoHardwareDecoding, s] {
        if (videoHardwareDecoding->getState() !=
            Settings::getInstance()->getBool("VideoHardwareDecoding")) {
            Settings::getInstance()->setBool("VideoHardwareDecoding",
                                             videoHardwareDecoding->getState());
            s->setNeedsSaving();
        }
    });
#endif

    // Whether to upscale the video frame rate to 60 FPS.
    auto videoUpscaleFrameRate = std::make_shared<SwitchComponent>();
    videoUpscaleFrameRate->setState(Settings::getInstance()->getBool("VideoUpscaleFrameRate"));
    s->addWithLabel("高帧率60FPS", videoUpscaleFrameRate);
    s->addSaveFunc([videoUpscaleFrameRate, s] {
        if (videoUpscaleFrameRate->getState() !=
            Settings::getInstance()->getBool("VideoUpscaleFrameRate")) {
            Settings::getInstance()->setBool("VideoUpscaleFrameRate",
                                             videoUpscaleFrameRate->getState());
            s->setNeedsSaving();
        }
    });

    // Whether to enable alternative emulators per game (the option to disable this is intended
    // primarily for testing purposes).
    auto alternativeEmulatorPerGame = std::make_shared<SwitchComponent>();
    alternativeEmulatorPerGame->setState(
        Settings::getInstance()->getBool("AlternativeEmulatorPerGame"));
    s->addWithLabel("启用每个游戏可替代模拟器", alternativeEmulatorPerGame);
    s->addSaveFunc([alternativeEmulatorPerGame, s] {
        if (alternativeEmulatorPerGame->getState() !=
            Settings::getInstance()->getBool("AlternativeEmulatorPerGame")) {
            Settings::getInstance()->setBool("AlternativeEmulatorPerGame",
                                             alternativeEmulatorPerGame->getState());
            s->setNeedsSaving();
            s->setNeedsReloading();
            s->setInvalidateCachedBackground();
        }
    });

    // Show hidden files.
    auto showHiddenFiles = std::make_shared<SwitchComponent>();
    showHiddenFiles->setState(Settings::getInstance()->getBool("ShowHiddenFiles"));
    s->addWithLabel("显示隐藏文件和文件夹(需重启)", showHiddenFiles);
    s->addSaveFunc([showHiddenFiles, s] {
        if (showHiddenFiles->getState() != Settings::getInstance()->getBool("ShowHiddenFiles")) {
            Settings::getInstance()->setBool("ShowHiddenFiles", showHiddenFiles->getState());
            s->setNeedsSaving();
        }
    });

    // Show hidden games.
    auto showHiddenGames = std::make_shared<SwitchComponent>();
    showHiddenGames->setState(Settings::getInstance()->getBool("ShowHiddenGames"));
    s->addWithLabel("显示隐藏游戏(需重启)", showHiddenGames);
    s->addSaveFunc([showHiddenGames, s] {
        if (showHiddenGames->getState() != Settings::getInstance()->getBool("ShowHiddenGames")) {
            Settings::getInstance()->setBool("ShowHiddenGames", showHiddenGames->getState());
            s->setNeedsSaving();
        }
    });

    // Custom event scripts, fired using Scripting::fireEvent().
    auto customEventScripts = std::make_shared<SwitchComponent>();
    customEventScripts->setState(Settings::getInstance()->getBool("CustomEventScripts"));
    s->addWithLabel("启用自定义事件脚本", customEventScripts);
    s->addSaveFunc([customEventScripts, s] {
        if (customEventScripts->getState() !=
            Settings::getInstance()->getBool("CustomEventScripts")) {
            Settings::getInstance()->setBool("CustomEventScripts", customEventScripts->getState());
            s->setNeedsSaving();
        }
    });

    // Only show ROMs included in the gamelist.xml files.
    auto parseGamelistOnly = std::make_shared<SwitchComponent>();
    parseGamelistOnly->setState(Settings::getInstance()->getBool("ParseGamelistOnly"));
    s->addWithLabel("仅显示来自Gamelist.xml文件的ROM", parseGamelistOnly);
    s->addSaveFunc([parseGamelistOnly, s] {
        if (parseGamelistOnly->getState() !=
            Settings::getInstance()->getBool("ParseGamelistOnly")) {
            Settings::getInstance()->setBool("ParseGamelistOnly", parseGamelistOnly->getState());
            s->setNeedsSaving();
        }
    });

    // Strip extra MAME name info.
    auto mameNameStripExtraInfo = std::make_shared<SwitchComponent>();
    mameNameStripExtraInfo->setState(Settings::getInstance()->getBool("MAMENameStripExtraInfo"));
    s->addWithLabel("去除额外的MAME名称信息(需重启)", mameNameStripExtraInfo);
    s->addSaveFunc([mameNameStripExtraInfo, s] {
        if (Settings::getInstance()->getBool("MAMENameStripExtraInfo") !=
            mameNameStripExtraInfo->getState()) {
            Settings::getInstance()->setBool("MAMENameStripExtraInfo",
                                             mameNameStripExtraInfo->getState());
            s->setNeedsSaving();
        }
    });

#if defined(__unix__)
    // Whether to disable desktop composition.
    auto disableComposition = std::make_shared<SwitchComponent>();
    disableComposition->setState(Settings::getInstance()->getBool("DisableComposition"));
    s->addWithLabel("禁用桌面合成(需重启)", disableComposition);
    s->addSaveFunc([disableComposition, s] {
        if (disableComposition->getState() !=
            Settings::getInstance()->getBool("DisableComposition")) {
            Settings::getInstance()->setBool("DisableComposition", disableComposition->getState());
            s->setNeedsSaving();
        }
    });
#endif

    // GPU statistics overlay.
    auto displayGpuStatistics = std::make_shared<SwitchComponent>();
    displayGpuStatistics->setState(Settings::getInstance()->getBool("DisplayGPUStatistics"));
    s->addWithLabel("显示GPU统计信息覆盖", displayGpuStatistics);
    s->addSaveFunc([displayGpuStatistics, s] {
        if (displayGpuStatistics->getState() !=
            Settings::getInstance()->getBool("DisplayGPUStatistics")) {
            Settings::getInstance()->setBool("DisplayGPUStatistics",
                                             displayGpuStatistics->getState());
            s->setNeedsSaving();
        }
    });

    // Whether to enable the menu in Kid mode.
    auto enableMenuKidMode = std::make_shared<SwitchComponent>();
    enableMenuKidMode->setState(Settings::getInstance()->getBool("EnableMenuKidMode"));
    s->addWithLabel("在儿童模式下启用菜单", enableMenuKidMode);
    s->addSaveFunc([enableMenuKidMode, s] {
        if (Settings::getInstance()->getBool("EnableMenuKidMode") !=
            enableMenuKidMode->getState()) {
            Settings::getInstance()->setBool("EnableMenuKidMode", enableMenuKidMode->getState());
            s->setNeedsSaving();
        }
    });

// macOS requires root privileges to reboot and power off so it doesn't make much
// sense to enable this setting and menu entry for that operating system.
#if !defined(__APPLE__)
    // Whether to show the quit menu with the options to reboot and shutdown the computer.
    auto showQuitMenu = std::make_shared<SwitchComponent>();
    showQuitMenu->setState(Settings::getInstance()->getBool("ShowQuitMenu"));
    s->addWithLabel("显示退出菜单(重启和关机选项)", showQuitMenu);
    s->addSaveFunc([this, showQuitMenu, s] {
        if (showQuitMenu->getState() != Settings::getInstance()->getBool("ShowQuitMenu")) {
            Settings::getInstance()->setBool("ShowQuitMenu", showQuitMenu->getState());
            s->setNeedsSaving();
            GuiMenu::close(false);
        }
    });
#endif

#if defined(APPLICATION_UPDATER) && !defined(IS_PRERELEASE)
    auto applicationUpdaterFrequencyFunc =
        [applicationUpdaterPrereleases](const std::string& frequency) {
            if (frequency == "never") {
                applicationUpdaterPrereleases->setEnabled(false);
                applicationUpdaterPrereleases->setOpacity(DISABLED_OPACITY);
                applicationUpdaterPrereleases->getParent()
                    ->getChild(applicationUpdaterPrereleases->getChildIndex() - 1)
                    ->setOpacity(DISABLED_OPACITY);
            }
            else {
                applicationUpdaterPrereleases->setEnabled(true);
                applicationUpdaterPrereleases->setOpacity(1.0f);
                applicationUpdaterPrereleases->getParent()
                    ->getChild(applicationUpdaterPrereleases->getChildIndex() - 1)
                    ->setOpacity(1.0f);
            }
        };

    applicationUpdaterFrequencyFunc(applicationUpdaterFrequency->getSelected());
    applicationUpdaterFrequency->setCallback(applicationUpdaterFrequencyFunc);
#endif

    s->setSize(mSize);
    mWindow->pushGui(s);
}

void GuiMenu::openQuitMenu()
{
    if (!Settings::getInstance()->getBool("ShowQuitMenu")) {
        mWindow->pushGui(new GuiMsgBox(
            this->getHelpStyle(), "确定退出?", "确认",
            [this] {
                close(true);
                Utils::Platform::quitES();
            },
            "取消", nullptr));
    }
    else {
        auto s = new GuiSettings("退出");

        Window* window {mWindow};
        HelpStyle style {getHelpStyle()};

        ComponentListRow row;

        row.makeAcceptInputHandler([window, this] {
            window->pushGui(new GuiMsgBox(
                this->getHelpStyle(), "确定退出?", "确定",
                [this] {
                    close(true);
                    Utils::Platform::quitES();
                },
                "取消", nullptr));
        });
        auto quitText = std::make_shared<TextComponent>("退出EmulationStation",
                                                        Font::get(FONT_SIZE_MEDIUM), 0x777777FF);
        quitText->setSelectable(true);
        row.addElement(quitText, true);
        s->addRow(row);

        row.elements.clear();
        row.makeAcceptInputHandler([window, this] {
            window->pushGui(new GuiMsgBox(
                this->getHelpStyle(), "确定重启?", "确认",
                [] {
                    if (Utils::Platform::quitES(Utils::Platform::QuitMode::REBOOT) != 0) {
                        LOG(LogWarning) << "Reboot terminated with non-zero result!";
                    }
                },
                "取消", nullptr));
        });
        auto rebootText =
            std::make_shared<TextComponent>("重启系统", Font::get(FONT_SIZE_MEDIUM), 0x777777FF);
        rebootText->setSelectable(true);
        row.addElement(rebootText, true);
        s->addRow(row);

        row.elements.clear();
        row.makeAcceptInputHandler([window, this] {
            window->pushGui(new GuiMsgBox(
                this->getHelpStyle(), "确定关机?", "确定",
                [] {
                    if (Utils::Platform::quitES(Utils::Platform::QuitMode::POWEROFF) != 0) {
                        LOG(LogWarning) << "Power off terminated with non-zero result!";
                    }
                },
                "取消", nullptr));
        });
        auto powerOffText =
            std::make_shared<TextComponent>("关闭系统", Font::get(FONT_SIZE_MEDIUM), 0x777777FF);
        powerOffText->setSelectable(true);
        row.addElement(powerOffText, true);
        s->addRow(row);

        s->setSize(mSize);
        mWindow->pushGui(s);
    }
}

void GuiMenu::addVersionInfo()
{
    mVersion.setFont(Font::get(FONT_SIZE_SMALL));
    mVersion.setColor(0x5E5E5EFF);

#if defined(IS_PRERELEASE)
    mVersion.setText("EMULATIONSTATION-DE  V" + Utils::String::toUpper(PROGRAM_VERSION_STRING) +
                     " (Built " + __DATE__ + ")");
#else
    mVersion.setText("EMULATIONSTATION-DE  V" + Utils::String::toUpper(PROGRAM_VERSION_STRING));
#endif

    mVersion.setHorizontalAlignment(ALIGN_CENTER);
    addChild(&mVersion);
}

void GuiMenu::openThemeDownloader(GuiSettings* settings)
{
    mWindow->pushGui(new GuiThemeDownloader());
}

void GuiMenu::openMediaViewerOptions()
{
    mWindow->pushGui(new GuiMediaViewerOptions("媒体查看器设置"));
}

void GuiMenu::openScreensaverOptions() { mWindow->pushGui(new GuiScreensaverOptions("屏保设置")); }

void GuiMenu::openCollectionSystemOptions()
{
    mWindow->pushGui(new GuiCollectionSystemsOptions("游戏专辑设置"));
}

void GuiMenu::onSizeChanged()
{
    mVersion.setSize(mSize.x, 0.0f);
    mVersion.setPosition(0.0f, mSize.y - mVersion.getSize().y);
}

void GuiMenu::addEntry(const std::string& name,
                       unsigned int color,
                       bool add_arrow,
                       const std::function<void()>& func)
{
    std::shared_ptr<Font> font {Font::get(FONT_SIZE_MEDIUM)};

    // Populate the list.
    ComponentListRow row;
    row.addElement(std::make_shared<TextComponent>(name, font, color), true);

    if (add_arrow) {
        std::shared_ptr<ImageComponent> bracket {makeArrow()};
        row.addElement(bracket, false);
    }

    row.makeAcceptInputHandler(func);
    mMenu.addRow(row);
}

void GuiMenu::close(bool closeAllWindows)
{
    std::function<void()> closeFunc;
    if (!closeAllWindows) {
        closeFunc = [this] { delete this; };
    }
    else {
        Window* window {mWindow};
        closeFunc = [window] {
            while (window->peekGui() != ViewController::getInstance())
                delete window->peekGui();
        };
    }
    closeFunc();
}

bool GuiMenu::input(InputConfig* config, Input input)
{
    if (GuiComponent::input(config, input))
        return true;

    const bool isStart {config->isMappedTo("start", input)};
    if (input.value != 0 && (config->isMappedTo("b", input) || isStart)) {
        close(isStart);
        return true;
    }

    return false;
}

std::vector<HelpPrompt> GuiMenu::getHelpPrompts()
{
    std::vector<HelpPrompt> prompts;
    prompts.push_back(HelpPrompt("up/down", "选择"));
    prompts.push_back(HelpPrompt("a", "确定"));
    prompts.push_back(HelpPrompt("b", "返回"));
    prompts.push_back(HelpPrompt("start", "关闭菜单"));
    return prompts;
}
