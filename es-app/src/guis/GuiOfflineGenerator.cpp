//  SPDX-License-Identifier: MIT
//
//  EmulationStation Desktop Edition
//  GuiOfflineGenerator.cpp
//
//  User interface for the miximage offline generator.
//  Calls MiximageGenerator to do the actual work.
//

#include "guis/GuiOfflineGenerator.h"

#include "SystemData.h"
#include "components/MenuComponent.h"

GuiOfflineGenerator::GuiOfflineGenerator(const std::queue<FileData*>& gameQueue)
    : mGameQueue {gameQueue}
    , mRenderer {Renderer::getInstance()}
    , mBackground {":/graphics/frame.svg"}
    , mGrid {glm::ivec2 {6, 13}}
{
    addChild(&mBackground);
    addChild(&mGrid);

    mProcessing = false;
    mPaused = false;
    mOverwriting = false;

    mTotalGames = static_cast<int>(mGameQueue.size());
    mGamesProcessed = 0;
    mImagesGenerated = 0;
    mImagesOverwritten = 0;
    mGamesSkipped = 0;
    mGamesFailed = 0;

    mGame = nullptr;

    // Header.
    mTitle = std::make_shared<TextComponent>("混合图像离线生成器",
                                             Font::get(FONT_SIZE_LARGE), 0x555555FF, ALIGN_CENTER);
    mGrid.setEntry(mTitle, glm::ivec2 {0, 0}, false, true, glm::ivec2 {6, 1});

    mStatus = std::make_shared<TextComponent>("没有开始", Font::get(FONT_SIZE_MEDIUM),
                                              0x777777FF, ALIGN_CENTER);
    mGrid.setEntry(mStatus, glm::ivec2 {0, 1}, false, true, glm::ivec2 {6, 1});

    mGameCounter = std::make_shared<TextComponent>(
        std::to_string(mGamesProcessed) + " OF " + std::to_string(mTotalGames) +
            (mTotalGames == 1 ? " 游戏 " : " 游戏 ") + "已处理",
        Font::get(FONT_SIZE_SMALL), 0x888888FF, ALIGN_CENTER);
    mGrid.setEntry(mGameCounter, glm::ivec2 {0, 2}, false, true, glm::ivec2 {6, 1});

    // Spacer row with top border.
    mGrid.setEntry(std::make_shared<GuiComponent>(), glm::ivec2 {0, 3}, false, false,
                   glm::ivec2 {6, 1}, GridFlags::BORDER_TOP);

    // Left spacer.
    mGrid.setEntry(std::make_shared<GuiComponent>(), glm::ivec2 {0, 4}, false, false,
                   glm::ivec2 {1, 7});

    // Generated label.
    mGeneratedLbl = std::make_shared<TextComponent>("生成的:", Font::get(FONT_SIZE_SMALL),
                                                    0x888888FF, ALIGN_LEFT);
    mGrid.setEntry(mGeneratedLbl, glm::ivec2 {1, 4}, false, true, glm::ivec2 {1, 1});

    // Generated value/counter.
    mGeneratedVal = std::make_shared<TextComponent>(
        std::to_string(mGamesProcessed), Font::get(FONT_SIZE_SMALL), 0x888888FF, ALIGN_LEFT);
    mGrid.setEntry(mGeneratedVal, glm::ivec2 {2, 4}, false, true, glm::ivec2 {1, 1});

    // Overwritten label.
    mOverwrittenLbl = std::make_shared<TextComponent>("覆盖:", Font::get(FONT_SIZE_SMALL),
                                                      0x888888FF, ALIGN_LEFT);
    mGrid.setEntry(mOverwrittenLbl, glm::ivec2 {1, 5}, false, true, glm::ivec2 {1, 1});

    // Overwritten value/counter.
    mOverwrittenVal = std::make_shared<TextComponent>(
        std::to_string(mImagesOverwritten), Font::get(FONT_SIZE_SMALL), 0x888888FF, ALIGN_LEFT);
    mGrid.setEntry(mOverwrittenVal, glm::ivec2 {2, 5}, false, true, glm::ivec2 {1, 1});

    // Skipping label.
    const std::string skipLabel {mRenderer->getIsVerticalOrientation() ? "跳过:" :
                                                                         "跳过(已存在):"};
    mSkippedLbl = std::make_shared<TextComponent>(skipLabel, Font::get(FONT_SIZE_SMALL), 0x888888FF,
                                                  ALIGN_LEFT);
    mGrid.setEntry(mSkippedLbl, glm::ivec2 {1, 6}, false, true, glm::ivec2 {1, 1});

    // Skipping value/counter.
    mSkippedVal = std::make_shared<TextComponent>(
        std::to_string(mGamesSkipped), Font::get(FONT_SIZE_SMALL), 0x888888FF, ALIGN_LEFT);
    mGrid.setEntry(mSkippedVal, glm::ivec2 {2, 6}, false, true, glm::ivec2 {1, 1});

    // Failed label.
    mFailedLbl = std::make_shared<TextComponent>("失败:", Font::get(FONT_SIZE_SMALL), 0x888888FF,
                                                 ALIGN_LEFT);
    mGrid.setEntry(mFailedLbl, glm::ivec2 {1, 7}, false, true, glm::ivec2 {1, 1});

    // Failed value/counter.
    mFailedVal = std::make_shared<TextComponent>(
        std::to_string(mGamesFailed), Font::get(FONT_SIZE_SMALL), 0x888888FF, ALIGN_LEFT);
    mGrid.setEntry(mFailedVal, glm::ivec2 {2, 7}, false, true, glm::ivec2 {1, 1});

    // Processing label.
    mProcessingLbl = std::make_shared<TextComponent>("处理中: ", Font::get(FONT_SIZE_SMALL),
                                                     0x888888FF, ALIGN_LEFT);
    mGrid.setEntry(mProcessingLbl, glm::ivec2 {3, 4}, false, true, glm::ivec2 {1, 1});

    // Processing value.
    mProcessingVal =
        std::make_shared<TextComponent>("", Font::get(FONT_SIZE_SMALL), 0x888888FF, ALIGN_LEFT);
    mGrid.setEntry(mProcessingVal, glm::ivec2 {4, 4}, false, true, glm::ivec2 {1, 1});

    // Spacer row.
    mGrid.setEntry(std::make_shared<GuiComponent>(), glm::ivec2 {1, 8}, false, false,
                   glm::ivec2 {4, 1});

    // Last error message label.
    mLastErrorLbl = std::make_shared<TextComponent>(
        "最后的错误消息:", Font::get(FONT_SIZE_SMALL), 0x888888FF, ALIGN_LEFT);
    mGrid.setEntry(mLastErrorLbl, glm::ivec2 {1, 9}, false, true, glm::ivec2 {4, 1});

    // Last error message value.
    mLastErrorVal =
        std::make_shared<TextComponent>("", Font::get(FONT_SIZE_SMALL), 0x888888FF, ALIGN_LEFT);
    mGrid.setEntry(mLastErrorVal, glm::ivec2 {1, 10}, false, true, glm::ivec2 {4, 1});

    // Right spacer.
    mGrid.setEntry(std::make_shared<GuiComponent>(), glm::ivec2 {5, 4}, false, false,
                   glm::ivec2 {1, 7});

    // Spacer row with bottom border.
    mGrid.setEntry(std::make_shared<GuiComponent>(), glm::ivec2 {0, 11}, false, false,
                   glm::ivec2 {6, 1}, GridFlags::BORDER_BOTTOM);

    // Buttons.
    std::vector<std::shared_ptr<ButtonComponent>> buttons;

    mStartPauseButton = std::make_shared<ButtonComponent>("开始", "开始处理", [this]() {
        if (!mProcessing) {
            mProcessing = true;
            mPaused = false;
            mStartPauseButton->setText("暂停", "暂停处理");
            mCloseButton->setText("关闭", "关闭(中止处理)");
            mStatus->setText("运行中...");
            if (mGamesProcessed == 0) {
                LOG(LogInfo) << "GuiOfflineGenerator: Processing " << mTotalGames << " games";
            }
        }
        else {
            if (mMiximageGeneratorThread.joinable())
                mMiximageGeneratorThread.join();
            mPaused = true;
            update(1);
            mProcessing = false;
            this->mStartPauseButton->setText("开始", "开始处理");
            this->mCloseButton->setText("关闭", "关闭(中止处理)");
            mStatus->setText("暂停");
        }
    });

    buttons.push_back(mStartPauseButton);

    mCloseButton = std::make_shared<ButtonComponent>("关闭", "关闭", [this]() {
        if (mGamesProcessed != 0 && mGamesProcessed != mTotalGames) {
            LOG(LogInfo) << "GuiOfflineGenerator: Aborted after processing " << mGamesProcessed
                         << (mGamesProcessed == 1 ? " game (" : " games (") << mImagesGenerated
                         << (mImagesGenerated == 1 ? " image " : " images ") << "generated, "
                         << mGamesSkipped << (mGamesSkipped == 1 ? " game " : " games ")
                         << "skipped, " << mGamesFailed
                         << (mGamesFailed == 1 ? " game " : " games ") << "failed)";
        }
        delete this;
    });

    buttons.push_back(mCloseButton);
    mButtonGrid = makeButtonGrid(buttons);

    mGrid.setEntry(mButtonGrid, glm::ivec2 {0, 12}, true, false, glm::ivec2 {6, 1});

    // For narrower displays (e.g. in 4:3 ratio), allow the window to fill 95% of the screen
    // width rather than the 85% allowed for wider displays.
    float width {mRenderer->getScreenWidth() *
                 ((mRenderer->getScreenAspectRatio() < 1.4f) ? 0.95f : 0.85f)};

    setSize(width,
            mRenderer->getScreenHeight() * (mRenderer->getIsVerticalOrientation() ? 0.52f : 0.75f));
    setPosition((mRenderer->getScreenWidth() - mSize.x) / 2.0f,
                (mRenderer->getScreenHeight() - mSize.y) / 2.0f);
}

GuiOfflineGenerator::~GuiOfflineGenerator()
{
    // Let the miximage generator thread complete.
    if (mMiximageGeneratorThread.joinable())
        mMiximageGeneratorThread.join();

    mMiximageGenerator.reset();

    if (mImagesGenerated > 0)
        ViewController::getInstance()->reloadAll();
}

void GuiOfflineGenerator::onSizeChanged()
{
    mBackground.fitTo(mSize, glm::vec3 {}, glm::vec2 {-32.0f, -32.0f});

    // Set row heights.
    mGrid.setRowHeightPerc(0, mTitle->getFont()->getLetterHeight() * 1.9725f / mSize.y, false);
    mGrid.setRowHeightPerc(1, (mStatus->getFont()->getLetterHeight() + 2.0f) / mSize.y, false);
    mGrid.setRowHeightPerc(2, mGameCounter->getFont()->getHeight() * 1.75f / mSize.y, false);
    mGrid.setRowHeightPerc(3, (mStatus->getFont()->getLetterHeight() + 3.0f) / mSize.y, false);
    mGrid.setRowHeightPerc(4, 0.07f, false);
    mGrid.setRowHeightPerc(5, 0.07f, false);
    mGrid.setRowHeightPerc(6, 0.07f, false);
    mGrid.setRowHeightPerc(7, 0.07f, false);
    mGrid.setRowHeightPerc(8, 0.02f, false);
    mGrid.setRowHeightPerc(9, 0.07f, false);
    mGrid.setRowHeightPerc(10, 0.07f, false);
    mGrid.setRowHeightPerc(12, mButtonGrid->getSize().y / mSize.y, false);

    // Set column widths.
    mGrid.setColWidthPerc(0, 0.03f);
    mGrid.setColWidthPerc(1, 0.20f);
    mGrid.setColWidthPerc(2, 0.145f);
    mGrid.setColWidthPerc(5, 0.03f);

    // Adjust the width slightly depending on the aspect ratio of the screen to make sure
    // that the label does not get abbreviated.
    if (mRenderer->getIsVerticalOrientation())
        mGrid.setColWidthPerc(3, 0.17f);
    else if (mRenderer->getScreenAspectRatio() <= 1.4f)
        mGrid.setColWidthPerc(3, 0.14f);
    else if (mRenderer->getScreenAspectRatio() <= 1.6f)
        mGrid.setColWidthPerc(3, 0.12f);
    else
        mGrid.setColWidthPerc(3, 0.113f);

    mGrid.setSize(mSize);
}

void GuiOfflineGenerator::update(int deltaTime)
{
    if (!mProcessing)
        return;

    // Check if a miximage generator thread was started, and if the processing has been completed.
    if (mMiximageGenerator && mGeneratorFuture.valid()) {
        // Only wait one millisecond as this update() function runs very frequently.
        if (mGeneratorFuture.wait_for(std::chrono::milliseconds(1)) == std::future_status::ready) {
            // We always let the miximage generator thread complete.
            if (mMiximageGeneratorThread.joinable())
                mMiximageGeneratorThread.join();
            mMiximageGenerator.reset();
            if (!mGeneratorFuture.get()) {
                ++mImagesGenerated;
                TextureResource::manualUnload(mGame->getMiximagePath(), false);
                mProcessingVal->setText("");
                if (mOverwriting) {
                    ++mImagesOverwritten;
                    mOverwriting = false;
                }
            }
            else {
                std::string errorMessage {mResultMessage + " (" + mGameName + ")"};
                mLastErrorVal->setText(errorMessage);
                LOG(LogInfo) << "GuiOfflineGenerator: " << errorMessage;
                ++mGamesFailed;
            }
            mGame = nullptr;
            ++mGamesProcessed;
        }
    }

    // This is simply to retain the name of the last processed game on-screen while paused.
    if (mPaused)
        mProcessingVal->setText(mGameName);

    if (!mPaused && !mGameQueue.empty() && !mMiximageGenerator) {
        mGame = mGameQueue.front();
        mGameQueue.pop();

        mGameName =
            mGame->getName() + " [" + Utils::String::toUpper(mGame->getSystem()->getName()) + "]";
        mProcessingVal->setText(mGameName);

        if (!Settings::getInstance()->getBool("MiximageOverwrite") &&
            mGame->getMiximagePath() != "") {
            ++mGamesProcessed;
            ++mGamesSkipped;
            mSkippedVal->setText(std::to_string(mGamesSkipped));
        }
        else {
            if (mGame->getMiximagePath() != "")
                mOverwriting = true;

            mMiximageGenerator = std::make_unique<MiximageGenerator>(mGame, mResultMessage);

            // The promise/future mechanism is used as signaling for the thread to indicate
            // that processing has been completed.
            std::promise<bool>().swap(mGeneratorPromise);
            mGeneratorFuture = mGeneratorPromise.get_future();

            mMiximageGeneratorThread = std::thread(&MiximageGenerator::startThread,
                                                   mMiximageGenerator.get(), &mGeneratorPromise);
        }
    }

    // Update the statistics.
    mStatus->setText("运行中...");
    mGameCounter->setText(std::to_string(mGamesProcessed) + " OF " + std::to_string(mTotalGames) +
                          (mTotalGames == 1 ? " 游戏 " : " 游戏 ") + "已处理");

    mGeneratedVal->setText(std::to_string(mImagesGenerated));
    mFailedVal->setText(std::to_string(mGamesFailed));
    mOverwrittenVal->setText(std::to_string(mImagesOverwritten));

    if (mGamesProcessed == mTotalGames) {
        mStatus->setText("已完成");
        mStartPauseButton->setText("结束", "结束 (关闭)");
        mStartPauseButton->setPressedFunc([this]() { delete this; });
        mCloseButton->setText("关闭", "关闭");
        mProcessingVal->setText("");
        LOG(LogInfo) << "GuiOfflineGenerator: Completed processing (" << mImagesGenerated
                     << (mImagesGenerated == 1 ? " image " : " images ") << "generated, "
                     << mGamesSkipped << (mGamesSkipped == 1 ? " game " : " games ") << "skipped, "
                     << mGamesFailed << (mGamesFailed == 1 ? " game " : " games ") << "failed)";
        mProcessing = false;
    }
}

std::vector<HelpPrompt> GuiOfflineGenerator::getHelpPrompts()
{
    std::vector<HelpPrompt> prompts {mGrid.getHelpPrompts()};
    return prompts;
}
