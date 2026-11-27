//  SPDX-FileCopyrightText: Copyright 2025 shadPS4 Emulator Project
//  SPDX-License-Identifier: GPL-2.0-or-later

#include <charconv>
#include <map>
#include <ranges>
#include <ImGuiFileDialog.h>
#include <cmrc/cmrc.hpp>
#include <pugixml.hpp>
#include <stb_image.h>

#include "common/elf_info.h"
#include "common/logging/log.h"
#include "common/path_util.h"
#include "core/cpu_patches.h"
#include "core/devtools/layer.h"
#include "core/libraries/system/userservice.h"
#include "input/input_handler.h"
#include "imgui/imgui_std.h"
#include "settings_dialog_imgui.h"

CMRC_DECLARE(res);

constexpr float gameImageSize = 200.f;
constexpr float settingsIconSize = 125.f;

namespace ImGuiEmuSettings {

static float in_game_text_scale = 0.30f;

int SettingsWindow::GetComboIndex(std::string selection, std::vector<std::string> options) {
    for (int i = 0; i < options.size(); i++) {
        if (selection == options[i])
            return i;
    }

    return 0;
}

void SettingsWindow::LoadSettings(std::string profile) {
    // Global-only settings
    EmulatorSettings.Load();
    scanDepthSetting = EmulatorSettings.GetBigPictureFolderDepth();

    const bool isSpecific = currentProfile != "Global";
    if (isSpecific) {
        EmulatorSettings.Load(profile);
    }

    /////////// General Tab
    int languageIndex = EmulatorSettings.GetConsoleLanguage();
    std::string language;
    for (const auto& [key, value] : languageMap) {
        if (value == languageIndex) {
            language = key;
        }
    }
    consoleLanguageSetting = GetComboIndex(language, languageOptions);
    volumeSetting = EmulatorSettings.GetVolumeSlider();
    showSplashSetting = EmulatorSettings.IsShowSplash();
    showFpsSetting = isGameRunning ? Overlay::IsSimpleFpsEnabled() : EmulatorSettings.IsShowFpsCounter();
    audioBackendSetting = EmulatorSettings.GetAudioBackend();

    /////////// Graphics Tab
    fullscreenModeSetting =
        GetComboIndex(EmulatorSettings.GetFullScreenMode(), fullscreenModeOptions);
    presentModeSetting = GetComboIndex(EmulatorSettings.GetPresentMode(), presentModeOptions);
    windowHeightSetting = EmulatorSettings.GetWindowHeight();
    windowWidthSetting = EmulatorSettings.GetWindowWidth();
    hdrAllowedSetting = EmulatorSettings.IsHdrAllowed();
    fsrEnabledSetting = EmulatorSettings.IsFsrEnabled();
    dlssEnabledSetting = EmulatorSettings.IsDlssEnabled();
    dlssQualitySetting = EmulatorSettings.GetDlssQuality();
    dlssSharpnessSetting = EmulatorSettings.GetDlssSharpness();
    stretchToFillSetting = EmulatorSettings.IsStretchToFill();
    ignoreFlipRateSetting = EmulatorSettings.IsFlipRateIgnored();
    vblankFrequencySetting = EmulatorSettings.GetVblankFrequency();
    rcasEnabledSetting = EmulatorSettings.IsRcasEnabled();
    rcasAttenuationSetting = static_cast<float>(EmulatorSettings.GetRcasAttenuation() * 0.001f);

    /////////// Input Tab
    motionControlsSetting = EmulatorSettings.IsMotionControlsEnabled();
    backgroundControllerSetting = EmulatorSettings.IsBackgroundControllerInput();
    cursorStateSetting = EmulatorSettings.GetCursorState();
    cursorTimeoutSetting = EmulatorSettings.GetCursorHideTimeout();

    /////////// Trophy Tab
    trophyPopupDisabledSetting = EmulatorSettings.IsTrophyPopupDisabled();
    trophySideSetting =
        GetComboIndex(EmulatorSettings.GetTrophyNotificationSide(), trophySideOptions);
    trophyDurationSetting = static_cast<float>(EmulatorSettings.GetTrophyNotificationDuration());

    /////////// Log Tab
    logEnableSetting = EmulatorSettings.IsLogEnable();
    logSeparateSetting = EmulatorSettings.IsLogSeparate();
    logSyncSetting = EmulatorSettings.IsLogSync();

    /////////// Experimental Tab
    readbacksModeSetting = EmulatorSettings.GetReadbacksMode();
    neoModeSetting = EmulatorSettings.IsNeo();
    pipelineCacheEnabledSetting = EmulatorSettings.IsPipelineCacheEnabled();
    if (isSpecific) {
        readbackLinearImagesSetting = EmulatorSettings.IsReadbackLinearImagesEnabled();
        directMemoryAccessSetting = EmulatorSettings.IsDirectMemoryAccessEnabled();
        windowsGuestRedZoneProtectionModeSetting = EmulatorSettings.IsRedZonePatchingEnabled();
        devkitConsoleSetting = EmulatorSettings.IsDevKit();
        shadnetEnabledSetting = EmulatorSettings.IsShadNetEnabledSetting();
        connectedNetworkSetting = EmulatorSettings.IsConnectedToNetwork();
        pipelineCacheArchiveSetting = EmulatorSettings.IsPipelineCacheArchived();
        extraDmemSetting = EmulatorSettings.GetExtraDmemInMBytes();
    }
}

bool SettingsWindow::SaveSettings(std::string profile) {
    const bool isSpecific = currentProfile != "Global";

    /////////// General Tab
    EmulatorSettings.SetConsoleLanguage(languageMap.at(languageOptions.at(consoleLanguageSetting)),
                                        isSpecific);
    EmulatorSettings.SetVolumeSlider(volumeSetting, isSpecific);
    EmulatorSettings.SetShowSplash(showSplashSetting, isSpecific);
    if (isGameRunning) {
        EmulatorSettings.SetShowFpsCounter(showFpsSetting, isSpecific);
        Overlay::SetSimpleFps(showFpsSetting);
    }
    EmulatorSettings.SetAudioBackend(audioBackendSetting, isSpecific);

    /////////// Graphics Tab
    bool isFullscreen = fullscreenModeSetting != 0;
    EmulatorSettings.SetFullScreen(isFullscreen);
    EmulatorSettings.SetFullScreenMode(fullscreenModeOptions.at(fullscreenModeSetting), isSpecific);
    EmulatorSettings.SetPresentMode(presentModeOptions.at(presentModeSetting), isSpecific);
    EmulatorSettings.SetWindowHeight(windowHeightSetting, isSpecific);
    EmulatorSettings.SetWindowWidth(windowWidthSetting, isSpecific);
    EmulatorSettings.SetHdrAllowed(hdrAllowedSetting, isSpecific);
    EmulatorSettings.SetFsrEnabled(fsrEnabledSetting, isSpecific);
    EmulatorSettings.SetDlssEnabled(dlssEnabledSetting, isSpecific);
    EmulatorSettings.SetDlssQuality(dlssQualitySetting, isSpecific);
    EmulatorSettings.SetDlssSharpness(dlssSharpnessSetting, isSpecific);
    EmulatorSettings.SetStretchToFill(stretchToFillSetting, isSpecific);
    EmulatorSettings.SetFlipRateIgnored(ignoreFlipRateSetting, isSpecific);
    EmulatorSettings.SetVblankFrequency(vblankFrequencySetting, isSpecific);
    EmulatorSettings.SetRcasEnabled(rcasEnabledSetting, isSpecific);
    EmulatorSettings.SetRcasAttenuation(static_cast<int>(rcasAttenuationSetting * 1000),
                                        isSpecific);

    /////////// Input Tab
    EmulatorSettings.SetMotionControlsEnabled(motionControlsSetting, isSpecific);
    EmulatorSettings.SetBackgroundControllerInput(backgroundControllerSetting, isSpecific);
    EmulatorSettings.SetCursorState(cursorStateSetting, isSpecific);
    EmulatorSettings.SetCursorHideTimeout(cursorTimeoutSetting, isSpecific);

    /////////// Trophy Tab
    EmulatorSettings.SetTrophyPopupDisabled(trophyPopupDisabledSetting, isSpecific);
    EmulatorSettings.SetTrophyNotificationSide(trophySideOptions.at(trophySideSetting), isSpecific);
    EmulatorSettings.SetTrophyNotificationDuration(static_cast<double>(trophyDurationSetting));

    /////////// Log Tab
    EmulatorSettings.SetLogEnable(logEnableSetting, isSpecific);
    EmulatorSettings.SetLogSeparate(logSeparateSetting, isSpecific);
    EmulatorSettings.SetLogSync(logSyncSetting, isSpecific);

    /////////// Experimental Tab
    if (isSpecific || isGameRunning) {
        EmulatorSettings.SetReadbacksMode(readbacksModeSetting, isSpecific);
        EmulatorSettings.SetNeo(neoModeSetting, isSpecific);
        EmulatorSettings.SetPipelineCacheEnabled(pipelineCacheEnabledSetting, isSpecific);
    }
    if (isSpecific) {
        EmulatorSettings.SetReadbackLinearImagesEnabled(readbackLinearImagesSetting, true);
        EmulatorSettings.SetDirectMemoryAccessEnabled(directMemoryAccessSetting, true);
        // Windows static guest red-zone protection
        EmulatorSettings.SetRedZonePatchingEnabled(windowsGuestRedZoneProtectionModeSetting, true);
        EmulatorSettings.SetDevKit(devkitConsoleSetting, true);
        EmulatorSettings.SetShadNetEnabled(shadnetEnabledSetting, true);
        EmulatorSettings.SetConnectedToNetwork(connectedNetworkSetting, true);
        EmulatorSettings.SetPipelineCacheArchived(pipelineCacheArchiveSetting, true);
        EmulatorSettings.SetExtraDmemInMBytes(extraDmemSetting, true);
    }

    return isSpecific ? EmulatorSettings.Save(profile) : EmulatorSettings.Save();
}

void SettingsWindow::SaveInstallDirs() {
    std::string profile;
    const bool isGlobal = currentProfile == "Global";
    if (!isGlobal) {
        profile = currentProfile.substr(0, 9);
        EmulatorSettings.Load();
    }

    EmulatorSettings.SetAllGameInstallDirs(m_GameInstallDirs);
    EmulatorSettings.Save();

    if (!isGlobal) {
        EmulatorSettings.Load(profile);
    }

    if (!isGameRunning) {
        GetProfileInfo();
    }
}

void SettingsWindow::GetProfileInfo() {
    GetGameIconInfo(profileIcons);

    BigPictureMode::IconInfo global;
    global.title = "Global";
    profileIcons.emplace(profileIcons.begin(), global);
}

SettingsWindow::SettingsWindow(bool gameRunning) : isGameRunning(gameRunning) {
    auto resource = cmrc::res::get_filesystem();
    auto loadTexture = [&](const std::string& resourcePath,
                           std::variant<SDL_Texture*, ImGui::RefCountedTexture>& texture) {
        auto file = resource.open(resourcePath);
        std::vector<u8> texData = std::vector<u8>(file.begin(), file.end());
        gameRunning ? texture = ImGui::RefCountedTexture::DecodePngTexture(texData)
                    : texture = BigPictureMode::LoadSdlTextureData(texData);
    };

    loadTexture("src/resources/big_picture/settings.png", generalTexture);
    loadTexture("src/resources/big_picture/experimental.png", experimentalTexture);
    loadTexture("src/resources/big_picture/graphics.png", graphicsTexture);
    loadTexture("src/resources/big_picture/controller.png", inputTexture);
    loadTexture("src/resources/big_picture/trophy.png", trophyTexture);
    loadTexture("src/resources/big_picture/log.png", logTexture);
    loadTexture("src/resources/big_picture/folder.png", foldersTexture);
    loadTexture("src/resources/big_picture/profiles.png", profilesTexture);
    if (gameRunning) {
        constexpr std::array grades{"bronze", "silver", "gold", "platinum"};
        for (size_t index = 0; index < grades.size(); ++index) {
            const auto file = resource.open(std::string("src/resources/") + grades[index] + ".png");
            trophyGradeIcons[index] =
                ImGui::RefCountedTexture::DecodePngTexture(std::vector<u8>(file.begin(), file.end()));
        }
    }

    auto languageKeys = std::views::keys(languageMap);
    languageOptions.assign(languageKeys.begin(), languageKeys.end());

    currentProfile = "Global";
    m_GameInstallDirs = EmulatorSettings.GetAllGameInstallDirs();
    currentCategory = isGameRunning ? SettingsCategory::General : SettingsCategory::Profiles;

    bool customConfigFound = false;
    if (isGameRunning) {
        runningGameSerial = std::string(Common::ElfInfo::Instance().GameSerial());
        std::filesystem::path customConfigFile =
            Common::FS::GetUserPath(Common::FS::PathType::CustomConfigs) /
            (runningGameSerial + ".json");

        if (std::filesystem::exists(customConfigFile)) {
            customConfigFound = true;
            currentProfile =
                runningGameSerial + " - " + std::string(Common::ElfInfo::Instance().Title());
        }
    } else {
        GetProfileInfo();
    }

    customConfigFound ? LoadSettings(runningGameSerial) : LoadSettings("Global");
}

void SettingsWindow::Prepare() {
    uiScale = EmulatorSettings.GetBigPictureScale() / 1000.f;
    if (isGameRunning) {
        uiScale = in_game_text_scale;
    }
}

void SettingsWindow::DeInit() {
    EmulatorSettings.Load();
    if (!isGameRunning) {
        EmulatorSettings.SetBigPictureScale(static_cast<int>(uiScale * 1000));
        EmulatorSettings.Save();
    }

    if (isGameRunning && !runningGameSerial.empty()) {
        EmulatorSettings.Load(runningGameSerial);
    }
}
void SettingsWindow::DrawSettings(bool* open, const std::function<void()>& applySettings) {
    if (isGameRunning) {
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * uiScale);
    }
    ImGui::PushStyleColor(ImGuiCol_WindowBg,
                          ImVec4(0.025f, 0.055f, 0.13f, isGameRunning ? 0.66f : 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.035f, 0.13f, 0.30f, 0.96f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.045f, 0.20f, 0.43f, 0.98f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.05f, 0.21f, 0.45f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.10f, 0.33f, 0.62f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(1.0f, 0.76f, 0.19f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.20f, 0.40f, 0.70f, 1.00f));   // blue
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
                          ImVec4(0.25f, 0.50f, 0.85f, 1.00f)); // lighter blue
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive,
                          ImVec4(1.0f, 0.76f, 0.19f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab,
                          ImVec4(0.93f, 0.65f, 0.16f, 0.95f));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, isGameRunning ? 0.0f : ImGui::GetStyle().ScrollbarRounding);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, isGameRunning ? 0.0f : ImGui::GetStyle().GrabRounding);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, isGameRunning ? 0.0f : 5.0f * uiScale);
    const float control_scale = isGameRunning ? std::max(0.65f, uiScale) : uiScale;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f * control_scale, 8.0f * control_scale));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f * control_scale, 6.0f * control_scale));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.5f * uiScale);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    const float padding = (isGameRunning ? 12.0f : 20.0f) * uiScale;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padding, padding));
    ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 20.0f * uiScale);

    SetupWindow();
    DrawCategoryTabs();
    DrawMainContent(open, applySettings);

    ImGui::PopStyleVar(12);
    ImGui::PopStyleColor(10);

    ImGui::End();
    if (isGameRunning) {
        ImGui::PopFont();
    }
}

void SettingsWindow::SetupWindow() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (isGameRunning) {
        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + 16.0f, viewport->WorkPos.y + 16.0f),
                                ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(std::min(630.0f, viewport->WorkSize.x * 0.92f),
                                       std::min(480.0f, viewport->WorkSize.y * 0.92f)),
                                 ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(std::min(420.0f, viewport->WorkSize.x), std::min(300.0f, viewport->WorkSize.y)),
            viewport->WorkSize);
    } else {
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
    }

    ImGui::Begin(isGameRunning ? "NHL 24 PC | In-game settings###NhlUndockedSettings" : "Settings",
                 nullptr, isGameRunning ? ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking
                               : ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoScrollWithMouse);
    if (!isGameRunning) {
        ImGui::DrawPrettyBackground();
        ImGui::SetWindowFontScale(uiScale);
    }

    if (ImGui::IsWindowAppearing()) {
        ImGui::SetWindowFocus();
        ImGui::SetKeyboardFocusHere();
    }

    SettingsCategory firstCategory =
        isGameRunning ? SettingsCategory::General : SettingsCategory::Profiles;
    SettingsCategory lastCategory =
        isGameRunning ? SettingsCategory::Trophy
                      : currentProfile != "Global" ? SettingsCategory::Experimental
                                                    : SettingsCategory::Log;

    // Navigate categories with Tab / R1 / L1
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadR1) || ImGui::IsKeyPressed(ImGuiKey_Tab)) {
        int currentIndex = static_cast<int>(currentCategory);
        currentCategory == lastCategory
            ? currentCategory = static_cast<SettingsCategory>(firstCategory)
            : currentCategory = static_cast<SettingsCategory>(currentIndex + 1);
    }

    if (ImGui::IsKeyPressed(ImGuiKey_GamepadL1)) {
        int currentIndex = static_cast<int>(currentCategory);
        currentIndex == static_cast<int>(firstCategory)
            ? currentCategory = lastCategory
            : currentCategory = static_cast<SettingsCategory>(currentIndex - 1);
    }
}

void SettingsWindow::DrawCategoryTabs() {
    if (isGameRunning) {
        AddCategory("General", generalTexture, SettingsCategory::General);
        AddCategory("Graphics", graphicsTexture, SettingsCategory::Graphics);
        AddCategory("Input", inputTexture, SettingsCategory::Input);
        AddCategory("Trophies", trophyTexture, SettingsCategory::Trophy);
        ImGui::Separator();
        return;
    }
    ImVec4 settingsColor(0.1f, 0.1f, 0.12f, isGameRunning ? 0.12f : 0.8f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, settingsColor);

    float vertSize = (settingsIconSize * uiScale + ImGui::CalcTextSize("Profiles").y) +
                     ImGui::GetStyle().FramePadding.y * 4.f + 20.0 * uiScale;
    if (isGameRunning) {
        vertSize = ImGui::GetFrameHeightWithSpacing() + 2.0f * ImGui::GetStyle().WindowPadding.y;
    }
    ImGuiChildFlags child_flags = ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened;

    ImGuiWindowFlags window_flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

    ImGui::BeginChild("Categories", ImVec2(0, vertSize), child_flags, window_flags);

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                        ImVec2((isGameRunning ? 8.0f : 30.0f) * uiScale, 0.0f));

    // Must add categories in enum order for L1/R1 to work correctly
    if (!isGameRunning) {
        AddCategory("Profiles", profilesTexture, SettingsCategory::Profiles);
    }

    AddCategory("General", generalTexture, SettingsCategory::General);
    AddCategory("Graphics", graphicsTexture, SettingsCategory::Graphics);
    AddCategory("Input", inputTexture, SettingsCategory::Input);
    AddCategory(isGameRunning ? "Trophies" : "Trophy", trophyTexture, SettingsCategory::Trophy);
    if (!isGameRunning) {
        AddCategory("Game Folders", foldersTexture, SettingsCategory::Folders);
        AddCategory("Log", logTexture, SettingsCategory::Log);
        if (currentProfile != "Global") {
            AddCategory("Experimental", experimentalTexture, SettingsCategory::Experimental);
        }
    }

    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImGui::EndChild();
}

void SettingsWindow::AddCategory(std::string name,
                                 std::variant<SDL_Texture*, ImGui::RefCountedTexture> texture,
                                 SettingsCategory category) {
    if (!isGameRunning || category != SettingsCategory::General) {
        ImGui::SameLine();
    }
    if (isGameRunning) {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 4.0f));
        const bool selected = currentCategory == category;
        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonHovered]);
        }
        if (ImGui::Button(name.c_str())) {
            currentCategory = category;
        }
        if (selected) {
            ImGui::PopStyleColor();
        }
        ImGui::PopStyleVar();
        return;
    }
    ImGui::BeginGroup();

    // make button appear hovered as long as category is selected, otherwise dull its hovered color
    currentCategory == category
        ? ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonHovered])
        : ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.235f, 0.392f, 0.624f, 1.00f));

    ImTextureID id = nullptr;
    if (std::holds_alternative<SDL_Texture*>(texture)) {
        id = ImTextureID(std::get<SDL_Texture*>(texture));
    } else if (std::holds_alternative<ImGui::RefCountedTexture>(texture)) {
        id = std::get<ImGui::RefCountedTexture>(texture).GetTexture().im_id;
    }

    if (id != nullptr) {
        if (ImGui::ImageButton(name.c_str(), id,
                               ImVec2(settingsIconSize * uiScale, settingsIconSize * uiScale))) {
            currentCategory = category;
        }
    }

    ImGui::PopStyleColor();

    ImGui::SetCursorPosX(
        (ImGui::GetCursorPosX() +
         (settingsIconSize * uiScale - ImGui::CalcTextSize(name.c_str()).x) * 0.5f) +
        ImGui::GetStyle().FramePadding.x);
    ImGui::Text("%s", name.c_str());
    ImGui::EndGroup();
}

void SettingsWindow::DrawMainContent(bool* open, const std::function<void()>& applySettings) {
    ImVec4 settingsColor(0.1f, 0.1f, 0.12f, isGameRunning ? 0.12f : 0.8f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, settingsColor);

    std::string centeredText;
    currentCategory == SettingsCategory::Folders
        ? centeredText = "Manage shadPS4 Game Folders"
        : centeredText = "Selected Profile: " + currentProfile;

    if (!isGameRunning) {
        ImGui::Separator();
        Overlay::TextCentered(centeredText.c_str());
        ImGui::Separator();
    }

    if (isGameRunning && currentCategory == SettingsCategory::Trophy) {
        DrawGameTrophies();
        ImGui::PopStyleColor();
        if (ImGui::Button("Close")) {
            DeInit();
            *open = false;
        }
        return;
    }
    gameTrophies.reset();

    if (currentCategory == SettingsCategory::Profiles) {
        DrawProfileSelector();
    } else if (currentCategory == SettingsCategory::Folders) {
        DrawGameFolderManager();
    } else {
        DrawSettingsTable(currentCategory);
    }

    ImGui::PopStyleColor();

    ImGui::Separator();
    if (isGameRunning) {
        if (ImGui::Button("Apply & save")) {
            const bool restart_needed =
                fullscreenModeOptions.at(fullscreenModeSetting) != EmulatorSettings.GetFullScreenMode() ||
                presentModeOptions.at(presentModeSetting) != EmulatorSettings.GetPresentMode() ||
                windowWidthSetting != EmulatorSettings.GetWindowWidth() ||
                windowHeightSetting != EmulatorSettings.GetWindowHeight() ||
                hdrAllowedSetting != EmulatorSettings.IsHdrAllowed() ||
                dlssEnabledSetting != EmulatorSettings.IsDlssEnabled() ||
                dlssQualitySetting != EmulatorSettings.GetDlssQuality() ||
                dlssSharpnessSetting != EmulatorSettings.GetDlssSharpness() ||
                vblankFrequencySetting != EmulatorSettings.GetVblankFrequency() ||
                neoModeSetting != EmulatorSettings.IsNeo() ||
                readbacksModeSetting != EmulatorSettings.GetReadbacksMode() ||
                pipelineCacheEnabledSetting != EmulatorSettings.IsPipelineCacheEnabled();
            if (!SaveSettings(currentProfile == "Global" ? "Global" : runningGameSerial)) {
                ImGui::OpenPopup("Settings could not be saved");
            } else if (restart_needed) {
                ImGui::OpenPopup("Restart NHL 24?");
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Close")) {
            DeInit();
            *open = false;
        }
        if (ImGui::BeginPopupModal("Restart NHL 24?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Restart now to apply your saved settings?");
            ImGui::TextUnformatted("Save any game progress before restarting.");
            if (ImGui::Button("Restart now")) {
                SDL_Event event{};
                event.type = SDL_EVENT_RESTART_GAME;
                if (SDL_PushEvent(&event)) {
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Later")) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::SetItemDefaultFocus();
            ImGui::EndPopup();
        }
        if (ImGui::BeginPopupModal("Settings could not be saved", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Check the emulator log for details.");
            if (ImGui::Button("OK")) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::SetItemDefaultFocus();
            ImGui::EndPopup();
        }
        return;
    }
    if (!isGameRunning) {
        ImGui::SetNextItemWidth(300.0f * uiScale);
        static float sliderScale2 = 1.0f;
        if (ImGui::IsWindowAppearing()) {
            sliderScale2 = uiScale;
        }

        ImGui::SliderFloat("UI Scale", &sliderScale2, 0.25f, 3.0f);
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            uiScale = sliderScale2;
        }
        ImGui::SameLine();
    }

    // Align buttons right
    float buttonsWidth = ImGui::CalcTextSize("Save").x + ImGui::CalcTextSize("Cancel").x +
                         ImGui::CalcTextSize("Apply").x + ImGui::GetStyle().FramePadding.x * 6.0f +
                         ImGui::GetStyle().ItemSpacing.x * 2;
    ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - buttonsWidth);

    if (ImGui::Button("Save")) {
        closeOnSave = true;
        ImGui::OpenPopup("Save Confirmation");
    }

    ImGui::SameLine();
    if (ImGui::Button("Apply")) {
        ImGui::OpenPopup("Save Confirmation");
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Save Confirmation", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("%s", ("Profile Saved:\n" + currentProfile).c_str());
        ImGui::Separator();

        if (ImGui::Button("OK", ImVec2(250 * uiScale, 0))) {
            std::string profile = currentProfile;
            if (currentProfile != "Global") {
                profile = currentProfile.substr(0, 9);
            }

            SaveSettings(profile);
            if (closeOnSave) {
                DeInit();
                *open = false;
                closeOnSave = false;
                ImGui::CloseCurrentPopup();
            } else {
                ImGui::CloseCurrentPopup();
            }
            if (applySettings) {
                applySettings();
            }
        }

        ImGui::EndPopup();
    }

    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        DeInit();
        *open = false;
        if (applySettings) {
            applySettings();
        }
    }
}

void SettingsWindow::DrawGameTrophies() {
    const bool entering = !gameTrophies.has_value();
    const bool refresh = ImGui::Button("Refresh trophies");
    if (!gameTrophies || refresh) {
        auto& entries = gameTrophies.emplace();
        int user_id{};
        const bool has_user = Libraries::UserService::sceUserServiceGetInitialUser(&user_id) == 0;
        for (const auto& [label, communication_id] :
             Common::ElfInfo::Instance().GetTrophyIndexMap()) {
            const auto definitions = Common::FS::GetUserPath(Common::FS::PathType::TrophyDir) /
                                     communication_id / "Xml" / "TROP.XML";
            pugi::xml_document metadata;
            if (!metadata.load_file(definitions.c_str())) {
                continue;
            }

            pugi::xml_document progress;
            if (has_user) {
                const auto progress_path = EmulatorSettings.GetHomeDir() / std::to_string(user_id) /
                                           "trophy" / (communication_id + ".xml");
                progress.load_file(progress_path.c_str());
            }
            const auto progress_root = progress.child("trophyconf");
            const auto trophies = metadata.child("trophyconf").children("trophy");
            for (const auto& trophy : trophies) {
                const auto saved = progress_root.find_child_by_attribute(
                    "trophy", "id", trophy.attribute("id").value());
                const bool earned = saved.attribute("unlockstate").as_bool();
                const bool hidden =
                    !earned && std::string_view(trophy.attribute("hidden").value()) == "yes";
                TrophyEntry entry{hidden ? "Hidden trophy" : trophy.child_value("name"),
                                  hidden ? "" : trophy.child_value("detail"), earned,
                                  trophy.attribute("ttype").value()[0]};
                if (!hidden && entry.grade == 'P') {
                    const int platinum_id = trophy.attribute("id").as_int(-1);
                    for (const auto& prerequisite : trophies) {
                        if (prerequisite.attribute("pid").as_int(-1) != platinum_id) {
                            continue;
                        }
                        ++entry.target;
                        const auto unlocked = progress_root.find_child_by_attribute(
                            "trophy", "id", prerequisite.attribute("id").value());
                        entry.progress += unlocked.attribute("unlockstate").as_bool();
                    }
                } else {
                    std::string_view requirement = entry.description;
                    if (requirement.starts_with("Complete ") || requirement.starts_with("Collect ")) {
                        requirement.remove_prefix(requirement.find(' ') + 1);
                        int target{};
                        const auto end = requirement.data() + requirement.size();
                        const auto parsed = std::from_chars(requirement.data(), end, target);
                        if (parsed.ec == std::errc{} && parsed.ptr != end && *parsed.ptr == ' ' &&
                            target > 1) {
                            entry.target = target;
                            entry.progress = earned ? target : -1;
                        }
                    }
                }
                entries.push_back(std::move(entry));
            }
        }
    }

    const auto earned = std::ranges::count_if(*gameTrophies, &TrophyEntry::earned);
    ImGui::SameLine();
    ImGui::Text("%zu / %zu earned", static_cast<size_t>(earned), gameTrophies->size());
    if (ImGui::BeginChild("Game trophies", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()),
                          ImGuiChildFlags_NavFlattened)) {
        if (gameTrophies->empty()) {
            ImGui::TextWrapped("No trophy metadata is available for this game.");
        }
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(10.0f, 9.0f));
        ImGui::PushStyleColor(ImGuiCol_TableBorderLight, ImVec4(0.50f, 0.64f, 0.82f, 0.42f));
        if (ImGui::BeginTable("Trophy list", 3, ImGuiTableFlags_SizingStretchProp |
                                                     ImGuiTableFlags_BordersInnerH)) {
            ImGui::TableSetupColumn("Grade", ImGuiTableColumnFlags_WidthFixed, 48.0f);
            ImGui::TableSetupColumn("Trophy", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed,
                                    ImGui::CalcTextSize("EARNED").x + 12.0f);
            for (size_t index = 0; index < gameTrophies->size(); ++index) {
                const auto& trophy = (*gameTrophies)[index];
                const size_t grade = trophy.grade == 'P' ? 3 : trophy.grade == 'G' ? 2 :
                                     trophy.grade == 'S' ? 1 : 0;
                ImGui::TableNextRow(ImGuiTableRowFlags_None, 58.0f);
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                    trophy.earned ? IM_COL32(65, 94, 60, 112) : IM_COL32(20, 42, 79, 48));
                ImGui::TableNextColumn();
                const auto icon_cell = ImGui::GetCursorScreenPos();
                const float icon_center_x = icon_cell.x + ImGui::GetContentRegionAvail().x * 0.5f;
                ImGui::TableNextColumn();
                const auto text_pos = ImGui::GetCursorScreenPos();
                const float wrap_width = ImGui::GetContentRegionAvail().x;
                float text_height = ImGui::CalcTextSize(trophy.name.c_str(), nullptr, false, wrap_width).y;
                if (!trophy.description.empty()) {
                    text_height += ImGui::GetStyle().ItemSpacing.y +
                                   ImGui::CalcTextSize(trophy.description.c_str(), nullptr, false,
                                                       wrap_width).y;
                }
                if (trophy.target > 0) {
                    text_height += ImGui::GetTextLineHeightWithSpacing();
                }
                ImGui::PushID(static_cast<int>(index));
                if (entering && index == 0) {
                    ImGui::SetKeyboardFocusHere();
                }
                ImGui::Selectable("##Trophy", false, ImGuiSelectableFlags_SpanAllColumns,
                                  ImVec2(0.0f, std::max(40.0f, text_height)));
                const bool focused = ImGui::IsItemFocused();
                ImGui::PopID();
                if (focused) {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(40, 82, 138, 200));
                }
                ImGui::SetCursorScreenPos(text_pos);
                ImGui::TextWrapped("%s", trophy.name.c_str());
                if (!trophy.description.empty()) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.76f, 0.82f, 0.92f, 1.0f));
                    ImGui::TextWrapped("%s", trophy.description.c_str());
                    ImGui::PopStyleColor();
                }
                if (trophy.target > 0) {
                    if (trophy.grade == 'P') {
                        ImGui::TextDisabled("%d / %d required trophies earned", trophy.progress,
                                            trophy.target);
                    } else if (trophy.progress < 0) {
                        ImGui::TextDisabled("Goal: %d | Progress not reported", trophy.target);
                    } else {
                        ImGui::TextDisabled("%d / %d completed", trophy.progress, trophy.target);
                    }
                }
                const float row_content_bottom = ImGui::GetItemRectMax().y;
                ImGui::TableSetColumnIndex(0);
                const auto& icon = trophyGradeIcons[grade];
                if (icon) {
                    constexpr float icon_half_size = 15.0f;
                    const float center_y = (icon_cell.y +
                        std::max(icon_cell.y + 40.0f, row_content_bottom)) * 0.5f;
                    ImGui::GetWindowDrawList()->AddImage(
                        icon.GetTexture().im_id,
                        ImVec2(icon_center_x - icon_half_size, center_y - icon_half_size),
                        ImVec2(icon_center_x + icon_half_size, center_y + icon_half_size));
                }
                ImGui::TableSetColumnIndex(2);
                ImGui::TextColored(trophy.earned ? ImVec4(1.0f, 0.81f, 0.30f, 1.0f)
                                                : ImVec4(0.76f, 0.82f, 0.92f, 1.0f),
                                   "%s", trophy.earned ? "EARNED" : "LOCKED");
            }
            ImGui::EndTable();
        }
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    }
    ImGui::EndChild();
}

void SettingsWindow::DrawProfileSelector() {
    ImGuiChildFlags child_flags = ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened;

    ImGuiWindowFlags window_flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;

    ImVec4 settingsColor = ImVec4(0.1f, 0.1f, 0.12f, 0.8f); // Darker gray
    ImGui::PushStyleColor(ImGuiCol_ChildBg, settingsColor);
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);

    ImGui::BeginChild("Profile Selection", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()),
                      child_flags, window_flags);

    ImGui::PopStyleColor();

    if (ImGui::BeginTable("ProfilesTable", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
        ImGui::TableSetupColumn("CreateDeleteButton", ImGuiTableColumnFlags_WidthFixed,
                                400.0f * uiScale);
        ImGui::TableSetupColumn("Profile");

        for (int i = 0; i < profileIcons.size(); i++) {
            const std::filesystem::path customConfigFile =
                Common::FS::GetUserPath(Common::FS::PathType::CustomConfigs) /
                (profileIcons[i].serial + ".json");
            const bool gameConfigExists = std::filesystem::exists(customConfigFile);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            std::string deleteButtonLabel = "Delete game-specific config##" + std::to_string(i);

            if (gameConfigExists) {
                // Different shades of red depending on button state
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.0f, 0.0f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.0f, 0.0f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.5f, 0.0f, 0.0f, 1.0f));

                if (ImGui::Button(deleteButtonLabel.c_str(),
                                  ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
                    deleteProfileIndex = i;
                }

                ImGui::PopStyleColor(3);
            }

            ImGui::TableNextColumn();
            std::string profileLabel = i == 0
                                           ? "Global"
                                           : profileIcons[i].serial + " - " +
                                                 profileIcons[i].title + "##" + std::to_string(i);
            if (ImGui::Button(profileLabel.c_str(),
                              ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {

                currentProfile = profileLabel;
                if (currentProfile == "Global") {
                    LoadSettings("Global");
                } else {
                    LoadSettings(profileIcons[i].serial);
                    if (!gameConfigExists) {
                        SaveSettings(profileIcons[i].serial);
                    }
                }
            }
        }

        ImGui::EndTable();
    }

    if (deleteProfileIndex != -1) {
        ImGui::OpenPopup("Confirm Delete");
    }

    ImGui::PopStyleVar(3);
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Confirm Delete", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const std::string title = profileIcons[deleteProfileIndex].title;
        const std::string message = "Delete game-specific config file for " + title + "?";
        const std::filesystem::path path =
            Common::FS::GetUserPath(Common::FS::PathType::CustomConfigs) /
            (profileIcons[deleteProfileIndex].serial + ".json");

        ImGui::Text("%s", message.c_str());
        ImGui::Separator();

        if (ImGui::Button("OK", ImVec2(120 * uiScale, 0))) {
            try {
                std::filesystem::remove(path);
            } catch (const std::exception& e) {
                LOG_ERROR(ImGui, "Could not delete config file {}: {}", path.string(), e.what());
            }

            deleteProfileIndex = -1;
            currentProfile = "Global";
            LoadSettings("Global");
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();

        if (ImGui::Button("Cancel", ImVec2(120 * uiScale, 0))) {
            deleteProfileIndex = -1;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    ImGui::EndChild();
}

void SettingsWindow::DrawGameFolderManager() {
    ImGuiChildFlags child_flags = ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened;

    ImGuiWindowFlags window_flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;

    ImGui::BeginChild("ContentRegion", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), child_flags,
                      window_flags);

    if (ImGui::Button("Add Folder", ImVec2(400.f * uiScale, 0))) {
        ImGuiFileDialog::Instance()->OpenDialog("OpenFolder", "Add shadPS4 game folder", nullptr,
                                                ".", 1, nullptr,
                                                ImGuiFileDialogFlags_DisableCreateDirectoryButton |
                                                    ImGuiFileDialogFlags_DontShowHiddenFiles);

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize(viewport->Size);
    }

    float buttonWidth = ImGui::GetItemRectSize().x;
    ImGui::PushItemWidth(buttonWidth);
    if (ImGui::SliderInt("Folder Scan Depth", &scanDepthSetting, 0, 5)) {
        std::string profile;
        const bool isGlobal = currentProfile == "Global";
        if (!isGlobal) {
            profile = currentProfile.substr(0, 9);
            EmulatorSettings.Load();
        }

        EmulatorSettings.SetBigPictureFolderDepth(scanDepthSetting);
        EmulatorSettings.Save();

        if (!isGlobal) {
            EmulatorSettings.Load(profile);
        }

        if (!isGameRunning) {
            GetProfileInfo();
        }
    }
    ImGui::PopItemWidth();

    if (ImGuiFileDialog::Instance()->Display("OpenFolder", child_flags | ImGuiWindowFlags_NoMove)) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            GameInstallDir dir;
            dir.path = ImGuiFileDialog::Instance()->GetCurrentPath();
            dir.enabled = true;

#ifdef WIN32
            // replace \ with / on windows
            std::string pathString = dir.path.string();
            size_t pos = 0;
            while ((pos = pathString.find("\\", pos)) != std::string::npos) {
                pathString.replace(pos, 1, "/");
                pos += 2;
            }

            dir.path = pathString;
#endif

            m_GameInstallDirs.push_back(dir);
            SaveInstallDirs();
        }

        ImGuiFileDialog::Instance()->Close();
    }

    ImGui::BeginChild("Game Folder List", ImVec2(0, 0), true, child_flags);

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0, 5.0f * uiScale));
    if (ImGui::BeginTable("FoldersTable", 3)) {
        ImGui::TableSetupColumn("FolderButton", ImGuiTableColumnFlags_WidthFixed, 300.0f * uiScale);
        ImGui::TableSetupColumn("FolderEnabled", ImGuiTableColumnFlags_WidthFixed, 0.0f);
        ImGui::TableSetupColumn("FolderPath");

        for (int i = 0; i < m_GameInstallDirs.size(); i++) {
            std::string buttonLabel = "Remove Folder##" + std::to_string(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (ImGui::Button(buttonLabel.c_str(), ImVec2(280 * uiScale, 0))) {
                m_GameInstallDirs.erase(m_GameInstallDirs.begin() + i);
                SaveInstallDirs();
            }

            ImGui::TableNextColumn();
            std::string checkboxLabel = "##EnableFolder" + std::to_string(i);
            bool previousState = m_GameInstallDirs[i].enabled;
            ImGui::Checkbox(checkboxLabel.c_str(), &m_GameInstallDirs[i].enabled);
            ImGui::SameLine();
            ImGui::Dummy(ImVec2(5.0 * uiScale, 0));

            if (m_GameInstallDirs[i].enabled != previousState) {
                SaveInstallDirs();
            }

            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", m_GameInstallDirs[i].path.string().c_str());
        }

        ImGui::EndTable();
    }

    ImGui::PopStyleVar();
    ImGui::EndChild();
    ImGui::EndChild();
}

void SettingsWindow::DrawSettingsTable(SettingsCategory category) {
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4.0f * uiScale, 10.0f * uiScale));
    ImGuiChildFlags child_flags = ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened;

    ImGuiWindowFlags window_flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;

    ImGui::BeginChild("ContentRegion", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), child_flags,
                      window_flags);

    if (category == SettingsCategory::General) {
        if (ImGui::BeginTable("SettingsTable", 2)) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed,
                                    isGameRunning ? ImGui::GetContentRegionAvail().x * 0.48f
                                                  : 500.0f * uiScale);
            ImGui::TableSetupColumn("Value");

            AddSettingSliderInt("Volume", volumeSetting, 0, 500);
            if (isGameRunning) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted("Text size");
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::SliderFloat("##OverlayTextSize", &in_game_text_scale, 0.30f, 0.95f,
                                       "%.2f")) {
                    uiScale = in_game_text_scale;
                }
            }
            if (!isGameRunning) {
                AddSettingCombo("Console Language", consoleLanguageSetting, languageOptions);
                AddSettingCheckbox("Show Splash Screen When Launching Game", showSplashSetting);
                AddSettingCombo("Audio Backend", audioBackendSetting, audioBackendOptions);
            }

            ImGui::EndTable();
        }
    } else if (category == SettingsCategory::Graphics) {
        if (ImGui::BeginTable("SettingsTable", 2)) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed,
                                    isGameRunning ? ImGui::GetContentRegionAvail().x * 0.48f
                                                  : 500.0f * uiScale);
            ImGui::TableSetupColumn("Value");

            AddSettingCombo(isGameRunning ? "Display mode (restart)" : "Display Mode",
                            fullscreenModeSetting, fullscreenModeOptions);
            AddSettingCombo(isGameRunning ? "Present mode (restart)" : "Present Mode",
                            presentModeSetting, presentModeOptions);
            if (!isGameRunning) {
                AddSettingSliderInt("Window Width", windowWidthSetting, 0, 8000);
                AddSettingSliderInt("Window Height", windowHeightSetting, 0, 7000);
            }
            AddSettingCheckbox(isGameRunning ? "HDR (restart)" : "Enable HDR", hdrAllowedSetting);
            AddSettingCheckbox("Enable FSR", fsrEnabledSetting);
            if (isGameRunning) {
                AddSettingCheckbox("Show FPS", showFpsSetting);
                AddSettingSliderInt("Output width (restart)", windowWidthSetting, 640, 7680);
                AddSettingSliderInt("Output height (restart)", windowHeightSetting, 360, 4320);
                AddSettingCheckbox("PS4 Pro mode (restart)", neoModeSetting);
                AddSettingCombo("GPU readbacks (restart)", readbacksModeSetting, readbacksModeOptions);
                AddSettingCheckbox("Vulkan driver cache (restart)", pipelineCacheEnabledSetting);
            }
            AddSettingCheckbox("Enable DLSS (RTX, restart)", dlssEnabledSetting);
            if (dlssEnabledSetting) {
                AddSettingCombo("DLSS quality (restart)", dlssQualitySetting,
                                {"DLAA", "Quality", "Balanced", "Performance", "Ultra Performance"});
                AddSettingSliderInt("DLSS sharpness (restart)", dlssSharpnessSetting, 0, 1000);
            }
            AddSettingCheckbox("Stretch to fill", stretchToFillSetting);
            AddSettingCheckbox("Smooth menus & cutscenes", ignoreFlipRateSetting);
            AddSettingSliderInt("Frame rate cap (restart)", vblankFrequencySetting, 30, 240);

            if (fsrEnabledSetting) {
                AddSettingCheckbox("Enable RCAS", rcasEnabledSetting);
            }

            if (rcasEnabledSetting && fsrEnabledSetting) {
                AddSettingSliderFloat("RCAS Attenuation", rcasAttenuationSetting, 0.0f, 3.0f, 3);
            }

            ImGui::EndTable();
        }
    } else if (category == SettingsCategory::Input) {
        if (ImGui::BeginTable("SettingsTable", 2)) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed,
                                    isGameRunning ? ImGui::GetContentRegionAvail().x * 0.48f
                                                  : 500.0f * uiScale);
            ImGui::TableSetupColumn("Value");

            if (!isGameRunning) {
                AddSettingCheckbox("Enable Motion Controls", motionControlsSetting);
            }
            AddSettingCheckbox("Background controller input", backgroundControllerSetting);
            AddSettingCombo("Hide Cursor", cursorStateSetting, hideCursorOptions);

            if (cursorStateSetting == 1) {
                AddSettingSliderInt("Hide Cursor Idle Timeout", cursorTimeoutSetting, 1, 10);
            }

            ImGui::EndTable();
        }
    } else if (category == SettingsCategory::Trophy) {
        if (ImGui::BeginTable("SettingsTable", 2)) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed,
                                    isGameRunning ? ImGui::GetContentRegionAvail().x * 0.48f
                                                  : 500.0f * uiScale);
            ImGui::TableSetupColumn("Value");

            AddSettingCheckbox("Disable Trophy Notification", trophyPopupDisabledSetting);
            if (!trophyPopupDisabledSetting) {
                AddSettingCombo("Trophy Notification Position", trophySideSetting,
                                trophySideOptions);
                AddSettingSliderFloat("Trophy Notification Duration", trophyDurationSetting, 0.f,
                                      10.f, 1);
            }

            ImGui::EndTable();
        }
    } else if (category == SettingsCategory::Log) {
        if (ImGui::BeginTable("SettingsTable", 2)) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed,
                                    isGameRunning ? ImGui::GetContentRegionAvail().x * 0.48f
                                                  : 500.0f * uiScale);
            ImGui::TableSetupColumn("Value");

            AddSettingCheckbox("Enable Logging", logEnableSetting);
            if (logEnableSetting) {
                AddSettingCheckbox("Separate Log Files", logSeparateSetting);
                AddSettingCheckbox("Log Sync", logSyncSetting);
            }

            ImGui::EndTable();
        }
    } else if (category == SettingsCategory::Experimental) {
        if (ImGui::BeginTable("SettingsTable", 2)) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed,
                                    isGameRunning ? ImGui::GetContentRegionAvail().x * 0.48f
                                                  : 500.0f * uiScale);
            ImGui::TableSetupColumn("Value");

            AddSettingSliderInt("Additional DMem Allocation", extraDmemSetting, 0, 20000);
            AddSettingCombo("Readbacks Mode", readbacksModeSetting, readbacksModeOptions);
            AddSettingCheckbox("Enable Readback Linear Images", readbackLinearImagesSetting);
            AddSettingCheckbox("Enable Direct Memory Access", directMemoryAccessSetting);
            AddSettingCheckbox("Enable Devkit Console Mode", devkitConsoleSetting);
            AddSettingCheckbox("Enable PS4 Neo Mode", neoModeSetting);
            AddSettingCheckbox("Enable ShadNet", shadnetEnabledSetting);
            AddSettingCheckbox("Set Network Connected to True", connectedNetworkSetting);
            AddSettingCheckbox("Enable Shader Cache", pipelineCacheEnabledSetting);

            if (pipelineCacheEnabledSetting) {
                AddSettingCheckbox("Compress Shader Cache to Zip File",
                                   pipelineCacheArchiveSetting);
            }

            ImGui::EndTable();
        }
    }

    ImGui::PopStyleVar();
    ImGui::EndChild();
}

void SettingsWindow::AddSettingCheckbox(std::string name, bool& value) {
    std::string label = "##" + name;
    ImGui::TableNextRow();
    ImGui::TableNextColumn();

    ImGui::TextWrapped("%s", name.c_str());
    ImGui::TableNextColumn();
    ImGui::Checkbox(label.c_str(), &value);
}

void SettingsWindow::AddSettingSliderInt(std::string name, int& value, int min, int max) {
    std::string label = "##" + name;
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextWrapped("%s", name.c_str());

    ImGui::TableNextColumn();
    ImGui::SliderInt(label.c_str(), &value, min, max);
}

void SettingsWindow::AddSettingSliderFloat(std::string name, float& value, int min, int max,
                                           int precision) {
    std::string label = "##" + name;
    std::string precisionString = "%." + std::to_string(precision) + "f";

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextWrapped("%s", name.c_str());

    ImGui::TableNextColumn();
    ImGui::SliderFloat(label.c_str(), &value, min, max, precisionString.c_str());
}

void SettingsWindow::AddSettingCombo(std::string name, int& value,
                                     std::vector<std::string> options) {
    std::string label = "##" + name;
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextWrapped("%s", name.c_str());

    ImGui::TableNextColumn();
    const char* combo_value = options[value].c_str();
    if (ImGui::BeginCombo(label.c_str(), combo_value)) {
        for (int i = 0; i < options.size(); i++) {
            const bool selected = (i == value);
            if (ImGui::Selectable(options[i].c_str(), selected))
                value = i;

            // Set the initial focus when opening the combo
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
}

} // namespace ImGuiEmuSettings
