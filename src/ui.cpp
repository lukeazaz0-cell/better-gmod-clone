#include "ui.h"
#include <imgui.h>
#include <cstdio>
#include <filesystem>
#include "game.h"

namespace ui {

static ImU32 col(const glm::vec4& c) { return ImGui::ColorConvertFloat4ToU32(ImVec4(c.r, c.g, c.b, c.a)); }

void setupStyle(Game& g) {
  ImGuiIO& io = ImGui::GetIO();
  const char* fonts[] = {"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                         "/usr/share/fonts/TTF/DejaVuSans.ttf",
                         "/usr/share/fonts/dejavu/DejaVuSans.ttf",
                         "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
                         "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
                         "/usr/share/fonts/noto/NotoSans-Regular.ttf"};
  for (const char* f : fonts) {
    if (std::filesystem::exists(f)) {
      io.Fonts->AddFontFromFileTTF(f, 17.0f);
      g.fontBig = io.Fonts->AddFontFromFileTTF(f, 30.0f);
      break;
    }
  }
  if (!g.fontBig) {
    io.Fonts->AddFontDefault();
    ImFontConfig cfg;
    cfg.SizePixels = 26;
    g.fontBig = io.Fonts->AddFontDefault(&cfg);
  }

  ImGui::StyleColorsDark();
  ImGuiStyle& s = ImGui::GetStyle();
  s.WindowRounding = 4;
  s.FrameRounding = 3;
  s.GrabRounding = 3;
  s.WindowPadding = ImVec2(10, 10);
  s.ItemSpacing = ImVec2(8, 6);
  ImVec4* c = s.Colors;
  c[ImGuiCol_WindowBg] = ImVec4(0.11f, 0.12f, 0.14f, 0.94f);
  c[ImGuiCol_ChildBg] = ImVec4(0.15f, 0.16f, 0.19f, 0.9f);
  c[ImGuiCol_TitleBgActive] = ImVec4(0.16f, 0.36f, 0.62f, 1.0f);
  c[ImGuiCol_Header] = ImVec4(0.2f, 0.42f, 0.7f, 0.6f);
  c[ImGuiCol_HeaderHovered] = ImVec4(0.25f, 0.5f, 0.82f, 0.8f);
  c[ImGuiCol_HeaderActive] = ImVec4(0.25f, 0.55f, 0.9f, 1.0f);
  c[ImGuiCol_Button] = ImVec4(0.2f, 0.3f, 0.45f, 0.8f);
  c[ImGuiCol_ButtonHovered] = ImVec4(0.27f, 0.45f, 0.7f, 1.0f);
  c[ImGuiCol_ButtonActive] = ImVec4(0.3f, 0.55f, 0.85f, 1.0f);
  c[ImGuiCol_Tab] = ImVec4(0.18f, 0.25f, 0.36f, 1.0f);
  c[ImGuiCol_TabHovered] = ImVec4(0.27f, 0.45f, 0.7f, 1.0f);
  c[ImGuiCol_TabActive] = ImVec4(0.22f, 0.42f, 0.68f, 1.0f);
  c[ImGuiCol_FrameBg] = ImVec4(0.18f, 0.2f, 0.25f, 1.0f);
}

static void textShadow(ImDrawList* dl, ImFont* f, float size, ImVec2 p, ImU32 c, const char* t) {
  dl->AddText(f, size, ImVec2(p.x + 1, p.y + 1), IM_COL32(0, 0, 0, 200), t);
  dl->AddText(f, size, p, c, t);
}

void drawHUD(Game& g) {
  ImGuiIO& io = ImGui::GetIO();
  ImDrawList* dl = ImGui::GetBackgroundDrawList();
  ImFont* font = ImGui::GetFont();
  float W = io.DisplaySize.x, H = io.DisplaySize.y;
  ImVec2 c(W * 0.5f, H * 0.5f);

  // Crosshair
  if (!g.spawnMenu) {
    dl->AddCircleFilled(c, 2.5f, IM_COL32(0, 0, 0, 160));
    dl->AddCircleFilled(c, 1.5f, IM_COL32(255, 255, 255, 230));
    for (int i = 0; i < 4; i++) {
      float a = i * 1.5707963f;
      ImVec2 d(std::cos(a), std::sin(a));
      dl->AddLine(ImVec2(c.x + d.x * 7, c.y + d.y * 7), ImVec2(c.x + d.x * 13, c.y + d.y * 13), IM_COL32(0, 0, 0, 150), 3);
      dl->AddLine(ImVec2(c.x + d.x * 7, c.y + d.y * 7), ImVec2(c.x + d.x * 13, c.y + d.y * 13),
                  IM_COL32(255, 255, 255, 220), 1.5f);
    }
  }

  // Weapon selector
  {
    const float bw = 170, bh = 34, gap = 8;
    float total = g.weapons.size() * bw + (g.weapons.size() - 1) * gap;
    float x0 = c.x - total * 0.5f, y0 = H - bh - 16;
    for (size_t i = 0; i < g.weapons.size(); i++) {
      bool cur = (int)i == g.curWeapon;
      ImVec2 a(x0 + i * (bw + gap), y0), b(a.x + bw, a.y + bh);
      dl->AddRectFilled(a, b, cur ? IM_COL32(40, 100, 180, 220) : IM_COL32(20, 22, 28, 170), 4);
      if (cur) dl->AddRect(a, b, IM_COL32(120, 190, 255, 255), 4, 0, 2);
      char buf[64];
      snprintf(buf, sizeof(buf), "%zu  %s", i + 1, g.weapons[i]->name());
      ImVec2 ts = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0, buf);
      textShadow(dl, font, font->FontSize, ImVec2(a.x + (bw - ts.x) * 0.5f, a.y + (bh - ts.y) * 0.5f),
                 IM_COL32(255, 255, 255, 255), buf);
    }
  }

  // Tool info panel (GMod style, top-left)
  if (g.curWeapon == 2 && !g.spawnMenu) {
    if (Tool* t = g.toolgun()->tool()) {
      ImVec2 p(18, 18);
      std::string help = t->help();
      float w = 560;
      ImVec2 hs = font->CalcTextSizeA(font->FontSize, FLT_MAX, w - 24, t->desc());
      float h = 52 + hs.y + 30;
      dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), IM_COL32(15, 18, 24, 190), 6);
      dl->AddRectFilled(p, ImVec2(p.x + 6, p.y + h), IM_COL32(60, 150, 255, 255), 6, ImDrawFlags_RoundCornersLeft);
      textShadow(dl, g.fontBig, g.fontBig->FontSize, ImVec2(p.x + 16, p.y + 8), IM_COL32(255, 255, 255, 255), t->name());
      dl->AddText(font, font->FontSize, ImVec2(p.x + 16, p.y + 48), IM_COL32(200, 210, 225, 255), t->desc(), nullptr,
                  w - 24);
      dl->AddText(font, font->FontSize, ImVec2(p.x + 16, p.y + 52 + hs.y + 4), IM_COL32(130, 200, 255, 255),
                  help.c_str());
    }
  } else if (g.curWeapon == 0 && g.physgun()->held() >= 0) {
    const char* h = "E + mouse: rotate   Shift: snap   Scroll: distance   Right click: freeze";
    ImVec2 ts = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0, h);
    textShadow(dl, font, font->FontSize, ImVec2(c.x - ts.x * 0.5f, H - 90), IM_COL32(180, 220, 255, 255), h);
  }

  // Stats (top-right)
  {
    char buf[256];
    snprintf(buf, sizeof(buf), "%.0f FPS   Props: %zu   Constraints: %zu%s%s", g.fps, g.world.props.size(),
             g.world.joints.size(), g.player.noclip ? "   [NOCLIP]" : "",
             g.physics.timeScale != 1.0f ? "   [TIMESCALE]" : "");
    ImVec2 ts = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0, buf);
    textShadow(dl, font, font->FontSize, ImVec2(W - ts.x - 16, 12), IM_COL32(230, 230, 230, 230), buf);
  }

  // Notifications (bottom-right)
  {
    float y = H - 70;
    for (auto it = g.notify.notes.rbegin(); it != g.notify.notes.rend(); ++it) {
      float a = it->t < 4.5f ? 1.0f : (5.0f - it->t) * 2.0f;
      float slide = it->t < 0.15f ? (1.0f - it->t / 0.15f) * 60.0f : 0.0f;
      ImVec2 ts = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0, it->text.c_str());
      float x = W - ts.x - 40 + slide;
      y -= ts.y + 14;
      dl->AddRectFilled(ImVec2(x - 12, y - 5), ImVec2(W - 16 + slide, y + ts.y + 5), IM_COL32(15, 18, 24, (int)(200 * a)), 4);
      dl->AddRectFilled(ImVec2(x - 12, y - 5), ImVec2(x - 8, y + ts.y + 5), col(glm::vec4(glm::vec3(it->color), a)));
      dl->AddText(font, font->FontSize, ImVec2(x, y), IM_COL32(255, 255, 255, (int)(255 * a)), it->text.c_str());
    }
  }

  if (g.hintTimer > 0 && !g.spawnMenu) {
    const char* h = "Hold Q for the spawn menu  -  F1 for controls";
    float a = std::min(1.0f, g.hintTimer);
    ImVec2 ts = g.fontBig->CalcTextSizeA(g.fontBig->FontSize, FLT_MAX, 0, h);
    textShadow(dl, g.fontBig, g.fontBig->FontSize, ImVec2(c.x - ts.x * 0.5f, H * 0.22f),
               IM_COL32(255, 255, 255, (int)(230 * a)), h);
  }
}

static void propsTab(Game& g) {
  const auto& cat = propCatalog();
  for (const std::string& category : propCategories()) {
    ImGui::SeparatorText(category.c_str());
    float avail = ImGui::GetContentRegionAvail().x;
    const float bw = 118, bh = 64;
    int perRow = std::max(1, (int)((avail + 8) / (bw + 8)));
    int n = 0;
    for (const PropDef& d : cat) {
      if (d.category != category) continue;
      if (n % perRow != 0) ImGui::SameLine();
      ImGui::PushID(d.id.c_str());
      glm::vec3 c = glm::vec3(d.color) * 0.55f;
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(c.r, c.g, c.b, 0.9f));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(c.r * 1.5f + 0.1f, c.g * 1.5f + 0.1f, c.b * 1.5f + 0.1f, 1));
      if (ImGui::Button(d.name.c_str(), ImVec2(bw, bh))) g.spawnProp(d);
      ImGui::PopStyleColor(2);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s\n%s, %.1f kg", d.name.c_str(), materialName(d.material), d.mass);
      ImGui::PopID();
      n++;
    }
  }
}

static void savesTab(Game& g) {
  static char name[128] = "my_save";
  ImGui::InputText("Name", name, sizeof(name));
  ImGui::SameLine();
  if (ImGui::Button("Save")) {
    if (g.saveGame(name)) g.notify.push(std::string("Saved ") + name, glm::vec4(0.4f, 1, 0.5f, 1));
  }
  ImGui::TextDisabled("Saves folder: %s", g.saveDir.c_str());
  ImGui::TextDisabled("F5 quick-saves, F9 quick-loads.");
  ImGui::Separator();
  for (const std::string& s : g.listSaves()) {
    ImGui::PushID(s.c_str());
    if (ImGui::Button("Load")) {
      if (g.loadGame(s)) g.notify.push("Loaded " + s, glm::vec4(0.4f, 1, 0.5f, 1));
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete")) g.deleteSave(s);
    ImGui::SameLine();
    ImGui::TextUnformatted(s.c_str());
    ImGui::PopID();
  }
}

static void optionsTab(Game& g) {
  ImGui::SeparatorText("Physics");
  float grav = g.physics.gravity();
  if (ImGui::SliderFloat("Gravity", &grav, 0.0f, 30.0f, "%.2f m/s^2")) g.physics.setGravity(grav);
  ImGui::SliderFloat("Time scale", &g.physics.timeScale, 0.05f, 2.0f);
  ImGui::Checkbox("Pause physics", &g.physics.paused);
  if (ImGui::Button("Freeze all")) g.freezeAll(true);
  ImGui::SameLine();
  if (ImGui::Button("Unfreeze all")) g.freezeAll(false);
  ImGui::SameLine();
  if (!g.confirmClear) {
    if (ImGui::Button("Remove everything")) g.confirmClear = true;
  } else {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.15f, 0.1f, 1));
    if (ImGui::Button("Really remove everything?")) {
      g.world.clearProps();
      g.notify.push("Cleaned up everything");
      g.confirmClear = false;
    }
    ImGui::PopStyleColor();
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) g.confirmClear = false;
  }

  ImGui::SeparatorText("Player");
  ImGui::SliderFloat("Mouse sensitivity", &g.mouseSens, 0.0005f, 0.008f, "%.4f");
  ImGui::SliderFloat("Field of view", &g.fov, 60, 110, "%.0f");
  if (ImGui::Button("Respawn")) g.respawn();
  ImGui::SameLine();
  bool nc = g.player.noclip;
  if (ImGui::Checkbox("Noclip (V)", &nc)) g.player.setNoclip(nc);

  ImGui::SeparatorText("Graphics & sound");
  ImGui::Checkbox("Shadows", &g.renderer.shadows);
  ImGui::SliderFloat("Shadow range", &g.renderer.shadowRange, 20, 200, "%.0f m");
  ImGui::SliderFloat("Volume", &g.audio.masterVolume, 0, 1);
}

void drawSpawnMenu(Game& g) {
  ImGuiIO& io = ImGui::GetIO();
  float W = io.DisplaySize.x, H = io.DisplaySize.y;
  float margin = std::max(20.0f, std::min(W, H) * 0.05f);
  float toolW = std::min(380.0f, W * 0.3f);

  ImGui::SetNextWindowPos(ImVec2(margin, margin));
  ImGui::SetNextWindowSize(ImVec2(W - margin * 3 - toolW, H - margin * 2));
  ImGui::Begin("Spawn Menu", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
  if (ImGui::BeginTabBar("tabs")) {
    if (ImGui::BeginTabItem("Props")) {
      ImGui::BeginChild("props");
      propsTab(g);
      ImGui::EndChild();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Saves")) {
      savesTab(g);
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Options")) {
      optionsTab(g);
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
  ImGui::End();

  ImGui::SetNextWindowPos(ImVec2(W - margin - toolW, margin));
  ImGui::SetNextWindowSize(ImVec2(toolW, H - margin * 2));
  ImGui::Begin("Tools", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
  ToolGun* tg = g.toolgun();
  ImGui::BeginChild("toollist", ImVec2(0, H * 0.38f), ImGuiChildFlags_Borders);
  const char* cats[] = {"Construction", "Gadgets", "Utility", "Render"};
  for (const char* cat : cats) {
    ImGui::SeparatorText(cat);
    for (size_t i = 0; i < tg->tools.size(); i++) {
      if (std::string(tg->tools[i]->category()) != cat) continue;
      bool sel = (int)i == tg->current && g.curWeapon == 2;
      if (ImGui::Selectable(tg->tools[i]->name(), sel)) {
        tg->select((int)i);
        g.selectWeapon(2);
      }
    }
  }
  ImGui::EndChild();
  if (Tool* t = tg->tool()) {
    ImGui::PushFont(g.fontBig);
    ImGui::TextUnformatted(t->name());
    ImGui::PopFont();
    ImGui::TextWrapped("%s", t->desc());
    ImGui::Separator();
    ImGui::PushItemWidth(-110);
    t->settings();
    ImGui::PopItemWidth();
  }
  ImGui::End();
}

void drawPauseMenu(Game& g) {
  ImGuiIO& io = ImGui::GetIO();
  ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 0, 0, 140));
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), 0, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(320, 0));
  ImGui::Begin("Paused", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
  ImVec2 bs(-1, 40);
  if (ImGui::Button("Resume", bs)) g.paused = false;
  if (ImGui::Button("Controls", bs)) g.showHelp = !g.showHelp;
  if (ImGui::Button("Quick save (F5)", bs) && g.saveGame("quicksave")) g.notify.push("Quick saved");
  if (ImGui::Button("Quick load (F9)", bs) && g.loadGame("quicksave")) g.notify.push("Quick loaded");
  if (ImGui::Button("Quit", bs)) g.running = false;
  ImGui::End();
}

void drawHelp(Game& g) {
  ImGuiIO& io = ImGui::GetIO();
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_Appearing);
  ImGui::Begin("Controls (F1)", &g.showHelp, ImGuiWindowFlags_NoCollapse);
  const char* rows[][2] = {
      {"W A S D", "Move"},
      {"Space / Ctrl", "Jump / crouch (up / down in noclip)"},
      {"Shift", "Sprint"},
      {"Mouse", "Look"},
      {"1 2 3 / wheel", "Physics Gun / Gravity Gun / Tool Gun"},
      {"Q (hold or tap)", "Spawn menu + tool settings"},
      {"Z", "Undo"},
      {"V", "Toggle noclip"},
      {"F5 / F9", "Quick save / quick load"},
      {"F12", "Screenshot"},
      {"Esc", "Pause menu"},
      {"", ""},
      {"Physics Gun", ""},
      {"  Left mouse", "Grab and move objects"},
      {"  Scroll", "Push / pull held object"},
      {"  E + mouse", "Rotate held object (Shift snaps to 45 deg)"},
      {"  Right mouse", "Freeze held object in place"},
      {"  R", "Unfreeze the contraption you are aiming at"},
      {"Gravity Gun", ""},
      {"  Right mouse", "Pull / pick up / drop"},
      {"  Left mouse", "Punt / launch"},
      {"Tool Gun", ""},
      {"  Left / Right / R", "Use current tool (see top-left panel)"},
  };
  if (ImGui::BeginTable("keys", 2, ImGuiTableFlags_RowBg)) {
    for (auto& r : rows) {
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::TextColored(ImVec4(0.5f, 0.8f, 1, 1), "%s", r[0]);
      ImGui::TableSetColumnIndex(1);
      ImGui::TextUnformatted(r[1]);
    }
    ImGui::EndTable();
  }
  ImGui::End();
}

}  // namespace ui
