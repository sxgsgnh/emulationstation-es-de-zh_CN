//  SPDX-License-Identifier: MIT
//
//  EmulationStation Desktop Edition
//  ViewController.cpp
//
//  Handles overall system navigation including animations and transitions.
//  Creates the gamelist views and handles refresh and reloads of these when needed
//  (for example when metadata has been changed or when a list sorting has taken place).
//  Initiates the launching of games, calling FileData to do the actual launch.
//  Displays a dialog when there are no games found on startup.
//

#include "views/ViewController.h"

#include "ApplicationUpdater.h"
#include "CollectionSystemsManager.h"
#include "FileFilterIndex.h"
#include "InputManager.h"
#include "Log.h"
#include "Scripting.h"
#include "Settings.h"
#include "Sound.h"
#include "SystemData.h"
#include "SystemView.h"
#include "UIModeController.h"
#include "Window.h"
#include "animations/Animation.h"
#include "animations/LambdaAnimation.h"
#include "animations/MoveCameraAnimation.h"
#include "guis/GuiMenu.h"
#include "guis/GuiTextEditKeyboardPopup.h"
#include "guis/GuiTextEditPopup.h"
#include "views/GamelistView.h"
#include "views/SystemView.h"

ViewController::ViewController() noexcept
    : mRenderer {Renderer::getInstance()}
    , mNoGamesMessageBox {nullptr}
    , mCurrentView {nullptr}
    , mPreviousView {nullptr}
    , mSkipView {nullptr}
    , mLastTransitionAnim {ViewTransitionAnimation::INSTANT}
    , mGameToLaunch {nullptr}
    , mCamera {Renderer::getIdentity()}
    , mSystemViewTransition {false}
    , mWrappedViews {false}
    , mFadeOpacity {0}
    , mCancelledTransition {false}
    , mLockInput {false}
    , mNextSystem {false}
{
    mState.viewing = NOTHING;
    mState.viewstyle = AUTOMATIC;
}

ViewController* ViewController::getInstance()
{
    static ViewController instance;
    return &instance;
}

void ViewController::invalidSystemsFileDialog()
{
    std::string errorMessage = "无法解析系统配置文件。"
                               "如果您有自定义的es_systems.xml文件,"
                               "那么您的XML语法可能有问题。"
                               "如果您没有自定义系统文件,则EMULATIONSTATION安装已损坏。"
                               "请参阅应用程序日志文件es_log.txt以获取更多信息";

    mWindow->pushGui(new GuiMsgBox(
        HelpStyle(), errorMessage.c_str(), "退出",
        [] {
            SDL_Event quit;
            quit.type = SDL_QUIT;
            SDL_PushEvent(&quit);
        },
        "", nullptr, "", nullptr, true, true,
        (mRenderer->getIsVerticalOrientation() ?
             0.85f :
             0.55f * (1.778f / mRenderer->getScreenAspectRatio()))));
}

void ViewController::noGamesDialog()
{
    mNoGamesErrorMessage = "没有找到游戏文件。要么将您的游戏放在当前配置的ROM目录中,"
                           "要么使用下面的按钮更改其路径。"
                           "可以选择生成ROM目录结构,它将为每个系统创建一个文本文件,"
                           "该文件提供一些信息,例如支持的文件扩展名。这是当前配置的ROM目录:";

#if defined(_WIN64)
    mRomDirectory = Utils::String::replace(FileData::getROMDirectory(), "/", "\\");
#else
    mRomDirectory = FileData::getROMDirectory();
#endif

    mNoGamesMessageBox = new GuiMsgBox(
        HelpStyle(), mNoGamesErrorMessage + mRomDirectory, "更改ROM目录",
        [this] {
            std::string currentROMDirectory;
#if defined(_WIN64)
            currentROMDirectory = Utils::String::replace(FileData::getROMDirectory(), "/", "\\");
#else
            currentROMDirectory = FileData::getROMDirectory();
#endif
            if (Settings::getInstance()->getBool("VirtualKeyboard")) {
                mWindow->pushGui(new GuiTextEditKeyboardPopup(
                    HelpStyle(), 0.0f, "输入ROM目录路径", currentROMDirectory,
                    [this](const std::string& newROMDirectory) {
                        Settings::getInstance()->setString("ROMDirectory",
                                                           Utils::String::trim(newROMDirectory));
                        Settings::getInstance()->saveFile();
#if defined(_WIN64)
                        mRomDirectory =
                            Utils::String::replace(FileData::getROMDirectory(), "/", "\\");
#else
                        mRomDirectory = FileData::getROMDirectory();
#endif
                        mNoGamesMessageBox->changeText(mNoGamesErrorMessage + mRomDirectory);
                        mWindow->pushGui(new GuiMsgBox(HelpStyle(),
                                                       "ROM目录设置已保存，重新启动应用程序"
                                                       "以重新扫描系统",
                                                       "确定", nullptr, "", nullptr, "", nullptr,
                                                       true, true));
                    },
                    false, "保存", "保存更改?", "当前配置的路径:",
                    currentROMDirectory, "加载当前配置路径",
                    "清空(留空以重置为默认路径)"));
            }
            else {
                mWindow->pushGui(new GuiTextEditPopup(
                    HelpStyle(), "输入ROM目录路径", currentROMDirectory,
                    [this](const std::string& newROMDirectory) {
                        Settings::getInstance()->setString("ROMDirectory",
                                                           Utils::String::trim(newROMDirectory));
                        Settings::getInstance()->saveFile();
#if defined(_WIN64)
                        mRomDirectory =
                            Utils::String::replace(FileData::getROMDirectory(), "/", "\\");
#else
                        mRomDirectory = FileData::getROMDirectory();
#endif
                        mNoGamesMessageBox->changeText(mNoGamesErrorMessage + mRomDirectory);
                        mWindow->pushGui(new GuiMsgBox(HelpStyle(),
                                                       "ROM目录设置已保存,重新启动\n"
                                                            "应用程序以重新扫描系统",
                                                       "OK", nullptr, "", nullptr, "", nullptr,
                                                       true));
                    },
                    false, "保存", "保存更改?", "Currently configured path:",
                    currentROMDirectory, "加载当前配置的路径",
                    "清空(留空以重置为默认路径)"));
            }
        },
        "创建目录",
        [this] {
            mWindow->pushGui(new GuiMsgBox(
                HelpStyle(),
                "这将为es_systems.xml中定义的所有游戏系统创建目录,"
                "这可能会创建大量文件夹,因此建议您删除不需要的文件夹？”\n",
                "确定",
                [this] {
                    if (!SystemData::createSystemDirectories()) {
                        mWindow->pushGui(new GuiMsgBox(HelpStyle(),
                                                       "系统目录已成功生成，退出应用程序\n"
                                                       "并将您的游戏放入新创建的文件夹中\n",
                                                       "确定", nullptr, "", nullptr, "", nullptr,
                                                       true));
                    }
                    else {
                        mWindow->pushGui(new GuiMsgBox(HelpStyle(),
                                                       "创建系统目录时出错、权限问题或磁盘已满？\n"
                                                            "查看日志文件了解更多详情",
                                                       "确定", nullptr, "", nullptr, "", nullptr,
                                                       true));
                    }
                },
                "取消", nullptr, "", nullptr, true));
        },
        "退出",
        [] {
            SDL_Event quit;
            quit.type = SDL_QUIT;
            SDL_PushEvent(&quit);
        },
        true, false,
        (mRenderer->getIsVerticalOrientation() ?
             0.90f :
             0.62f * (1.778f / mRenderer->getScreenAspectRatio())));

    mWindow->pushGui(mNoGamesMessageBox);
}

void ViewController::invalidAlternativeEmulatorDialog()
{
    cancelViewTransitions();
    mWindow->pushGui(new GuiMsgBox(getHelpStyle(),
                                   "您的至少一个系统配置了无效的替代仿真器，\n"
                                   "系统配置文件中没有匹配的条目，\n"
                                   "请使用“其他设置”菜单中的“替代仿真器”界面检查您的设置\n",
                                   "确定", nullptr, "", nullptr, "", nullptr, true, true));
}

void ViewController::updateAvailableDialog()
{
    cancelViewTransitions();

    std::string results {ApplicationUpdater::getInstance().getResultsString()};
    ApplicationUpdater::Package package {ApplicationUpdater::getInstance().getPackageInfo()};

    if (package.name != "") {
        LOG(LogDebug) << "ViewController::updateAvailableDialog(): Package filename \""
                      << package.filename << "\"";
        LOG(LogDebug) << "ViewController::updateAvailableDialog(): Package url \"" << package.url
                      << "\"";
        LOG(LogDebug) << "ViewController::updateAvailableDialog(): Package md5 \"" << package.md5
                      << "\"";
    }

    mWindow->pushGui(new GuiMsgBox(getHelpStyle(), results, "OK", nullptr, "", nullptr, "", nullptr,
                                   true, true,
                                   (mRenderer->getIsVerticalOrientation() ?
                                        0.70f :
                                        0.45f * (1.778f / mRenderer->getScreenAspectRatio()))));
}

void ViewController::goToStart(bool playTransition)
{
    // Needed to avoid segfaults during emergency shutdown.
    if (mRenderer->getSDLWindow() == nullptr)
        return;

    // If the system view does not exist, then create it. We do this here as it would
    // otherwise not be done if jumping directly into a specific game system on startup.
    if (!mSystemListView)
        getSystemListView();

    // If a specific system is requested, go directly to its game list.
    auto requestedSystem = Settings::getInstance()->getString("StartupSystem");
    if (requestedSystem != "") {
        for (auto it = SystemData::sSystemVector.cbegin(); // Line break.
             it != SystemData::sSystemVector.cend(); ++it) {
            if ((*it)->getName() == requestedSystem) {
                goToGamelist(*it);
                if (!playTransition)
                    cancelViewTransitions();
                return;
            }
        }

        // Requested system doesn't exist.
        Settings::getInstance()->setString("StartupSystem", "");
    }
    // Get the first system entry.
    goToSystemView(getSystemListView()->getFirstSystem(), false);
}

void ViewController::ReloadAndGoToStart()
{
    mWindow->renderSplashScreen(Window::SplashScreenState::RELOADING, 0.0f);
    reloadAll();
    if (mState.viewing == GAMELIST) {
        goToSystemView(SystemData::sSystemVector.front(), false);
        goToSystem(SystemData::sSystemVector.front(), false);
    }
    else {
        goToSystem(SystemData::sSystemVector.front(), false);
    }
}

bool ViewController::isCameraMoving()
{
    if (mCurrentView) {
        if (mCamera[3].x - -mCurrentView->getPosition().x != 0.0f ||
            mCamera[3].y - -mCurrentView->getPosition().y != 0.0f)
            return true;
    }
    return false;
}

void ViewController::cancelViewTransitions()
{
    if (mLastTransitionAnim == ViewTransitionAnimation::SLIDE) {
        if (isCameraMoving()) {
            mCamera[3].x = -mCurrentView->getPosition().x;
            mCamera[3].y = -mCurrentView->getPosition().y;
            stopAllAnimations();
        }
        // mSkipView is used when skipping through the gamelists in quick succession.
        // Without this, the game video (or static image) would not get rendered during
        // the slide transition animation.
        else if (mSkipView) {
            mSkipView.reset();
            mSkipView = nullptr;
        }
    }
    else if (mLastTransitionAnim == ViewTransitionAnimation::FADE) {
        if (isAnimationPlaying(0)) {
            finishAnimation(0);
            mCancelledTransition = true;
            mFadeOpacity = 0;
            mWindow->invalidateCachedBackground();
        }
    }
}

void ViewController::stopScrolling()
{
    if (mRenderer->getSDLWindow() == nullptr)
        return;

    mSystemListView->stopScrolling();
    mCurrentView->stopListScrolling();

    if (mSystemListView->isSystemAnimationPlaying(0))
        mSystemListView->finishSystemAnimation(0);
}

int ViewController::getSystemId(SystemData* system)
{
    std::vector<SystemData*>& sysVec {SystemData::sSystemVector};
    return static_cast<int>(std::find(sysVec.cbegin(), sysVec.cend(), system) - sysVec.cbegin());
}

void ViewController::restoreViewPosition()
{
    if (mPreviousView) {
        glm::vec3 restorePosition {mPreviousView->getPosition()};
        restorePosition.x = mWrapPreviousPositionX;
        mPreviousView->setPosition(restorePosition);
        mWrapPreviousPositionX = 0;
        mWrappedViews = false;
    }
}

void ViewController::goToSystemView(SystemData* system, bool playTransition)
{
    bool applicationStartup {false};

    if (mState.viewing == NOTHING)
        applicationStartup = true;

    // Restore the X position for the view, if it was previously moved.
    if (mWrappedViews)
        restoreViewPosition();

    if (mPreviousView) {
        mPreviousView.reset();
        mPreviousView = nullptr;
    }

    if (mCurrentView != nullptr)
        mCurrentView->onTransition();

    mPreviousView = mCurrentView;

    if (system->isGroupedCustomCollection())
        system = system->getRootFolder()->getParent()->getSystem();

    mState.viewing = SYSTEM_SELECT;
    mState.system = system;
    mSystemViewTransition = true;

    auto systemList = getSystemListView();
    systemList->setPosition(getSystemId(system) * Renderer::getScreenWidth(),
                            systemList->getPosition().y);

    systemList->goToSystem(system, false);
    mCurrentView = systemList;
    mCurrentView->onShow();

    // Application startup animation.
    if (applicationStartup) {
        const ViewTransitionAnimation transitionAnim {static_cast<ViewTransitionAnimation>(
            Settings::getInstance()->getInt("TransitionsStartupToSystem"))};

        mCamera = glm::translate(mCamera, glm::round(-mCurrentView->getPosition()));
        if (transitionAnim == ViewTransitionAnimation::SLIDE) {
            if (getSystemListView()->getPrimaryType() == SystemView::PrimaryType::CAROUSEL) {
                if (getSystemListView()->getCarouselType() ==
                        CarouselComponent<SystemData*>::CarouselType::HORIZONTAL ||
                    getSystemListView()->getCarouselType() ==
                        CarouselComponent<SystemData*>::CarouselType::HORIZONTAL_WHEEL)
                    mCamera[3].y += Renderer::getScreenHeight();
                else
                    mCamera[3].x -= Renderer::getScreenWidth();
            }
            else if (getSystemListView()->getPrimaryType() == SystemView::PrimaryType::TEXTLIST ||
                     getSystemListView()->getPrimaryType() == SystemView::PrimaryType::GRID) {
                mCamera[3].y += Renderer::getScreenHeight();
            }
            updateHelpPrompts();
        }
        else if (transitionAnim == ViewTransitionAnimation::FADE) {
            if (getSystemListView()->getPrimaryType() == SystemView::PrimaryType::CAROUSEL) {
                if (getSystemListView()->getCarouselType() ==
                        CarouselComponent<SystemData*>::CarouselType::HORIZONTAL ||
                    getSystemListView()->getCarouselType() ==
                        CarouselComponent<SystemData*>::CarouselType::HORIZONTAL_WHEEL)
                    mCamera[3].y += Renderer::getScreenHeight();
                else
                    mCamera[3].x += Renderer::getScreenWidth();
            }
            else if (getSystemListView()->getPrimaryType() == SystemView::PrimaryType::TEXTLIST ||
                     getSystemListView()->getPrimaryType() == SystemView::PrimaryType::GRID) {
                mCamera[3].y += Renderer::getScreenHeight();
            }
        }
        else {
            updateHelpPrompts();
        }
    }

    if (applicationStartup)
        playViewTransition(ViewTransition::STARTUP_TO_SYSTEM);
    else if (playTransition)
        playViewTransition(ViewTransition::GAMELIST_TO_SYSTEM);
    else
        playViewTransition(ViewTransition::GAMELIST_TO_SYSTEM, true);
}

void ViewController::goToSystem(SystemData* system, bool animate)
{
    mSystemListView->goToSystem(system, animate);
}

void ViewController::goToNextGamelist()
{
    assert(mState.viewing == GAMELIST);
    SystemData* system {getState().getSystem()};
    assert(system);
    NavigationSounds::getInstance().playThemeNavigationSound(QUICKSYSSELECTSOUND);
    mNextSystem = true;
    goToGamelist(system->getNext());
}

void ViewController::goToPrevGamelist()
{
    assert(mState.viewing == GAMELIST);
    SystemData* system {getState().getSystem()};
    assert(system);
    NavigationSounds::getInstance().playThemeNavigationSound(QUICKSYSSELECTSOUND);
    mNextSystem = false;
    goToGamelist(system->getPrev());
}

void ViewController::goToGamelist(SystemData* system)
{
    bool wrapFirstToLast {false};
    bool wrapLastToFirst {false};
    bool slideTransitions {false};
    bool fadeTransitions {false};

    if (mCurrentView != nullptr)
        mCurrentView->onTransition();

    ViewTransition transitionType;
    ViewTransitionAnimation transitionAnim;

    if (mState.viewing == SYSTEM_SELECT) {
        transitionType = ViewTransition::SYSTEM_TO_GAMELIST;
        transitionAnim = static_cast<ViewTransitionAnimation>(
            Settings::getInstance()->getInt("TransitionsSystemToGamelist"));
    }
    else if (mState.viewing == NOTHING) {
        transitionType = ViewTransition::STARTUP_TO_GAMELIST;
        transitionAnim = static_cast<ViewTransitionAnimation>(
            Settings::getInstance()->getInt("TransitionsStartupToGamelist"));
    }
    else {
        transitionType = ViewTransition::GAMELIST_TO_GAMELIST;
        transitionAnim = static_cast<ViewTransitionAnimation>(
            Settings::getInstance()->getInt("TransitionsGamelistToGamelist"));
    }

    if (transitionAnim == ViewTransitionAnimation::SLIDE)
        slideTransitions = true;

    if (transitionAnim == ViewTransitionAnimation::FADE)
        fadeTransitions = true;

    // Restore the X position for the view, if it was previously moved.
    if (mWrappedViews)
        restoreViewPosition();

    if (mPreviousView && fadeTransitions && isAnimationPlaying(0))
        mPreviousView->onHide();

    if (mPreviousView) {
        mSkipView = mPreviousView;
        mPreviousView.reset();
        mPreviousView = nullptr;
    }
    else if (!mPreviousView && mState.viewing == GAMELIST) {
        // This is needed as otherwise the static image would not get rendered during the
        // first Slide transition when coming from the System view.
        mSkipView = getGamelistView(system);
    }

    if (mState.viewing != SYSTEM_SELECT) {
        mPreviousView = mCurrentView;
        mSystemViewTransition = false;
    }
    else {
        mSystemViewTransition = true;
    }

    // Find if we're wrapping around the first and last systems, which requires the gamelist
    // to be moved in order to avoid weird camera movements. This is only needed for the
    // slide transition style.
    if (mState.viewing == GAMELIST && SystemData::sSystemVector.size() > 1 && slideTransitions) {
        if (SystemData::sSystemVector.front() == mState.getSystem()) {
            if (SystemData::sSystemVector.back() == system)
                wrapFirstToLast = true;
        }
        else if (SystemData::sSystemVector.back() == mState.getSystem()) {
            if (SystemData::sSystemVector.front() == system)
                wrapLastToFirst = true;
        }
    }

    // Stop any scrolling, animations and camera movements.
    if (mState.viewing == SYSTEM_SELECT) {
        mSystemListView->stopScrolling();
        if (mSystemListView->isSystemAnimationPlaying(0))
            mSystemListView->finishSystemAnimation(0);
    }

    if (slideTransitions ||
        (!fadeTransitions && mLastTransitionAnim == ViewTransitionAnimation::FADE))
        cancelViewTransitions();

    if (mState.viewing == SYSTEM_SELECT) {
        // Move the system list.
        auto sysList = getSystemListView();
        float offsetX {sysList->getPosition().x};
        int sysId {getSystemId(system)};

        sysList->setPosition(sysId * Renderer::getScreenWidth(), sysList->getPosition().y);
        offsetX = sysList->getPosition().x - offsetX;
        mCamera[3].x -= offsetX;
    }

    // If we are wrapping around, either from the first to last system, or the other way
    // around, we need to temporarily move the gamelist view location so that the camera
    // movements will be correct. This is accomplished by simply offsetting the X position
    // with the position of the first or last system plus the screen width.
    if (wrapFirstToLast) {
        glm::vec3 currentPosition {mCurrentView->getPosition()};
        mWrapPreviousPositionX = currentPosition.x;
        float offsetX {getGamelistView(system)->getPosition().x};
        // This is needed to move the camera in the correct direction if there are only two systems.
        if (SystemData::sSystemVector.size() == 2 && mNextSystem)
            offsetX -= Renderer::getScreenWidth();
        else
            offsetX += Renderer::getScreenWidth();
        currentPosition.x = offsetX;
        mCurrentView->setPosition(currentPosition);
        mCamera[3].x -= offsetX;
        mWrappedViews = true;
    }
    else if (wrapLastToFirst) {
        glm::vec3 currentPosition {mCurrentView->getPosition()};
        mWrapPreviousPositionX = currentPosition.x;
        float offsetX {getGamelistView(system)->getPosition().x};
        if (SystemData::sSystemVector.size() == 2 && !mNextSystem)
            offsetX += Renderer::getScreenWidth();
        else
            offsetX -= Renderer::getScreenWidth();
        currentPosition.x = offsetX;
        mCurrentView->setPosition(currentPosition);
        mCamera[3].x = -offsetX;
        mWrappedViews = true;
    }

    mCurrentView = getGamelistView(system);
    mCurrentView->finishAnimation(0);

    // Application startup animation, if starting in a gamelist rather than in the system view.
    if (mState.viewing == NOTHING) {
        if (mLastTransitionAnim == ViewTransitionAnimation::FADE)
            cancelViewTransitions();
        mCamera = glm::translate(mCamera, glm::round(-mCurrentView->getPosition()));
        if (transitionAnim == ViewTransitionAnimation::SLIDE) {
            mCamera[3].y -= Renderer::getScreenHeight();
            updateHelpPrompts();
        }
        else if (transitionAnim == ViewTransitionAnimation::FADE) {
            mCamera[3].y += Renderer::getScreenHeight() * 2.0f;
        }
        else {
            updateHelpPrompts();
        }
    }

    mState.viewing = GAMELIST;
    mState.system = system;

    if (system->getTheme()->isLegacyTheme()) {
        auto it = mGamelistViews.find(system);
        if (it != mGamelistViews.cend()) {
            std::string viewStyle {it->second->getName()};
            if (viewStyle == "basic")
                mState.viewstyle = BASIC;
            else if (viewStyle == "detailed")
                mState.viewstyle = DETAILED;
            else if (viewStyle == "video")
                mState.viewstyle = VIDEO;
        }
    }

    if (mCurrentView)
        mCurrentView->onShow();

    playViewTransition(transitionType);
}

void ViewController::playViewTransition(ViewTransition transitionType, bool instant)
{
    mCancelledTransition = false;

    glm::vec3 target {0.0f, 0.0f, 0.0f};
    if (mCurrentView)
        target = mCurrentView->getPosition();

    // No need to animate, we're not going anywhere (probably due to goToNextGamelist()
    // or goToPrevGamelist() being called when there's only 1 system).
    if (target == static_cast<glm::vec3>(-mCamera[3]) && !isAnimationPlaying(0))
        return;

    ViewTransitionAnimation transitionAnim {ViewTransitionAnimation::INSTANT};

    if (transitionType == ViewTransition::SYSTEM_TO_SYSTEM)
        transitionAnim = static_cast<ViewTransitionAnimation>(
            Settings::getInstance()->getInt("TransitionsSystemToSystem"));
    else if (transitionType == ViewTransition::SYSTEM_TO_GAMELIST)
        transitionAnim = static_cast<ViewTransitionAnimation>(
            Settings::getInstance()->getInt("TransitionsSystemToGamelist"));
    else if (transitionType == ViewTransition::GAMELIST_TO_GAMELIST)
        transitionAnim = static_cast<ViewTransitionAnimation>(
            Settings::getInstance()->getInt("TransitionsGamelistToGamelist"));
    else if (transitionType == ViewTransition::GAMELIST_TO_SYSTEM)
        transitionAnim = static_cast<ViewTransitionAnimation>(
            Settings::getInstance()->getInt("TransitionsGamelistToSystem"));
    else if (transitionType == ViewTransition::STARTUP_TO_SYSTEM)
        transitionAnim = static_cast<ViewTransitionAnimation>(
            Settings::getInstance()->getInt("TransitionsStartupToSystem"));
    else
        transitionAnim = static_cast<ViewTransitionAnimation>(
            Settings::getInstance()->getInt("TransitionsStartupToGamelist"));

    mLastTransitionAnim = transitionAnim;

    if (instant || transitionAnim == ViewTransitionAnimation::INSTANT) {
        setAnimation(new LambdaAnimation(
            [this, target](float /*t*/) {
                this->mCamera[3].x = -target.x;
                this->mCamera[3].y = -target.y;
                this->mCamera[3].z = -target.z;
                if (mPreviousView)
                    mPreviousView->onHide();
            },
            1));
        updateHelpPrompts();
    }
    else if (transitionAnim == ViewTransitionAnimation::FADE) {
        // Stop whatever's currently playing, leaving mFadeOpacity wherever it is.
        cancelAnimation(0);

        auto fadeFunc = [this](float t) {
            // The flag mCancelledTransition is required only when cancelViewTransitions()
            // cancels the animation, and it's only needed for the Fade transitions.
            // Without this, a (much shorter) fade transition would still play as
            // finishedCallback is calling this function.
            if (!mCancelledTransition)
                mFadeOpacity = glm::mix(0.0f, 1.0f, t);
        };

        auto fadeCallback = [this]() {
            if (mPreviousView)
                mPreviousView->onHide();
        };

        const static int FADE_DURATION {120}; // Fade in/out time.
        const static int FADE_WAIT {200}; // Time to wait between in/out.
        setAnimation(new LambdaAnimation(fadeFunc, FADE_DURATION), 0,
                     [this, fadeFunc, fadeCallback, target] {
                         this->mCamera[3].x = -target.x;
                         this->mCamera[3].y = -target.y;
                         this->mCamera[3].z = -target.z;
                         updateHelpPrompts();
                         setAnimation(new LambdaAnimation(fadeFunc, FADE_DURATION), FADE_WAIT,
                                      fadeCallback, true);
                     });

        // Fast-forward animation if we're partially faded.
        if (target == static_cast<glm::vec3>(-mCamera[3])) {
            // Not changing screens, so cancel the first half entirely.
            advanceAnimation(0, FADE_DURATION);
            advanceAnimation(0, FADE_WAIT);
            advanceAnimation(0, FADE_DURATION - static_cast<int>(mFadeOpacity * FADE_DURATION));
        }
        else {
            advanceAnimation(0, static_cast<int>(mFadeOpacity * FADE_DURATION));
        }
    }
    else if (transitionAnim == ViewTransitionAnimation::SLIDE) {
        auto slideCallback = [this]() {
            if (mSkipView) {
                mSkipView->onHide();
                mSkipView.reset();
                mSkipView = nullptr;
            }
            else if (mPreviousView) {
                mPreviousView->onHide();
            }
        };
        setAnimation(new MoveCameraAnimation(mCamera, target), 0, slideCallback);
        updateHelpPrompts(); // Update help prompts immediately.
    }
}

void ViewController::onFileChanged(FileData* file, bool reloadGamelist)
{
    auto it = mGamelistViews.find(file->getSystem());
    if (it != mGamelistViews.cend())
        it->second->onFileChanged(file, reloadGamelist);
}

void ViewController::launch(FileData* game)
{
    if (game->getType() != GAME) {
        LOG(LogError) << "Tried to launch something that isn't a game";
        return;
    }

    // Disable text scrolling and stop any Lottie animations. These will be enabled again in
    // FileData upon returning from the game.
    mWindow->setAllowTextScrolling(false);
    mWindow->setAllowFileAnimation(false);

    stopAnimation(1); // Make sure the fade in isn't still playing.
    mWindow->stopInfoPopup(); // Make sure we disable any existing info popup.

    int duration {0};
    std::string durationString {Settings::getInstance()->getString("LaunchScreenDuration")};

    if (durationString == "disabled") {
        // If the game launch screen has been set as disabled, show a simple info popup
        // notification instead.
        mWindow->queueInfoPopup(
            "加载游戏 '" + Utils::String::toUpper(game->metadata.get("showname") + "'"), 10000);
        duration = 1700;
    }
    else if (durationString == "brief") {
        duration = 1700;
    }
    else if (durationString == "long") {
        duration = 4500;
    }
    else {
        // Normal duration.
        duration = 3000;
    }

    if (durationString != "disabled")
        mWindow->displayLaunchScreen(game->getSourceFileData());

    NavigationSounds::getInstance().playThemeNavigationSound(LAUNCHSOUND);

    // This is just a dummy animation in order for the launch screen or notification popup
    // to be displayed briefly, and for the navigation sound playing to be able to complete.
    // During this time period, all user input is blocked.
    setAnimation(new LambdaAnimation([](float t) {}, duration), 0, [this, game] {
        game->launchGame();
        // If the launch screen is disabled then this will do nothing.
        mWindow->closeLaunchScreen();
        onFileChanged(game, true);
        // This is a workaround so that any keys or button presses used for exiting the emulator
        // are not captured upon returning.
        setAnimation(new LambdaAnimation([](float t) {}, 1), 0, [this] { mLockInput = false; });
    });
}

void ViewController::removeGamelistView(SystemData* system)
{
    auto exists = mGamelistViews.find(system);
    if (exists != mGamelistViews.cend()) {
        exists->second.reset();
        mGamelistViews.erase(system);
    }
}

std::shared_ptr<GamelistView> ViewController::getGamelistView(SystemData* system)
{
    // If we have already created an entry for this system, then return that one.
    auto exists = mGamelistViews.find(system);
    if (exists != mGamelistViews.cend())
        return exists->second;

    system->getIndex()->setKidModeFilters();
    // If there's no entry, then create it and return it.
    std::shared_ptr<GamelistView> view;

    if (system->getTheme()->isLegacyTheme()) {
        const bool themeHasVideoView {system->getTheme()->hasView("video")};

        // Decide which view style to use.
        GamelistViewStyle selectedViewStyle {AUTOMATIC};

        const std::string& viewPreference {Settings::getInstance()->getString("GamelistViewStyle")};
        if (viewPreference == "basic")
            selectedViewStyle = BASIC;
        else if (viewPreference == "detailed")
            selectedViewStyle = DETAILED;
        else if (viewPreference == "video")
            selectedViewStyle = VIDEO;

        if (selectedViewStyle == AUTOMATIC) {
            const std::vector<FileData*> files {
                system->getRootFolder()->getFilesRecursive(GAME | FOLDER)};

            for (auto it = files.cbegin(); it != files.cend(); ++it) {
                if (themeHasVideoView && !(*it)->getVideoPath().empty()) {
                    selectedViewStyle = VIDEO;
                    break;
                }
                else if (!(*it)->getImagePath().empty()) {
                    selectedViewStyle = DETAILED;
                    // Don't break out in case any subsequent files have videos.
                }
            }
        }
        // Create the view.
        switch (selectedViewStyle) {
            case VIDEO: {
                mState.viewstyle = VIDEO;
                break;
            }
            case DETAILED: {
                mState.viewstyle = DETAILED;
                break;
            }
            case BASIC: {
            }
            default: {
                if (!system->isGroupedCustomCollection())
                    mState.viewstyle = BASIC;
                break;
            }
        }
    }
    else if (Settings::getInstance()->getBool("ThemeVariantTriggers")) {
        const auto overrides = system->getTheme()->getCurrentThemeSetSelectedVariantOverrides();

        if (!overrides.empty()) {
            ThemeTriggers::TriggerType noVideosTriggerType {ThemeTriggers::TriggerType::NONE};
            ThemeTriggers::TriggerType noMediaTriggerType {ThemeTriggers::TriggerType::NONE};

            const std::vector<FileData*> files {
                system->getRootFolder()->getFilesRecursive(GAME | FOLDER)};

            if (overrides.find(ThemeTriggers::TriggerType::NO_VIDEOS) != overrides.end()) {
                noVideosTriggerType = ThemeTriggers::TriggerType::NO_VIDEOS;

                for (auto it = files.cbegin(); it != files.cend(); ++it) {
                    if (!(*it)->getVideoPath().empty()) {
                        noVideosTriggerType = ThemeTriggers::TriggerType::NONE;
                        break;
                    }
                }
            }

            if (overrides.find(ThemeTriggers::TriggerType::NO_MEDIA) != overrides.end()) {
                noMediaTriggerType = ThemeTriggers::TriggerType::NO_MEDIA;

                for (auto imageType : overrides.at(ThemeTriggers::TriggerType::NO_MEDIA).second) {
                    for (auto it = files.cbegin(); it != files.cend(); ++it) {
                        if (imageType == "miximage") {
                            if (!(*it)->getMiximagePath().empty()) {
                                noMediaTriggerType = ThemeTriggers::TriggerType::NONE;
                                goto BREAK;
                            }
                        }
                        else if (imageType == "marquee") {
                            if (!(*it)->getMarqueePath().empty()) {
                                noMediaTriggerType = ThemeTriggers::TriggerType::NONE;
                                goto BREAK;
                            }
                        }
                        else if (imageType == "screenshot") {
                            if (!(*it)->getScreenshotPath().empty()) {
                                noMediaTriggerType = ThemeTriggers::TriggerType::NONE;
                                goto BREAK;
                            }
                        }
                        else if (imageType == "titlescreen") {
                            if (!(*it)->getTitleScreenPath().empty()) {
                                noMediaTriggerType = ThemeTriggers::TriggerType::NONE;
                                goto BREAK;
                            }
                        }
                        else if (imageType == "cover") {
                            if (!(*it)->getCoverPath().empty()) {
                                noMediaTriggerType = ThemeTriggers::TriggerType::NONE;
                                goto BREAK;
                            }
                        }
                        else if (imageType == "backcover") {
                            if (!(*it)->getBackCoverPath().empty()) {
                                noMediaTriggerType = ThemeTriggers::TriggerType::NONE;
                                goto BREAK;
                            }
                        }
                        else if (imageType == "3dbox") {
                            if (!(*it)->get3DBoxPath().empty()) {
                                noMediaTriggerType = ThemeTriggers::TriggerType::NONE;
                                goto BREAK;
                            }
                        }
                        else if (imageType == "physicalmedia") {
                            if (!(*it)->getPhysicalMediaPath().empty()) {
                                noMediaTriggerType = ThemeTriggers::TriggerType::NONE;
                                goto BREAK;
                            }
                        }
                        else if (imageType == "fanart") {
                            if (!(*it)->getFanArtPath().empty()) {
                                noMediaTriggerType = ThemeTriggers::TriggerType::NONE;
                                goto BREAK;
                            }
                        }
                        else if (imageType == "video") {
                            if (!(*it)->getVideoPath().empty()) {
                                noMediaTriggerType = ThemeTriggers::TriggerType::NONE;
                                goto BREAK;
                            }
                        }
                    }
                }
            }
        BREAK:
            // noMedia takes precedence over the noVideos trigger.
            if (noMediaTriggerType == ThemeTriggers::TriggerType::NO_MEDIA)
                system->loadTheme(noMediaTriggerType);
            else
                system->loadTheme(noVideosTriggerType);
        }
    }

    view = std::shared_ptr<GamelistView>(new GamelistView(system->getRootFolder()));
    view->setTheme(system->getTheme());

    std::vector<SystemData*>& sysVec {SystemData::sSystemVector};
    int id {static_cast<int>(std::find(sysVec.cbegin(), sysVec.cend(), system) - sysVec.cbegin())};
    view->setPosition(id * Renderer::getScreenWidth(), Renderer::getScreenHeight() * 2.0f);

    addChild(view.get());

    mGamelistViews[system] = view;
    return view;
}

std::shared_ptr<SystemView> ViewController::getSystemListView()
{
    // If we have already created a system view entry, then return it.
    if (mSystemListView)
        return mSystemListView;

    mSystemListView = std::shared_ptr<SystemView>(new SystemView);
    addChild(mSystemListView.get());
    mSystemListView->setPosition(0, Renderer::getScreenHeight());
    return mSystemListView;
}

bool ViewController::input(InputConfig* config, Input input)
{
    if (mLockInput)
        return true;

    // If using the %RUNINBACKGROUND% variable in a launch command or if enabling the
    // RunInBackground setting, ES-DE will run in the background while a game is launched.
    // If we're in this state and then register some input, it means that the user is back in ES-DE.
    // Therefore unset the game launch flag and update all the GUI components. This will re-enable
    // the video player and scrolling of game names and game descriptions as well as letting the
    // screensaver start on schedule.
    if (mWindow->getGameLaunchedState()) {
        mWindow->setAllowTextScrolling(true);
        mWindow->setAllowFileAnimation(true);
        mWindow->setLaunchedGame(false);
        // Filter out the "a" button so the game is not restarted if there was such a button press
        // queued when leaving the game.
        if (config->isMappedTo("a", input) && input.value != 0)
            return true;
        // Trigger the game-end event.
        if (mGameEndEventParams.size() == 5) {
            Scripting::fireEvent(mGameEndEventParams[0], mGameEndEventParams[1],
                                 mGameEndEventParams[2], mGameEndEventParams[3],
                                 mGameEndEventParams[4]);
            mGameEndEventParams.clear();
        }
    }

    // Open the main menu.
    if (!(UIModeController::getInstance()->isUIModeKid() &&
          !Settings::getInstance()->getBool("EnableMenuKidMode")) &&
        config->isMappedTo("start", input) && input.value != 0 && mCurrentView != nullptr) {
        // If we don't stop the scrolling here, it will continue to
        // run after closing the menu.
        if (mSystemListView->isScrolling())
            mSystemListView->stopScrolling();
        // Finish the animation too, so that it doesn't continue
        // to play when we've closed the menu.
        if (mSystemListView->isSystemAnimationPlaying(0))
            mSystemListView->finishSystemAnimation(0);
        // Stop the gamelist scrolling as well as it would otherwise continue to run after
        // closing the menu.
        mCurrentView->stopListScrolling();
        // Pause all videos as they would otherwise continue to play beneath the menu.
        mCurrentView->pauseViewVideos();
        mCurrentView->stopGamelistFadeAnimations();

        mWindow->setAllowTextScrolling(false);
        mWindow->setAllowFileAnimation(false);

        // Finally, if the camera is currently moving, reset its position.
        cancelViewTransitions();

        mWindow->pushGui(new GuiMenu);
        return true;
    }

    if (!mWindow->isScreensaverActive()) {
        mWindow->setAllowTextScrolling(true);
        mWindow->setAllowFileAnimation(true);
    }

    // Check if UI mode has changed due to passphrase completion.
    if (UIModeController::getInstance()->listen(config, input))
        return true;

    if (mCurrentView)
        return mCurrentView->input(config, input);

    return false;
}

void ViewController::update(int deltaTime)
{
    if (mWindow->getChangedThemeSet())
        cancelViewTransitions();

    if (mCurrentView)
        mCurrentView->update(deltaTime);

    updateSelf(deltaTime);

    if (mGameToLaunch) {
        launch(mGameToLaunch);
        mGameToLaunch = nullptr;
    }
}

void ViewController::render(const glm::mat4& parentTrans)
{
    glm::mat4 trans {mCamera * parentTrans};
    glm::mat4 transInverse {glm::inverse(trans)};

    // Camera position, position + size.
    glm::vec3 viewStart {transInverse[3]};
    glm::vec3 viewEnd {std::fabs(trans[3].x) + Renderer::getScreenWidth(),
                       std::fabs(trans[3].y) + Renderer::getScreenHeight(), 0.0f};

    // Keep track of UI mode changes.
    UIModeController::getInstance()->monitorUIMode();

    // Render the system view if it's the currently displayed view, or if we're in the progress
    // of transitioning to or from this view.
    if (mSystemListView == mCurrentView || (mSystemViewTransition && isCameraMoving()))
        getSystemListView()->render(trans);

    // Draw the gamelists.
    for (auto it = mGamelistViews.cbegin(); it != mGamelistViews.cend(); ++it) {
        // Same thing as for the system view, limit the rendering only to what needs to be drawn.
        if (it->second == mCurrentView || (it->second == mPreviousView && isCameraMoving())) {
            // Clipping.
            glm::vec3 guiStart {it->second->getPosition()};
            glm::vec3 guiEnd {it->second->getPosition() +
                              glm::vec3 {it->second->getSize().x, it->second->getSize().y, 0.0f}};

            if (guiEnd.x >= viewStart.x && guiEnd.y >= viewStart.y && guiStart.x <= viewEnd.x &&
                guiStart.y <= viewEnd.y)
                it->second->render(trans);
        }
    }

    if (mWindow->peekGui() == this)
        mWindow->renderHelpPromptsEarly();

    // Fade out.
    if (mFadeOpacity) {
        unsigned int fadeColor {0x00000000 | static_cast<unsigned int>(mFadeOpacity * 255.0f)};
        mRenderer->setMatrix(parentTrans);
        mRenderer->drawRect(0.0f, 0.0f, Renderer::getScreenWidth(), Renderer::getScreenHeight(),
                            fadeColor, fadeColor);
    }
}

void ViewController::preload()
{
    unsigned int systemCount {static_cast<unsigned int>(SystemData::sSystemVector.size())};

    // This reduces the amount of texture pop-in when loading theme extras.
    if (!SystemData::sSystemVector.empty())
        getSystemListView();

    const bool splashScreen {Settings::getInstance()->getBool("SplashScreen")};
    float loadedSystems {0.0f};
    unsigned int lastTime {0};
    unsigned int accumulator {0};
    SDL_Event event {};

    for (auto it = SystemData::sSystemVector.cbegin(); // Line break.
         it != SystemData::sSystemVector.cend(); ++it) {
        // Poll events so that the OS doesn't think the application is hanging on startup,
        // this is required as the main application loop hasn't started yet.
        while (SDL_PollEvent(&event)) {
            InputManager::getInstance().parseEvent(event);
            if (event.type == SDL_QUIT) {
                SystemData::sStartupExitSignal = true;
                return;
            }
        };

        const std::string entryType {(*it)->isCustomCollection() ? "custom collection" : "system"};
        LOG(LogDebug) << "ViewController::preload(): Populating gamelist for " << entryType << " \""
                      << (*it)->getName() << "\"";
        if (splashScreen) {
            const unsigned int curTime {SDL_GetTicks()};
            accumulator += curTime - lastTime;
            lastTime = curTime;
            ++loadedSystems;
            // This prevents Renderer::swapBuffers() from being called excessively which
            // could lead to significantly longer application startup times.
            if (accumulator > 20) {
                accumulator = 0;
                const float progress {
                    glm::mix(0.5f, 1.0f, loadedSystems / static_cast<float>(systemCount))};
                mWindow->renderSplashScreen(Window::SplashScreenState::POPULATING, progress);
                lastTime += SDL_GetTicks() - curTime;
            }
        }
        (*it)->getIndex()->resetFilters();
        getGamelistView(*it)->preloadGamelist();
    }

    if (splashScreen && SystemData::sSystemVector.size() > 0)
        Window::getInstance()->renderSplashScreen(Window::SplashScreenState::POPULATING, 1.0f);

    // Short delay so that the full progress bar is always visible before proceeding.
    SDL_Delay(100);

    if (SystemData::sSystemVector.size() > 0)
        ThemeData::setThemeTransitions();

    // Load navigation sounds, either from the theme if it supports it, or otherwise from
    // the bundled fallback sound files.
    bool themeSoundSupport {false};
    for (auto system : SystemData::sSystemVector) {
        if (!themeSoundSupport && system->getTheme()->hasView("all")) {
            NavigationSounds::getInstance().loadThemeNavigationSounds(system->getTheme().get());
            themeSoundSupport = true;
        }
        if (system->getRootFolder()->getName() == "recent") {
            CollectionSystemsManager::getInstance()->trimCollectionCount(system->getRootFolder(),
                                                                         LAST_PLAYED_MAX);
        }
    }
    if (!SystemData::sSystemVector.empty() && !themeSoundSupport)
        NavigationSounds::getInstance().loadThemeNavigationSounds(nullptr);
}

void ViewController::reloadGamelistView(GamelistView* view, bool reloadTheme)
{
    for (auto it = mGamelistViews.cbegin(); it != mGamelistViews.cend(); ++it) {
        if (it->second.get() == view) {
            bool isCurrent {mCurrentView == it->second};
            SystemData* system {it->first};
            FileData* cursor {view->getCursor()};

            // Retain the cursor history for the view.
            std::vector<FileData*> cursorHistoryTemp;
            it->second->copyCursorHistory(cursorHistoryTemp);

            mGamelistViews.erase(it);

            if (isCurrent)
                mCurrentView = nullptr;

            if (reloadTheme)
                system->loadTheme(ThemeTriggers::TriggerType::NONE);
            system->getIndex()->setKidModeFilters();
            std::shared_ptr<GamelistView> newView {getGamelistView(system)};

            // Make sure we don't attempt to set the cursor to a nonexistent entry.
            auto children = system->getRootFolder()->getChildrenRecursive();
            if (std::find(children.cbegin(), children.cend(), cursor) != children.cend())
                newView->setCursor(cursor);

            if (isCurrent)
                mCurrentView = newView;

            newView->populateCursorHistory(cursorHistoryTemp);
            // This is required to get the game count updated if the favorite metadata value has
            // been changed for any game that is part of a custom collection.
            if (system->isCollection() && system->getName() == "collections") {
                std::pair<unsigned int, unsigned int> gameCount {0, 0};
                system->getRootFolder()->countGames(gameCount);
            }
            updateHelpPrompts();
            break;
        }
    }

    // If using the %RUNINBACKGROUND% variable in a launch command or if enabling the
    // RunInBackground setting, ES-DE will run in the background while a game is launched.
    // If this flag has been set, then update all the GUI components. This will block the
    // video player, prevent scrolling of game names and game descriptions and prevent the
    // screensaver from starting on schedule.
    if (mWindow->getGameLaunchedState())
        mWindow->setLaunchedGame(true);

    // Redisplay the current view.
    if (mCurrentView)
        mCurrentView->onShow();
}

void ViewController::reloadAll()
{
    if (mRenderer->getSDLWindow() == nullptr)
        return;

    cancelViewTransitions();

    // Clear all GamelistViews.
    std::map<SystemData*, FileData*> cursorMap;
    for (auto it = mGamelistViews.cbegin(); it != mGamelistViews.cend(); ++it) {
        if (std::find(SystemData::sSystemVector.cbegin(), SystemData::sSystemVector.cend(),
                      (*it).first) != SystemData::sSystemVector.cend())
            cursorMap[it->first] = it->second->getCursor();
    }

    mGamelistViews.clear();
    mCurrentView = nullptr;

    // Load themes, create GamelistViews and reset filters.
    for (auto it = cursorMap.cbegin(); it != cursorMap.cend(); ++it) {
        it->first->loadTheme(ThemeTriggers::TriggerType::NONE);
        it->first->getIndex()->resetFilters();
    }

    ThemeData::setThemeTransitions();

    // Rebuild SystemListView.
    mSystemListView.reset();
    getSystemListView();

    // Restore cursor positions for all systems.
    for (auto it = cursorMap.cbegin(); it != cursorMap.cend(); ++it) {
        const std::string entryType {(*it).first->isCustomCollection() ? "custom collection" :
                                                                         "system"};
        LOG(LogDebug) << "ViewController::reloadAll(): Populating gamelist for " << entryType
                      << " \"" << (*it).first->getName() << "\"";
        getGamelistView(it->first)->setCursor(it->second);
    }

    // Update mCurrentView since the pointers changed.
    if (mState.viewing == GAMELIST) {
        mCurrentView = getGamelistView(mState.getSystem());
    }
    else if (mState.viewing == SYSTEM_SELECT) {
        SystemData* system {mState.getSystem()};
        mSystemListView->goToSystem(system, false);
        mCurrentView = mSystemListView;
        mCamera[3].x = 0.0f;
    }
    else {
        goToSystemView(SystemData::sSystemVector.front(), false);
    }

    // Load navigation sounds, either from the theme if it supports it, or otherwise from
    // the bundled fallback sound files.
    NavigationSounds::getInstance().deinit();
    bool themeSoundSupport {false};
    for (SystemData* system : SystemData::sSystemVector) {
        if (system->getTheme()->hasView("all")) {
            NavigationSounds::getInstance().loadThemeNavigationSounds(system->getTheme().get());
            themeSoundSupport = true;
            break;
        }
    }
    if (!SystemData::sSystemVector.empty() && !themeSoundSupport)
        NavigationSounds::getInstance().loadThemeNavigationSounds(nullptr);

    ThemeData::themeLoadedLogOutput();

    mCurrentView->onShow();
    updateHelpPrompts();
}

std::vector<HelpPrompt> ViewController::getHelpPrompts()
{
    std::vector<HelpPrompt> prompts;
    if (!mCurrentView)
        return prompts;

    prompts = mCurrentView->getHelpPrompts();
    if (!(UIModeController::getInstance()->isUIModeKid() &&
          !Settings::getInstance()->getBool("EnableMenuKidMode")))
        prompts.push_back(HelpPrompt("start", "菜单"));
    return prompts;
}

HelpStyle ViewController::getHelpStyle()
{
    if (!mCurrentView)
        return GuiComponent::getHelpStyle();

    return mCurrentView->getHelpStyle();
}

HelpStyle ViewController::getViewHelpStyle()
{
    if (mState.getSystem()->getTheme()->isLegacyTheme()) {
        // For backward compatibility with legacy theme sets, read the helpsystem theme config
        // from the system view entry.
        return getSystemListView()->getHelpStyle();
    }
    else {
        if (mState.viewing == ViewMode::GAMELIST)
            return getGamelistView(mState.getSystem())->getHelpStyle();
        else
            return getSystemListView()->getHelpStyle();
    }
}
