// L4D2 PC port - ReXGlue application.
#pragma once

#include <rex/rex_app.h>

class L4d2App : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<L4d2App>(new L4d2App(ctx, "l4d2", PPCImageConfig));
  }

  // Game files: --game_data_root, else <exe_dir>\game (launcher install
  // layout), else the developer tree.
  void OnConfigurePaths(rex::PathConfig& paths) override;
  void OnPostInitLogging() override;
  void OnPreSetup(rex::RuntimeConfig& config) override;
  void OnPostSetup() override;
  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override;
  void OnShutdown() override;

 private:
  std::unique_ptr<rex::ui::ImGuiDialog> perf_overlay_;
};
