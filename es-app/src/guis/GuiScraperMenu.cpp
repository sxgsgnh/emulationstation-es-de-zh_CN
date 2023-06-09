//  SPDX-License-Identifier: MIT
//
//  EmulationStation Desktop Edition
//  GuiScraperMenu.cpp
//
//  Game media scraper, including settings as well as the scraping start button.
//  Submenu to the GuiMenu main menu.
//  Will call GuiScraperMulti to perform the actual scraping.
//

#include "guis/GuiScraperMenu.h"

#include "FileData.h"
#include "FileSorts.h"
#include "SystemData.h"
#include "components/OptionListComponent.h"
#include "components/SliderComponent.h"
#include "components/SwitchComponent.h"
#include "guis/GuiMsgBox.h"
#include "guis/GuiOfflineGenerator.h"
#include "guis/GuiScraperMulti.h"

GuiScraperMenu::GuiScraperMenu(std::string title)
    : mMenu {title}
{
    // Scraper service.
    mScraper =
        std::make_shared<OptionListComponent<std::string>>(getHelpStyle(), "抓取从", false);
    std::vector<std::string> scrapers = getScraperList();
    // Select either the first entry or the one read from the settings,
    // just in case the scraper from settings has vanished.
    for (auto it = scrapers.cbegin(); it != scrapers.cend(); ++it)
        mScraper->add(*it, *it, *it == Settings::getInstance()->getString("Scraper"));
    // If there are no objects returned, then there must be a manually modified entry in the
    // configuration file. Simply set the scraper to "screenscraper" in this case.
    if (mScraper->getSelectedObjects().size() == 0)
        mScraper->selectEntry(0);

    mMenu.addWithLabel("抓取从", mScraper);

    // Search filters, getSearches() will generate a queue of games to scrape
    // based on the outcome of the checks below.
    mFilters = std::make_shared<OptionListComponent<GameFilterFunc>>(getHelpStyle(),
                                                                     "抓取这些游戏", false);
    mFilters->add(
        "全部游戏",
        [](SystemData*, FileData*) -> bool {
            // All games.
            return true;
        },
        false);
    mFilters->add(
        "收藏的游戏",
        [](SystemData*, FileData* g) -> bool {
            // Favorite games.
            return g->getFavorite();
        },
        false);
    mFilters->add(
        "无元数据",
        [](SystemData*, FileData* g) -> bool {
            // No metadata.
            return g->metadata.get("desc").empty();
        },
        false);
    mFilters->add(
        "无游戏图像",
        [](SystemData*, FileData* g) -> bool {
            // No game image.
            return g->getImagePath().empty();
        },
        false);
    mFilters->add(
        "无游戏视频",
        [](SystemData*, FileData* g) -> bool {
            // No game video.
            return g->getVideoPath().empty();
        },
        false);
    mFilters->add(
        "仅文件夹",
        [](SystemData*, FileData* g) -> bool {
            // Folders only.
            return g->getType() == FOLDER;
        },
        false);

    mFilters->selectEntry(Settings::getInstance()->getInt("ScraperFilter"));
    mMenu.addWithLabel("抓取这些游戏", mFilters);

    mMenu.addSaveFunc([this] {
        if (mScraper->getSelected() != Settings::getInstance()->getString("Scraper")) {
            Settings::getInstance()->setString("Scraper", mScraper->getSelected());
            mMenu.setNeedsSaving();
        }
        // The filter setting is only retained during the program session i.e. it's not saved
        // to es_settings.xml.
        if (mFilters->getSelectedId() !=
            static_cast<unsigned int>(Settings::getInstance()->getInt("ScraperFilter")))
            Settings::getInstance()->setInt("ScraperFilter", mFilters->getSelectedId());
    });

    // Add systems (all systems with an existing platform ID are listed).
    mSystems = std::make_shared<OptionListComponent<SystemData*>>(getHelpStyle(),
                                                                  "抓取这些系统", true);
    for (unsigned int i = 0; i < SystemData::sSystemVector.size(); ++i) {
        if (!SystemData::sSystemVector[i]->hasPlatformId(PlatformIds::PLATFORM_IGNORE)) {
            mSystems->add(SystemData::sSystemVector[i]->getFullName(), SystemData::sSystemVector[i],
                          !SystemData::sSystemVector[i]->getPlatformIds().empty());
            SystemData::sSystemVector[i]->getScrapeFlag() ? mSystems->selectEntry(i) :
                                                            mSystems->unselectEntry(i);
        }
    }
    mMenu.addWithLabel("抓取这些系统", mSystems);

    addEntry("账户设置", 0x777777FF, true, [this] {
        // Open the account options menu.
        openAccountOptions();
    });
    addEntry("内容设置", 0x777777FF, true, [this] {
        // If the scraper service has been changed before entering this menu, then save the
        // settings so that the specific options supported by the respective scrapers
        // can be enabled or disabled.
        if (mScraper->getSelected() != Settings::getInstance()->getString("Scraper"))
            mMenu.save();
        openContentOptions();
    });
    addEntry("混合图像设置", 0x777777FF, true, [this] {
        // Open the miximage options menu.
        openMiximageOptions();
    });
    addEntry("其他设置", 0x777777FF, true, [this] {
        // If the scraper service has been changed before entering this menu, then save the
        // settings so that the specific options supported by the respective scrapers
        // can be enabled or disabled.
        if (mScraper->getSelected() != Settings::getInstance()->getString("Scraper"))
            mMenu.save();
        openOtherOptions();
    });

    addChild(&mMenu);

    mMenu.addButton("确定", "开始抓取", std::bind(&GuiScraperMenu::pressedStart, this));
    mMenu.addButton("返回", "返回", [&] { delete this; });

    setSize(mMenu.getSize());

    setPosition((Renderer::getScreenWidth() - mSize.x) / 2.0f, Renderer::getScreenHeight() * 0.13f);
}

GuiScraperMenu::~GuiScraperMenu()
{
    // Save the scrape flags to the system settings so that they are
    // remembered throughout the program session.
    std::vector<SystemData*> sys {mSystems->getSelectedObjects()};
    for (auto it = SystemData::sSystemVector.cbegin(); // Line break.
         it != SystemData::sSystemVector.cend(); ++it) {
        (*it)->setScrapeFlag(false);
        for (auto it_sys = sys.cbegin(); it_sys != sys.cend(); ++it_sys) {
            if ((*it)->getFullName() == (*it_sys)->getFullName())
                (*it)->setScrapeFlag(true);
        }
    }
}

void GuiScraperMenu::openAccountOptions()
{
    auto s = new GuiSettings("账户设置");

    // ScreenScraper username.
    auto scraperUsernameScreenScraper =
        std::make_shared<TextComponent>("", Font::get(FONT_SIZE_MEDIUM), 0x777777FF, ALIGN_RIGHT);
    s->addEditableTextComponent("ScreenScraper用户名", scraperUsernameScreenScraper,
                                Settings::getInstance()->getString("ScraperUsernameScreenScraper"));
    s->addSaveFunc([scraperUsernameScreenScraper, s] {
        if (scraperUsernameScreenScraper->getValue() !=
            Settings::getInstance()->getString("ScraperUsernameScreenScraper")) {
            Settings::getInstance()->setString("ScraperUsernameScreenScraper",
                                               scraperUsernameScreenScraper->getValue());
            s->setNeedsSaving();
        }
    });

    // ScreenScraper password.
    auto scraperPasswordScreenScraper =
        std::make_shared<TextComponent>("", Font::get(FONT_SIZE_MEDIUM), 0x777777FF, ALIGN_RIGHT);
    std::string passwordMasked;
    if (Settings::getInstance()->getString("ScraperPasswordScreenScraper") != "") {
        passwordMasked = "********";
        scraperPasswordScreenScraper->setHiddenValue(
            Settings::getInstance()->getString("ScraperPasswordScreenScraper"));
    }
    s->addEditableTextComponent("ScreenScraper密码", scraperPasswordScreenScraper,
                                passwordMasked, "", true);
    s->addSaveFunc([scraperPasswordScreenScraper, s] {
        if (scraperPasswordScreenScraper->getHiddenValue() !=
            Settings::getInstance()->getString("ScraperPasswordScreenScraper")) {
            Settings::getInstance()->setString("ScraperPasswordScreenScraper",
                                               scraperPasswordScreenScraper->getHiddenValue());
            s->setNeedsSaving();
        }
    });

    // Whether to use the ScreenScraper account when scraping.
    auto scraperUseAccountScreenScraper = std::make_shared<SwitchComponent>();
    scraperUseAccountScreenScraper->setState(
        Settings::getInstance()->getBool("ScraperUseAccountScreenScraper"));
    s->addWithLabel("为ScreenScraper使用这个账户", scraperUseAccountScreenScraper);
    s->addSaveFunc([scraperUseAccountScreenScraper, s] {
        if (scraperUseAccountScreenScraper->getState() !=
            Settings::getInstance()->getBool("ScraperUseAccountScreenScraper")) {
            Settings::getInstance()->setBool("ScraperUseAccountScreenScraper",
                                             scraperUseAccountScreenScraper->getState());
            s->setNeedsSaving();
        }
    });

    mWindow->pushGui(s);
}

void GuiScraperMenu::openContentOptions()
{
    auto s = new GuiSettings("内容设置");

    // Scrape game names.
    auto scrapeGameNames = std::make_shared<SwitchComponent>();
    scrapeGameNames->setState(Settings::getInstance()->getBool("ScrapeGameNames"));
    s->addWithLabel("游戏名称", scrapeGameNames);
    s->addSaveFunc([scrapeGameNames, s] {
        if (scrapeGameNames->getState() != Settings::getInstance()->getBool("ScrapeGameNames")) {
            Settings::getInstance()->setBool("ScrapeGameNames", scrapeGameNames->getState());
            s->setNeedsSaving();
        }
    });

    // Scrape ratings.
    auto scrapeRatings = std::make_shared<SwitchComponent>();
    scrapeRatings->setState(Settings::getInstance()->getBool("ScrapeRatings"));
    s->addWithLabel("评级", scrapeRatings);
    s->addSaveFunc([scrapeRatings, s] {
        if (scrapeRatings->getState() != Settings::getInstance()->getBool("ScrapeRatings")) {
            Settings::getInstance()->setBool("ScrapeRatings", scrapeRatings->getState());
            s->setNeedsSaving();
        }
    });

    // Ratings are not supported by TheGamesDB, so gray out the option if this scraper is selected.
    if (Settings::getInstance()->getString("Scraper") == "thegamesdb") {
        scrapeRatings->setEnabled(false);
        scrapeRatings->setOpacity(DISABLED_OPACITY);
        scrapeRatings->getParent()
            ->getChild(scrapeRatings->getChildIndex() - 1)
            ->setOpacity(DISABLED_OPACITY);
    }

    // ScreenScraper controller scraping is currently broken, it's unclear if they will fix it.
    // // Scrape controllers (arcade systems only).
    // auto scrapeControllers = std::make_shared<SwitchComponent>();
    // scrapeControllers->setState(Settings::getInstance()->getBool("ScrapeControllers"));
    // s->addWithLabel("CONTROLLERS (ARCADE SYSTEMS ONLY)", scrapeControllers);
    // s->addSaveFunc([scrapeControllers, s] {
    //     if (scrapeControllers->getState() !=
    //         Settings::getInstance()->getBool("ScrapeControllers")) {
    //         Settings::getInstance()->setBool("ScrapeControllers", scrapeControllers->getState());
    //         s->setNeedsSaving();
    //     }
    // });

    // // Controllers are not supported by TheGamesDB, so gray out the option if this scraper is
    // // selected.
    // if (Settings::getInstance()->getString("Scraper") == "thegamesdb") {
    //     scrapeControllers->setEnabled(false);
    //     scrapeControllers->setOpacity(DISABLED_OPACITY);
    //     scrapeControllers->getParent()
    //         ->getChild(scrapeControllers->getChildIndex() - 1)
    //         ->setOpacity(DISABLED_OPACITY);
    // }

    // Scrape other metadata.
    auto scrapeMetadata = std::make_shared<SwitchComponent>();
    scrapeMetadata->setState(Settings::getInstance()->getBool("ScrapeMetadata"));
    s->addWithLabel("其他元数据", scrapeMetadata);
    s->addSaveFunc([scrapeMetadata, s] {
        if (scrapeMetadata->getState() != Settings::getInstance()->getBool("ScrapeMetadata")) {
            Settings::getInstance()->setBool("ScrapeMetadata", scrapeMetadata->getState());
            s->setNeedsSaving();
        }
    });

    // Scrape videos.
    auto scrapeVideos = std::make_shared<SwitchComponent>();
    scrapeVideos->setState(Settings::getInstance()->getBool("ScrapeVideos"));
    s->addWithLabel("视频", scrapeVideos);
    s->addSaveFunc([scrapeVideos, s] {
        if (scrapeVideos->getState() != Settings::getInstance()->getBool("ScrapeVideos")) {
            Settings::getInstance()->setBool("ScrapeVideos", scrapeVideos->getState());
            s->setNeedsSaving();
        }
    });

    // Videos are not supported by TheGamesDB, so gray out the option if this scraper is selected.
    if (Settings::getInstance()->getString("Scraper") == "thegamesdb") {
        scrapeVideos->setEnabled(false);
        scrapeVideos->setOpacity(DISABLED_OPACITY);
        scrapeVideos->getParent()
            ->getChild(scrapeVideos->getChildIndex() - 1)
            ->setOpacity(DISABLED_OPACITY);
    }

    // Scrape screenshots images.
    auto scrapeScreenshots = std::make_shared<SwitchComponent>();
    scrapeScreenshots->setState(Settings::getInstance()->getBool("ScrapeScreenshots"));
    s->addWithLabel("截屏图像", scrapeScreenshots);
    s->addSaveFunc([scrapeScreenshots, s] {
        if (scrapeScreenshots->getState() !=
            Settings::getInstance()->getBool("ScrapeScreenshots")) {
            Settings::getInstance()->setBool("ScrapeScreenshots", scrapeScreenshots->getState());
            s->setNeedsSaving();
        }
    });

    // Scrape title screen images.
    auto scrapeTitleScreens = std::make_shared<SwitchComponent>();
    scrapeTitleScreens->setState(Settings::getInstance()->getBool("ScrapeTitleScreens"));
    s->addWithLabel("标题屏幕图像", scrapeTitleScreens);
    s->addSaveFunc([scrapeTitleScreens, s] {
        if (scrapeTitleScreens->getState() !=
            Settings::getInstance()->getBool("ScrapeTitleScreens")) {
            Settings::getInstance()->setBool("ScrapeTitleScreens", scrapeTitleScreens->getState());
            s->setNeedsSaving();
        }
    });

    // Scrape box cover images.
    auto scrapeCovers = std::make_shared<SwitchComponent>();
    scrapeCovers->setState(Settings::getInstance()->getBool("ScrapeCovers"));
    s->addWithLabel("盒子封面图像", scrapeCovers);
    s->addSaveFunc([scrapeCovers, s] {
        if (scrapeCovers->getState() != Settings::getInstance()->getBool("ScrapeCovers")) {
            Settings::getInstance()->setBool("ScrapeCovers", scrapeCovers->getState());
            s->setNeedsSaving();
        }
    });

    // Scrape box back cover images.
    auto scrapeBackCovers = std::make_shared<SwitchComponent>();
    scrapeBackCovers->setState(Settings::getInstance()->getBool("ScrapeBackCovers"));
    s->addWithLabel("盒装封底图片", scrapeBackCovers);
    s->addSaveFunc([scrapeBackCovers, s] {
        if (scrapeBackCovers->getState() != Settings::getInstance()->getBool("ScrapeBackCovers")) {
            Settings::getInstance()->setBool("ScrapeBackCovers", scrapeBackCovers->getState());
            s->setNeedsSaving();
        }
    });

    // Scrape marquee images.
    auto scrapeMarquees = std::make_shared<SwitchComponent>();
    scrapeMarquees->setState(Settings::getInstance()->getBool("ScrapeMarquees"));
    s->addWithLabel("轮播图像", scrapeMarquees);
    s->addSaveFunc([scrapeMarquees, s] {
        if (scrapeMarquees->getState() != Settings::getInstance()->getBool("ScrapeMarquees")) {
            Settings::getInstance()->setBool("ScrapeMarquees", scrapeMarquees->getState());
            s->setNeedsSaving();
        }
    });

    // Scrape 3D box images.
    auto scrape3dBoxes = std::make_shared<SwitchComponent>();
    scrape3dBoxes->setState(Settings::getInstance()->getBool("Scrape3DBoxes"));
    s->addWithLabel("3D盒子图像", scrape3dBoxes);
    s->addSaveFunc([scrape3dBoxes, s] {
        if (scrape3dBoxes->getState() != Settings::getInstance()->getBool("Scrape3DBoxes")) {
            Settings::getInstance()->setBool("Scrape3DBoxes", scrape3dBoxes->getState());
            s->setNeedsSaving();
        }
    });

    // 3D box images are not supported by TheGamesDB, so gray out the option if this scraper
    // is selected.
    if (Settings::getInstance()->getString("Scraper") == "thegamesdb") {
        scrape3dBoxes->setEnabled(false);
        scrape3dBoxes->setOpacity(DISABLED_OPACITY);
        scrape3dBoxes->getParent()
            ->getChild(scrape3dBoxes->getChildIndex() - 1)
            ->setOpacity(DISABLED_OPACITY);
    }

    // Scrape physical media images.
    auto scrapePhysicalMedia = std::make_shared<SwitchComponent>();
    scrapePhysicalMedia->setState(Settings::getInstance()->getBool("ScrapePhysicalMedia"));
    s->addWithLabel("物理媒体图像", scrapePhysicalMedia);
    s->addSaveFunc([scrapePhysicalMedia, s] {
        if (scrapePhysicalMedia->getState() !=
            Settings::getInstance()->getBool("ScrapePhysicalMedia")) {
            Settings::getInstance()->setBool("ScrapePhysicalMedia",
                                             scrapePhysicalMedia->getState());
            s->setNeedsSaving();
        }
    });

    // Physical media images are not supported by TheGamesDB, so gray out the option if this
    // scraper is selected.
    if (Settings::getInstance()->getString("Scraper") == "thegamesdb") {
        scrapePhysicalMedia->setEnabled(false);
        scrapePhysicalMedia->setOpacity(DISABLED_OPACITY);
        scrapePhysicalMedia->getParent()
            ->getChild(scrapePhysicalMedia->getChildIndex() - 1)
            ->setOpacity(DISABLED_OPACITY);
    }

    // Scrape fan art images.
    auto scrapeFanArt = std::make_shared<SwitchComponent>();
    scrapeFanArt->setState(Settings::getInstance()->getBool("ScrapeFanArt"));
    s->addWithLabel("粉丝艺术图片", scrapeFanArt);
    s->addSaveFunc([scrapeFanArt, s] {
        if (scrapeFanArt->getState() != Settings::getInstance()->getBool("ScrapeFanArt")) {
            Settings::getInstance()->setBool("ScrapeFanArt", scrapeFanArt->getState());
            s->setNeedsSaving();
        }
    });

    mWindow->pushGui(s);
}

void GuiScraperMenu::openMiximageOptions()
{
    auto s = new GuiSettings("混合图像设置");

    // Miximage resolution.
    auto miximageResolution = std::make_shared<OptionListComponent<std::string>>(
        getHelpStyle(), "混合图像分辨率", false);
    std::string selectedResolution {Settings::getInstance()->getString("MiximageResolution")};
    miximageResolution->add("1280x960", "1280x960", selectedResolution == "1280x960");
    miximageResolution->add("1920x1440", "1920x1440", selectedResolution == "1920x1440");
    miximageResolution->add("640x480", "640x480", selectedResolution == "640x480");
    // If there are no objects returned, then there must be a manually modified entry in the
    // configuration file. Simply set the resolution to "1280x960" in this case.
    if (miximageResolution->getSelectedObjects().size() == 0)
        miximageResolution->selectEntry(0);
    s->addWithLabel("混合图像分辨率", miximageResolution);
    s->addSaveFunc([miximageResolution, s] {
        if (miximageResolution->getSelected() !=
            Settings::getInstance()->getString("MiximageResolution")) {
            Settings::getInstance()->setString("MiximageResolution",
                                               miximageResolution->getSelected());
            s->setNeedsSaving();
        }
    });

    // Screenshot scaling method.
    auto miximageScaling = std::make_shared<OptionListComponent<std::string>>(
        getHelpStyle(), "屏幕截图缩放", false);
    std::string selectedScaling {Settings::getInstance()->getString("MiximageScreenshotScaling")};
    miximageScaling->add("sharp", "sharp", selectedScaling == "sharp");
    miximageScaling->add("smooth", "smooth", selectedScaling == "smooth");
    // If there are no objects returned, then there must be a manually modified entry in the
    // configuration file. Simply set the scaling method to "sharp" in this case.
    if (miximageScaling->getSelectedObjects().size() == 0)
        miximageScaling->selectEntry(0);
    s->addWithLabel("屏幕截图缩放方法", miximageScaling);
    s->addSaveFunc([miximageScaling, s] {
        if (miximageScaling->getSelected() !=
            Settings::getInstance()->getString("MiximageScreenshotScaling")) {
            Settings::getInstance()->setString("MiximageScreenshotScaling",
                                               miximageScaling->getSelected());
            s->setNeedsSaving();
        }
    });

    // Box/cover size.
    auto miximageBoxSize =
        std::make_shared<OptionListComponent<std::string>>(getHelpStyle(), "盒子大小", false);
    std::string selectedBoxSize {Settings::getInstance()->getString("MiximageBoxSize")};
    miximageBoxSize->add("small", "small", selectedBoxSize == "small");
    miximageBoxSize->add("medium", "medium", selectedBoxSize == "medium");
    miximageBoxSize->add("large", "large", selectedBoxSize == "large");
    // If there are no objects returned, then there must be a manually modified entry in the
    // configuration file. Simply set the box size to "medium" in this case.
    if (miximageBoxSize->getSelectedObjects().size() == 0)
        miximageBoxSize->selectEntry(0);
    s->addWithLabel("盒子大小", miximageBoxSize);
    s->addSaveFunc([miximageBoxSize, s] {
        if (miximageBoxSize->getSelected() !=
            Settings::getInstance()->getString("MiximageBoxSize")) {
            Settings::getInstance()->setString("MiximageBoxSize", miximageBoxSize->getSelected());
            s->setNeedsSaving();
        }
    });

    // Physical media size.
    auto miximagePhysicalMediaSize = std::make_shared<OptionListComponent<std::string>>(
        getHelpStyle(), "物理媒体大小", false);
    std::string selectedPhysicalMediaSize {
        Settings::getInstance()->getString("MiximagePhysicalMediaSize")};
    miximagePhysicalMediaSize->add("small", "small", selectedPhysicalMediaSize == "small");
    miximagePhysicalMediaSize->add("medium", "medium", selectedPhysicalMediaSize == "medium");
    miximagePhysicalMediaSize->add("large", "large", selectedPhysicalMediaSize == "large");
    // If there are no objects returned, then there must be a manually modified entry in the
    // configuration file. Simply set the physical media size to "medium" in this case.
    if (miximagePhysicalMediaSize->getSelectedObjects().size() == 0)
        miximagePhysicalMediaSize->selectEntry(0);
    s->addWithLabel("物理媒体大小", miximagePhysicalMediaSize);
    s->addSaveFunc([miximagePhysicalMediaSize, s] {
        if (miximagePhysicalMediaSize->getSelected() !=
            Settings::getInstance()->getString("MiximagePhysicalMediaSize")) {
            Settings::getInstance()->setString("MiximagePhysicalMediaSize",
                                               miximagePhysicalMediaSize->getSelected());
            s->setNeedsSaving();
        }
    });

    // Whether to generate miximages when scraping.
    auto miximageGenerate = std::make_shared<SwitchComponent>();
    miximageGenerate->setState(Settings::getInstance()->getBool("MiximageGenerate"));
    s->addWithLabel("抓取时生成混合图像", miximageGenerate);
    s->addSaveFunc([miximageGenerate, s] {
        if (miximageGenerate->getState() != Settings::getInstance()->getBool("MiximageGenerate")) {
            Settings::getInstance()->setBool("MiximageGenerate", miximageGenerate->getState());
            s->setNeedsSaving();
        }
    });

    // Whether to overwrite miximages (both for the scraper and offline generator).
    auto miximageOverwrite = std::make_shared<SwitchComponent>();
    miximageOverwrite->setState(Settings::getInstance()->getBool("MiximageOverwrite"));
    s->addWithLabel("覆盖混合图像(抓取/离线生成器)", miximageOverwrite);
    s->addSaveFunc([miximageOverwrite, s] {
        if (miximageOverwrite->getState() !=
            Settings::getInstance()->getBool("MiximageOverwrite")) {
            Settings::getInstance()->setBool("MiximageOverwrite", miximageOverwrite->getState());
            s->setNeedsSaving();
        }
    });

    // Whether to remove letterboxes from the screenshots.
    auto miximageRemoveLetterboxes = std::make_shared<SwitchComponent>();
    miximageRemoveLetterboxes->setState(
        Settings::getInstance()->getBool("MiximageRemoveLetterboxes"));
    s->addWithLabel("从屏幕截图中删除LETTERBOXES", miximageRemoveLetterboxes);
    s->addSaveFunc([miximageRemoveLetterboxes, s] {
        if (miximageRemoveLetterboxes->getState() !=
            Settings::getInstance()->getBool("MiximageRemoveLetterboxes")) {
            Settings::getInstance()->setBool("MiximageRemoveLetterboxes",
                                             miximageRemoveLetterboxes->getState());
            s->setNeedsSaving();
        }
    });

    // Whether to remove pillarboxes from the screenshots.
    auto miximageRemovePillarboxes = std::make_shared<SwitchComponent>();
    miximageRemovePillarboxes->setState(
        Settings::getInstance()->getBool("MiximageRemovePillarboxes"));
    s->addWithLabel("从屏幕截图中删除LETTERBOXES", miximageRemovePillarboxes);
    s->addSaveFunc([miximageRemovePillarboxes, s] {
        if (miximageRemovePillarboxes->getState() !=
            Settings::getInstance()->getBool("MiximageRemovePillarboxes")) {
            Settings::getInstance()->setBool("MiximageRemovePillarboxes",
                                             miximageRemovePillarboxes->getState());
            s->setNeedsSaving();
        }
    });

    // Whether to rotate horizontally oriented boxes.
    auto miximageRotateBoxes = std::make_shared<SwitchComponent>();
    miximageRotateBoxes->setState(
        Settings::getInstance()->getBool("MiximageRotateHorizontalBoxes"));
    s->addWithLabel("旋转水平方向的盒子", miximageRotateBoxes);
    s->addSaveFunc([miximageRotateBoxes, s] {
        if (miximageRotateBoxes->getState() !=
            Settings::getInstance()->getBool("MiximageRotateHorizontalBoxes")) {
            Settings::getInstance()->setBool("MiximageRotateHorizontalBoxes",
                                             miximageRotateBoxes->getState());
            s->setNeedsSaving();
        }
    });

    // Whether to include marquee images.
    auto miximageIncludeMarquee = std::make_shared<SwitchComponent>();
    miximageIncludeMarquee->setState(Settings::getInstance()->getBool("MiximageIncludeMarquee"));
    s->addWithLabel("包括轮播图像", miximageIncludeMarquee);
    s->addSaveFunc([miximageIncludeMarquee, s] {
        if (miximageIncludeMarquee->getState() !=
            Settings::getInstance()->getBool("MiximageIncludeMarquee")) {
            Settings::getInstance()->setBool("MiximageIncludeMarquee",
                                             miximageIncludeMarquee->getState());
            s->setNeedsSaving();
        }
    });

    // Whether to include box images.
    auto miximageIncludeBox = std::make_shared<SwitchComponent>();
    miximageIncludeBox->setState(Settings::getInstance()->getBool("MiximageIncludeBox"));
    s->addWithLabel("包括盒子图像", miximageIncludeBox);
    s->addSaveFunc([miximageIncludeBox, s] {
        if (miximageIncludeBox->getState() !=
            Settings::getInstance()->getBool("MiximageIncludeBox")) {
            Settings::getInstance()->setBool("MiximageIncludeBox", miximageIncludeBox->getState());
            s->setNeedsSaving();
        }
    });

    // Whether to use cover image if there is no 3D box image.
    auto miximageCoverFallback = std::make_shared<SwitchComponent>();
    miximageCoverFallback->setState(Settings::getInstance()->getBool("MiximageCoverFallback"));
    s->addWithLabel("如果3D框丢失,请使用封面图片", miximageCoverFallback);
    s->addSaveFunc([miximageCoverFallback, s] {
        if (miximageCoverFallback->getState() !=
            Settings::getInstance()->getBool("MiximageCoverFallback")) {
            Settings::getInstance()->setBool("MiximageCoverFallback",
                                             miximageCoverFallback->getState());
            s->setNeedsSaving();
        }
    });

    // Whether to include physical media images.
    auto miximageIncludePhysicalMedia = std::make_shared<SwitchComponent>();
    miximageIncludePhysicalMedia->setState(
        Settings::getInstance()->getBool("MiximageIncludePhysicalMedia"));
    s->addWithLabel("包括物理媒体图像", miximageIncludePhysicalMedia);
    s->addSaveFunc([miximageIncludePhysicalMedia, s] {
        if (miximageIncludePhysicalMedia->getState() !=
            Settings::getInstance()->getBool("MiximageIncludePhysicalMedia")) {
            Settings::getInstance()->setBool("MiximageIncludePhysicalMedia",
                                             miximageIncludePhysicalMedia->getState());
            s->setNeedsSaving();
        }
    });

    // Miximage offline generator.
    ComponentListRow offlineGeneratorRow;
    offlineGeneratorRow.elements.clear();
    offlineGeneratorRow.addElement(std::make_shared<TextComponent>("OFFLINE GENERATOR",
                                                                   Font::get(FONT_SIZE_MEDIUM),
                                                                   0x777777FF),
                                   true);
    offlineGeneratorRow.addElement(makeArrow(), false);
    offlineGeneratorRow.makeAcceptInputHandler(
        std::bind(&GuiScraperMenu::openOfflineGenerator, this, s));
    s->addRow(offlineGeneratorRow);

    mWindow->pushGui(s);
}

void GuiScraperMenu::openOfflineGenerator(GuiSettings* settings)
{
    if (mSystems->getSelectedObjects().empty()) {
        mWindow->pushGui(new GuiMsgBox(getHelpStyle(),
                                       "离线生成器使用与抓取相同的系统选择,”\n"
                                       "因此请至少选择一个系统来为“生成图像\n"));
        return;
    }

    // Always save the settings before starting the generator, in case any of the
    // miximage settings were modified.
    settings->save();
    // Also unset the save flag so that a double saving does not take place when closing
    // the miximage options menu later on.
    settings->setNeedsSaving(false);

    // Build the queue of games to process.
    std::queue<FileData*> gameQueue;
    std::vector<SystemData*> systems {mSystems->getSelectedObjects()};

    for (auto sys = systems.cbegin(); sys != systems.cend(); ++sys) {
        std::vector<FileData*> games {(*sys)->getRootFolder()->getChildrenRecursive()};

        // Sort the games by "filename, ascending".
        std::stable_sort(games.begin(), games.end(), FileSorts::SortTypes.at(0).comparisonFunction);

        for (FileData* game : games)
            gameQueue.push(game);
    }

    mWindow->pushGui(new GuiOfflineGenerator(gameQueue));
}

void GuiScraperMenu::openOtherOptions()
{
    auto s = new GuiSettings("其他设置");

    // Scraper region.
    auto scraperRegion =
        std::make_shared<OptionListComponent<std::string>>(getHelpStyle(), "区域", false);
    std::string selectedScraperRegion {Settings::getInstance()->getString("ScraperRegion")};
    // clang-format off
    scraperRegion->add("Europe", "eu",  selectedScraperRegion == "eu");
    scraperRegion->add("Japan",  "jp",  selectedScraperRegion == "jp");
    scraperRegion->add("USA",    "us",  selectedScraperRegion == "us");
    scraperRegion->add("World",  "wor", selectedScraperRegion == "wor");
    // clang-format on
    // If there are no objects returned, then there must be a manually modified entry in the
    // configuration file. Simply set the region to "Europe" in this case.
    if (scraperRegion->getSelectedObjects().size() == 0)
        scraperRegion->selectEntry(0);
    s->addWithLabel("区域", scraperRegion);
    s->addSaveFunc([scraperRegion, s] {
        if (scraperRegion->getSelected() != Settings::getInstance()->getString("ScraperRegion")) {
            Settings::getInstance()->setString("ScraperRegion", scraperRegion->getSelected());
            s->setNeedsSaving();
        }
    });

    // Regions are not supported by TheGamesDB, so gray out the option if this scraper is selected.
    if (Settings::getInstance()->getString("Scraper") == "thegamesdb") {
        scraperRegion->setEnabled(false);
        scraperRegion->setOpacity(DISABLED_OPACITY);
        scraperRegion->getParent()
            ->getChild(scraperRegion->getChildIndex() - 1)
            ->setOpacity(DISABLED_OPACITY);
    }

    // Scraper language.
    auto scraperLanguage = std::make_shared<OptionListComponent<std::string>>(
        getHelpStyle(), "首选语言", false);
    std::string selectedScraperLanguage {Settings::getInstance()->getString("ScraperLanguage")};
    // clang-format off
    scraperLanguage->add("English",    "en", selectedScraperLanguage == "en");
    scraperLanguage->add("Español",    "es", selectedScraperLanguage == "es");
    scraperLanguage->add("Português",  "pt", selectedScraperLanguage == "pt");
    scraperLanguage->add("Français",   "fr", selectedScraperLanguage == "fr");
    scraperLanguage->add("Deutsch",    "de", selectedScraperLanguage == "de");
    scraperLanguage->add("Italiano",   "it", selectedScraperLanguage == "it");
    scraperLanguage->add("Nederlands", "nl", selectedScraperLanguage == "nl");
    scraperLanguage->add("日本語",      "ja", selectedScraperLanguage == "ja");
    scraperLanguage->add("简体中文",    "zh", selectedScraperLanguage == "zh");
    scraperLanguage->add("한국어",      "ko", selectedScraperLanguage == "ko");
    scraperLanguage->add("Русский",    "ru", selectedScraperLanguage == "ru");
    scraperLanguage->add("Dansk",      "da", selectedScraperLanguage == "da");
    scraperLanguage->add("Suomi",      "fi", selectedScraperLanguage == "fi");
    scraperLanguage->add("Svenska",    "sv", selectedScraperLanguage == "sv");
    scraperLanguage->add("Magyar",     "hu", selectedScraperLanguage == "hu");
    scraperLanguage->add("Norsk",      "no", selectedScraperLanguage == "no");
    scraperLanguage->add("Polski",     "pl", selectedScraperLanguage == "pl");
    scraperLanguage->add("Čeština",    "cz", selectedScraperLanguage == "cz");
    scraperLanguage->add("Slovenčina", "sk", selectedScraperLanguage == "sk");
    scraperLanguage->add("Türkçe",     "tr", selectedScraperLanguage == "tr");
    // clang-format on
    // If there are no objects returned, then there must be a manually modified entry in the
    // configuration file. Simply set the language to "English" in this case.
    if (scraperLanguage->getSelectedObjects().size() == 0)
        scraperLanguage->selectEntry(0);
    s->addWithLabel("首选语言", scraperLanguage);
    s->addSaveFunc([scraperLanguage, s] {
        if (scraperLanguage->getSelected() !=
            Settings::getInstance()->getString("ScraperLanguage")) {
            Settings::getInstance()->setString("ScraperLanguage", scraperLanguage->getSelected());
            s->setNeedsSaving();
        }
    });

    // Languages are not supported by TheGamesDB, so gray out the option if this scraper is
    // selected.
    if (Settings::getInstance()->getString("Scraper") == "thegamesdb") {
        scraperLanguage->setEnabled(false);
        scraperLanguage->setOpacity(DISABLED_OPACITY);
        scraperLanguage->getParent()
            ->getChild(scraperLanguage->getChildIndex() - 1)
            ->setOpacity(DISABLED_OPACITY);
    }

    // Automatic retries on error.
    auto scraperRetryOnErrorCount = std::make_shared<SliderComponent>(0.0f, 10.0f, 1.0f);
    scraperRetryOnErrorCount->setValue(
        static_cast<float>(Settings::getInstance()->getInt("ScraperRetryOnErrorCount")));
    s->addWithLabel("出错时自动重试", scraperRetryOnErrorCount);
    s->addSaveFunc([scraperRetryOnErrorCount, s] {
        if (scraperRetryOnErrorCount->getValue() !=
            static_cast<float>(Settings::getInstance()->getInt("ScraperRetryOnErrorCount"))) {
            Settings::getInstance()->setInt("ScraperRetryOnErrorCount",
                                            static_cast<int>(scraperRetryOnErrorCount->getValue()));
            s->setNeedsSaving();
        }
    });

    // Retry attempt timer.
    auto scraperRetryOnErrorTimer = std::make_shared<SliderComponent>(1.0f, 30.0f, 1.0f, "s");
    scraperRetryOnErrorTimer->setValue(
        static_cast<float>(Settings::getInstance()->getInt("ScraperRetryOnErrorTimer")));
    s->addWithLabel("重试计时器", scraperRetryOnErrorTimer);
    s->addSaveFunc([scraperRetryOnErrorTimer, s] {
        if (scraperRetryOnErrorTimer->getValue() !=
            static_cast<float>(Settings::getInstance()->getInt("ScraperRetryOnErrorTimer"))) {
            Settings::getInstance()->setInt("ScraperRetryOnErrorTimer",
                                            static_cast<int>(scraperRetryOnErrorTimer->getValue()));
            s->setNeedsSaving();
        }
    });

    if (scraperRetryOnErrorCount->getValue() == 0.0f) {
        scraperRetryOnErrorTimer->setEnabled(false);
        scraperRetryOnErrorTimer->setOpacity(DISABLED_OPACITY);
        scraperRetryOnErrorTimer->getParent()
            ->getChild(scraperRetryOnErrorTimer->getChildIndex() - 1)
            ->setOpacity(DISABLED_OPACITY);
    }

    // Overwrite files and data.
    auto scraperOverwriteData = std::make_shared<SwitchComponent>();
    scraperOverwriteData->setState(Settings::getInstance()->getBool("ScraperOverwriteData"));
    s->addWithLabel("覆盖文件和数据", scraperOverwriteData);
    s->addSaveFunc([scraperOverwriteData, s] {
        if (scraperOverwriteData->getState() !=
            Settings::getInstance()->getBool("ScraperOverwriteData")) {
            Settings::getInstance()->setBool("ScraperOverwriteData",
                                             scraperOverwriteData->getState());
            s->setNeedsSaving();
        }
    });

    // Halt scraping on invalid media files.
    auto scraperHaltOnInvalidMedia = std::make_shared<SwitchComponent>();
    scraperHaltOnInvalidMedia->setState(
        Settings::getInstance()->getBool("ScraperHaltOnInvalidMedia"));
    s->addWithLabel("暂停无效的媒体文件", scraperHaltOnInvalidMedia);
    s->addSaveFunc([scraperHaltOnInvalidMedia, s] {
        if (scraperHaltOnInvalidMedia->getState() !=
            Settings::getInstance()->getBool("ScraperHaltOnInvalidMedia")) {
            Settings::getInstance()->setBool("ScraperHaltOnInvalidMedia",
                                             scraperHaltOnInvalidMedia->getState());
            s->setNeedsSaving();
        }
    });

    // Search using metadata names.
    auto scraperSearchMetadataName = std::make_shared<SwitchComponent>();
    scraperSearchMetadataName->setState(
        Settings::getInstance()->getBool("ScraperSearchMetadataName"));
    s->addWithLabel("使用元数据名称搜索", scraperSearchMetadataName);
    s->addSaveFunc([scraperSearchMetadataName, s] {
        if (scraperSearchMetadataName->getState() !=
            Settings::getInstance()->getBool("ScraperSearchMetadataName")) {
            Settings::getInstance()->setBool("ScraperSearchMetadataName",
                                             scraperSearchMetadataName->getState());
            s->setNeedsSaving();
        }
    });

    // Include actual folders when scraping.
    auto scraperIncludeFolders = std::make_shared<SwitchComponent>();
    scraperIncludeFolders->setState(Settings::getInstance()->getBool("ScraperIncludeFolders"));
    s->addWithLabel("抓取实际文件夹", scraperIncludeFolders);
    s->addSaveFunc([scraperIncludeFolders, s] {
        if (scraperIncludeFolders->getState() !=
            Settings::getInstance()->getBool("ScraperIncludeFolders")) {
            Settings::getInstance()->setBool("ScraperIncludeFolders",
                                             scraperIncludeFolders->getState());
            s->setNeedsSaving();
        }
    });

    // Interactive scraping.
    auto scraperInteractive = std::make_shared<SwitchComponent>();
    scraperInteractive->setState(Settings::getInstance()->getBool("ScraperInteractive"));
    s->addWithLabel("交互模式", scraperInteractive);
    s->addSaveFunc([scraperInteractive, s] {
        if (scraperInteractive->getState() !=
            Settings::getInstance()->getBool("ScraperInteractive")) {
            Settings::getInstance()->setBool("ScraperInteractive", scraperInteractive->getState());
            s->setNeedsSaving();
        }
    });

    // Semi-automatic scraping.
    auto scraperSemiautomatic = std::make_shared<SwitchComponent>();
    scraperSemiautomatic->setState(Settings::getInstance()->getBool("ScraperSemiautomatic"));
    s->addWithLabel("自动接受单一游戏匹配", scraperSemiautomatic);
    s->addSaveFunc([scraperSemiautomatic, s] {
        if (scraperSemiautomatic->getState() !=
            Settings::getInstance()->getBool("ScraperSemiautomatic")) {
            Settings::getInstance()->setBool("ScraperSemiautomatic",
                                             scraperSemiautomatic->getState());
            s->setNeedsSaving();
        }
    });

    // If interactive mode is set to off, then gray out this option.
    if (!Settings::getInstance()->getBool("ScraperInteractive")) {
        scraperSemiautomatic->setEnabled(false);
        scraperSemiautomatic->setOpacity(DISABLED_OPACITY);
        scraperSemiautomatic->getParent()
            ->getChild(scraperSemiautomatic->getChildIndex() - 1)
            ->setOpacity(DISABLED_OPACITY);
    }

    // Respect the per-file multi-scraper exclusion flag.
    auto scraperRespectExclusions = std::make_shared<SwitchComponent>();
    scraperRespectExclusions->setState(
        Settings::getInstance()->getBool("ScraperRespectExclusions"));
    s->addWithLabel("尊重每个文件的Scraper排除", scraperRespectExclusions);
    s->addSaveFunc([scraperRespectExclusions, s] {
        if (scraperRespectExclusions->getState() !=
            Settings::getInstance()->getBool("ScraperRespectExclusions")) {
            Settings::getInstance()->setBool("ScraperRespectExclusions",
                                             scraperRespectExclusions->getState());
            s->setNeedsSaving();
        }
    });

    // Exclude files recursively for excluded folders.
    auto scraperExcludeRecursively = std::make_shared<SwitchComponent>();
    scraperExcludeRecursively->setState(
        Settings::getInstance()->getBool("ScraperExcludeRecursively"));
    s->addWithLabel("排除递归文件夹", scraperExcludeRecursively);
    s->addSaveFunc([scraperExcludeRecursively, s] {
        if (scraperExcludeRecursively->getState() !=
            Settings::getInstance()->getBool("ScraperExcludeRecursively")) {
            Settings::getInstance()->setBool("ScraperExcludeRecursively",
                                             scraperExcludeRecursively->getState());
            s->setNeedsSaving();
        }
    });

    // If respecting excluded files is set to off, then gray out this option.
    if (!Settings::getInstance()->getBool("ScraperRespectExclusions")) {
        scraperExcludeRecursively->setEnabled(false);
        scraperExcludeRecursively->setOpacity(DISABLED_OPACITY);
        scraperExcludeRecursively->getParent()
            ->getChild(scraperExcludeRecursively->getChildIndex() - 1)
            ->setOpacity(DISABLED_OPACITY);
    }

    // Convert underscores to spaces when searching.
    auto scraperConvertUnderscores = std::make_shared<SwitchComponent>();
    scraperConvertUnderscores->setState(
        Settings::getInstance()->getBool("ScraperConvertUnderscores"));
    s->addWithLabel("搜索时将下划线转换为空格", scraperConvertUnderscores);
    s->addSaveFunc([scraperConvertUnderscores, s] {
        if (scraperConvertUnderscores->getState() !=
            Settings::getInstance()->getBool("ScraperConvertUnderscores")) {
            Settings::getInstance()->setBool("ScraperConvertUnderscores",
                                             scraperConvertUnderscores->getState());
            s->setNeedsSaving();
        }
    });

    // Whether to remove dots from game names when searching using the automatic scraper.
    auto scraperAutomaticRemoveDots = std::make_shared<SwitchComponent>();
    scraperAutomaticRemoveDots->setState(
        Settings::getInstance()->getBool("ScraperAutomaticRemoveDots"));
    s->addWithLabel("自动抓取时从搜索中删除Dot", scraperAutomaticRemoveDots);
    s->addSaveFunc([scraperAutomaticRemoveDots, s] {
        if (scraperAutomaticRemoveDots->getState() !=
            Settings::getInstance()->getBool("ScraperAutomaticRemoveDots")) {
            Settings::getInstance()->setBool("ScraperAutomaticRemoveDots",
                                             scraperAutomaticRemoveDots->getState());
            s->setNeedsSaving();
        }
    });

    // This is not needed for TheGamesDB, so gray out the option if this scraper is selected.
    if (Settings::getInstance()->getString("Scraper") == "thegamesdb") {
        scraperAutomaticRemoveDots->setEnabled(false);
        scraperAutomaticRemoveDots->setOpacity(DISABLED_OPACITY);
        scraperAutomaticRemoveDots->getParent()
            ->getChild(scraperAutomaticRemoveDots->getChildIndex() - 1)
            ->setOpacity(DISABLED_OPACITY);
    }

    // Whether to fallback to additional regions.
    auto scraperRegionFallback = std::make_shared<SwitchComponent>(mWindow);
    scraperRegionFallback->setState(Settings::getInstance()->getBool("ScraperRegionFallback"));
    s->addWithLabel("启用回退到其他区域", scraperRegionFallback);
    s->addSaveFunc([scraperRegionFallback, s] {
        if (scraperRegionFallback->getState() !=
            Settings::getInstance()->getBool("ScraperRegionFallback")) {
            Settings::getInstance()->setBool("ScraperRegionFallback",
                                             scraperRegionFallback->getState());
            s->setNeedsSaving();
        }
    });

    // Regions are not supported by TheGamesDB, so gray out the option if this scraper is selected.
    if (Settings::getInstance()->getString("Scraper") == "thegamesdb") {
        scraperRegionFallback->setEnabled(false);
        scraperRegionFallback->setOpacity(DISABLED_OPACITY);
        scraperRegionFallback->getParent()
            ->getChild(scraperRegionFallback->getChildIndex() - 1)
            ->setOpacity(DISABLED_OPACITY);
    }

    // Slider callback.
    auto scraperRetryCountFunc = [scraperRetryOnErrorCount, scraperRetryOnErrorTimer]() {
        if (scraperRetryOnErrorCount->getValue() == 0.0f) {
            scraperRetryOnErrorTimer->setEnabled(false);
            scraperRetryOnErrorTimer->setOpacity(DISABLED_OPACITY);
            scraperRetryOnErrorTimer->getParent()
                ->getChild(scraperRetryOnErrorTimer->getChildIndex() - 1)
                ->setOpacity(DISABLED_OPACITY);
        }
        else {
            scraperRetryOnErrorTimer->setEnabled(true);
            scraperRetryOnErrorTimer->setOpacity(1.0f);
            scraperRetryOnErrorTimer->getParent()
                ->getChild(scraperRetryOnErrorTimer->getChildIndex() - 1)
                ->setOpacity(1.0f);
        }
    };

    // Switch callbacks.
    auto interactiveToggleFunc = [scraperSemiautomatic]() {
        if (scraperSemiautomatic->getEnabled()) {
            scraperSemiautomatic->setEnabled(false);
            scraperSemiautomatic->setOpacity(DISABLED_OPACITY);
            scraperSemiautomatic->getParent()
                ->getChild(scraperSemiautomatic->getChildIndex() - 1)
                ->setOpacity(DISABLED_OPACITY);
        }
        else {
            scraperSemiautomatic->setEnabled(true);
            scraperSemiautomatic->setOpacity(1.0f);
            scraperSemiautomatic->getParent()
                ->getChild(scraperSemiautomatic->getChildIndex() - 1)
                ->setOpacity(1.0f);
        }
    };

    auto excludeRecursivelyToggleFunc = [scraperExcludeRecursively]() {
        if (scraperExcludeRecursively->getEnabled()) {
            scraperExcludeRecursively->setEnabled(false);
            scraperExcludeRecursively->setOpacity(DISABLED_OPACITY);
            scraperExcludeRecursively->getParent()
                ->getChild(scraperExcludeRecursively->getChildIndex() - 1)
                ->setOpacity(DISABLED_OPACITY);
        }
        else {
            scraperExcludeRecursively->setEnabled(true);
            scraperExcludeRecursively->setOpacity(1.0f);
            scraperExcludeRecursively->getParent()
                ->getChild(scraperExcludeRecursively->getChildIndex() - 1)
                ->setOpacity(1.0f);
        }
    };

    scraperRetryOnErrorCount->setCallback(scraperRetryCountFunc);
    scraperInteractive->setCallback(interactiveToggleFunc);
    scraperRespectExclusions->setCallback(excludeRecursivelyToggleFunc);

    mWindow->pushGui(s);
}

void GuiScraperMenu::pressedStart()
{
    // If the scraper service has been changed, then save the settings as otherwise the
    // wrong scraper would be used.
    if (mScraper->getSelected() != Settings::getInstance()->getString("Scraper"))
        mMenu.save();

    std::vector<SystemData*> sys {mSystems->getSelectedObjects()};
    for (auto it = sys.cbegin(); it != sys.cend(); ++it) {
        if ((*it)->getPlatformIds().empty()) {
            std::string warningString;
            if (sys.size() == 1) {
                warningString = "The selected system does not have a\n"
                                "platform set, results may be inaccurate\n"
                                "Continue anyway?";
            }
            else {
                warningString = "At least one of your selected\n"
                                "systems does not have a platform\n"
                                "set, results may be inaccurate\n"
                                "Continue anyway?";
            }
            mWindow->pushGui(new GuiMsgBox(getHelpStyle(), Utils::String::toUpper(warningString),
                                           "确定", std::bind(&GuiScraperMenu::start, this), "取消",
                                           nullptr));
            return;
        }
    }
    start();
}

void GuiScraperMenu::start()
{
    if (mSystems->getSelectedObjects().empty()) {
        mWindow->pushGui(
            new GuiMsgBox(getHelpStyle(), "请选择至少一个系统进行抓取"));
        return;
    }

    bool contentToScrape {false};
    std::string scraperService {Settings::getInstance()->getString("Scraper")};

    // Check if there is actually any type of content selected for scraping.
    do {
        if (Settings::getInstance()->getBool("ScrapeGameNames")) {
            contentToScrape = true;
            break;
        }
        if (scraperService == "screenscraper" &&
            Settings::getInstance()->getBool("ScrapeRatings")) {
            contentToScrape = true;
            break;
        }
        // ScreenScraper controller scraping is currently broken, it's unclear if they will fix it.
        // if (scraperService == "screenscraper" &&
        //    Settings::getInstance()->getBool("ScrapeControllers")) {
        //    contentToScrape = true;
        //    break;
        // }
        if (Settings::getInstance()->getBool("ScrapeMetadata")) {
            contentToScrape = true;
            break;
        }
        if (scraperService == "screenscraper" && Settings::getInstance()->getBool("ScrapeVideos")) {
            contentToScrape = true;
            break;
        }
        if (Settings::getInstance()->getBool("ScrapeScreenshots")) {
            contentToScrape = true;
            break;
        }
        if (Settings::getInstance()->getBool("ScrapeTitleScreens")) {
            contentToScrape = true;
            break;
        }
        if (Settings::getInstance()->getBool("ScrapeCovers")) {
            contentToScrape = true;
            break;
        }
        if (Settings::getInstance()->getBool("ScrapeBackCovers")) {
            contentToScrape = true;
            break;
        }
        if (Settings::getInstance()->getBool("ScrapeFanArt")) {
            contentToScrape = true;
            break;
        }
        if (Settings::getInstance()->getBool("ScrapeMarquees")) {
            contentToScrape = true;
            break;
        }
        if (scraperService == "screenscraper" &&
            Settings::getInstance()->getBool("Scrape3DBoxes")) {
            contentToScrape = true;
            break;
        }
        if (scraperService == "screenscraper" &&
            Settings::getInstance()->getBool("ScrapePhysicalMedia")) {
            contentToScrape = true;
            break;
        }
    } while (0);

    if (!contentToScrape) {
        mWindow->pushGui(
            new GuiMsgBox(getHelpStyle(), "请选择至少一种要抓取的内容类型"));
        return;
    }

    std::queue<ScraperSearchParams> searches =
        getSearches(mSystems->getSelectedObjects(), mFilters->getSelected());

    if (searches.empty()) {
        mWindow->pushGui(
            new GuiMsgBox(getHelpStyle(), "所有游戏都经过过滤，没有什么可抓取的"));
    }
    else {
        GuiScraperMulti* gsm {
            new GuiScraperMulti(searches, Settings::getInstance()->getBool("ScraperInteractive"))};
        mWindow->pushGui(gsm);
        mMenu.setCursorToList();
        mMenu.setCursorToFirstListEntry();
    }
}

std::queue<ScraperSearchParams> GuiScraperMenu::getSearches(std::vector<SystemData*> systems,
                                                            GameFilterFunc selector)
{
    std::queue<ScraperSearchParams> queue;
    for (auto sys = systems.cbegin(); sys != systems.cend(); ++sys) {
        std::vector<FileData*> games {(*sys)->getRootFolder()->getScrapeFilesRecursive(
            Settings::getInstance()->getBool("ScraperIncludeFolders"),
            Settings::getInstance()->getBool("ScraperExcludeRecursively"),
            Settings::getInstance()->getBool("ScraperRespectExclusions"))};
        for (auto game = games.cbegin(); game != games.cend(); ++game) {
            if (selector((*sys), (*game))) {
                ScraperSearchParams search;
                search.game = *game;
                search.system = *sys;

                queue.push(search);
            }
        }
    }
    return queue;
}

void GuiScraperMenu::addEntry(const std::string& name,
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

bool GuiScraperMenu::input(InputConfig* config, Input input)
{
    if (GuiComponent::input(config, input))
        return true;

    if (config->isMappedTo("y", input) && input.value != 0)
        pressedStart();

    if (config->isMappedTo("b", input) && input.value != 0) {
        delete this;
        return true;
    }

    return false;
}

std::vector<HelpPrompt> GuiScraperMenu::getHelpPrompts()
{
    std::vector<HelpPrompt> prompts {mMenu.getHelpPrompts()};
    prompts.push_back(HelpPrompt("b", "返回"));
    prompts.push_back(HelpPrompt("y", "开始Scraper"));
    return prompts;
}
