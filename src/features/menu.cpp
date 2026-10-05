#include "menu.h"
#include "../imgui/imgui_internal.h"
#include "playerlist.h"
#include "weaponhud.h"
#include "screenfx.h"
#include "websurface.h"
#include "websurf_math.h"

static void ColorPickerPopup(float* colorData, int channels, const char* id)
{
    char popupId[48], btnId[48], copyId[48], pasteId[48];
    snprintf(popupId, sizeof(popupId), "cpopup%s", id);
    snprintf(btnId,   sizeof(btnId),   "cbtn%s",   id);
    snprintf(copyId,  sizeof(copyId),  "Copy##cp%s", id);
    snprintf(pasteId, sizeof(pasteId), "Paste##cp%s", id);

    ImVec4 swatch = ImVec4(colorData[0], colorData[1], colorData[2],
                           channels == 4 ? colorData[3] : 1.f);
    const float sz = ImGui::GetFrameHeight();
    if (ImGui::ColorButton(btnId, swatch, ImGuiColorEditFlags_NoTooltip, ImVec2(sz, sz)))
        ImGui::OpenPopup(popupId);

    if (ImGui::BeginPopup(popupId))
    {
        ImGuiColorEditFlags pickerFlags =
            ImGuiColorEditFlags_PickerHueBar |
            ImGuiColorEditFlags_NoSidePreview |
            ImGuiColorEditFlags_NoSmallPreview |
            ImGuiColorEditFlags_DisplayRGB;
        if (channels == 4)
            pickerFlags |= ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreview;

        if (channels == 4) ImGui::ColorPicker4(id, colorData, pickerFlags);
        else               ImGui::ColorPicker3(id, colorData, pickerFlags);

        int ri = (int)(colorData[0]*255.f+0.5f);
        int gi = (int)(colorData[1]*255.f+0.5f);
        int bi = (int)(colorData[2]*255.f+0.5f);
        char hexBuf[12]{};
        if (channels == 4) {
            int ai = (int)(colorData[3]*255.f+0.5f);
            snprintf(hexBuf, sizeof(hexBuf), "#%02X%02X%02X%02X", ri, gi, bi, ai);
        } else {
            snprintf(hexBuf, sizeof(hexBuf), "#%02X%02X%02X", ri, gi, bi);
        }

        ImGui::TextDisabled("%s", hexBuf);

        const float bw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        if (ImGui::Button(copyId, ImVec2(bw, 0)))
        {
            ImGui::SetClipboardText(hexBuf);
            for (int i = 0; i < channels; i++) g_colorClipboard[i] = colorData[i];
            if (channels == 3) g_colorClipboard[3] = 1.f;
            g_colorClipboardFilled   = true;
        }

        if (g_colorClipboardFilled)
        {
            ImGui::SameLine();
            if (ImGui::Button(pasteId, ImVec2(bw, 0)))
            {
                int apply = (channels < 4) ? channels : 4;
                for (int i = 0; i < apply; i++) colorData[i] = g_colorClipboard[i];
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::ColorButton("##prev",
                    ImVec4(g_colorClipboard[0], g_colorClipboard[1],
                           g_colorClipboard[2], g_colorClipboard[3]),
                    ImGuiColorEditFlags_NoTooltip, ImVec2(32, 16));
                ImGui::SameLine();
                ImGui::TextUnformatted("Paste color");
                ImGui::EndTooltip();
            }
        }
        ImGui::EndPopup();
    }
}

// ---- Config file helpers -----------------------------------
static std::vector<std::string> ListConfigs()
{
    std::vector<std::string> names;
    WIN32_FIND_DATAA fd{};
    HANDLE h = FindFirstFileA("C:\\infovisual\\*.cfg", &fd);
    if (h == INVALID_HANDLE_VALUE) return names;
    do { names.push_back(fd.cFileName); } while (FindNextFileA(h, &fd));
    FindClose(h);
    std::sort(names.begin(), names.end());
    return names;
}

static std::string ConfigFullPath(const std::string& name) {
    return std::string("C:\\infovisual\\") + name;
}

static void DeleteConfig(const std::string& name) {
    DeleteFileA(ConfigFullPath(name).c_str());
}

// Human-readable name for a virtual-key code (for the menu key rebind UI).
static std::string VkToName(int vk)
{
    switch (vk) {
        case VK_INSERT:   return "Insert";
        case VK_DELETE:   return "Delete";
        case VK_HOME:     return "Home";
        case VK_END:      return "End";
        case VK_PRIOR:    return "Page Up";
        case VK_NEXT:     return "Page Down";
        case VK_TAB:      return "Tab";
        case VK_RETURN:   return "Enter";
        case VK_SPACE:    return "Space";
        case VK_BACK:     return "Backspace";
        case VK_ESCAPE:   return "Escape";
        case VK_PAUSE:    return "Pause";
        case VK_CAPITAL:  return "Caps Lock";
        case VK_LCONTROL: case VK_RCONTROL: case VK_CONTROL: return "Ctrl";
        case VK_LSHIFT:   case VK_RSHIFT:   case VK_SHIFT:   return "Shift";
        case VK_LMENU:    case VK_RMENU:    case VK_MENU:    return "Alt";
        case VK_LBUTTON:  return "Mouse 1";
        case VK_RBUTTON:  return "Mouse 2";
        case VK_MBUTTON:  return "Mouse 3";
        case VK_XBUTTON1: return "Mouse 4";
        case VK_XBUTTON2: return "Mouse 5";
        case VK_LEFT:     return "Left";
        case VK_RIGHT:    return "Right";
        case VK_UP:       return "Up";
        case VK_DOWN:     return "Down";
    }
    if (vk >= VK_F1 && vk <= VK_F24)        return "F" + std::to_string(vk - VK_F1 + 1);
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return "Num " + std::to_string(vk - VK_NUMPAD0);
    if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z'))
        return std::string(1, (char)vk);
    // Fall back to the OS key name via the scan code.
    UINT sc = MapVirtualKeyA(vk, MAPVK_VK_TO_VSC);
    char buf[64] = {};
    if (sc && GetKeyNameTextA((LONG)(sc << 16), buf, sizeof(buf)) > 0)
        return buf;
    char hex[16];
    snprintf(hex, sizeof(hex), "0x%02X", vk);
    return hex;
}

// ---- Custom menu widgets -----------------------------------
// Circle toggle checkbox matching the reference panel design.
static bool MenuCheckbox(const char* labelAndId, bool* v, float width = 0.f)
{
    const char* hash    = strstr(labelAndId, "##");
    const char* dispEnd = hash ? hash : labelAndId + strlen(labelAndId);

    const float rowH  = 30.f;
    const float circR = 6.f;

    ImVec2 pos   = ImGui::GetCursorScreenPos();
    float  avail = width > 0.f ? width : ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    ImGuiID  id = ImGui::GetID(labelAndId);
    ImRect   bb(pos, ImVec2(pos.x + avail, pos.y + rowH));
    ImGui::ItemSize(bb);
    if (!ImGui::ItemAdd(bb, id)) return false;

    bool hov, held;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hov, &held);
    if (pressed) *v = !(*v);

    // Smooth lerp via ImGui storage (10 fps-independent)
    ImGuiStorage* st = ImGui::GetStateStorage();
    float t  = st->GetFloat(id,          *v  ? 1.f : 0.f);
    float ht = st->GetFloat(id + 0x9000u, 0.f);
    float dt = ImGui::GetIO().DeltaTime;
    t  += ((*v  ? 1.f : 0.f) - t)  * ImMin(1.f, dt * 12.f);
    ht += ((hov ? 1.f : 0.f) - ht) * ImMin(1.f, dt * 10.f);
    st->SetFloat(id,          t);
    st->SetFloat(id + 0x9000u, ht);

    // Hover row bg
    if (ht > 0.01f)
        dl->AddRectFilled(pos, ImVec2(pos.x + avail, pos.y + rowH),
            IM_COL32(255,255,255, (int)(ht * 8)), 3.f);

    // Label -- interpolates dim (#a4a4ab) -> bright (#dcdcdc)
    int lv = 164 + (int)(t * 56.f);
    dl->AddText(ImVec2(pos.x + 4.f, pos.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f),
        IM_COL32(lv, lv, lv + 7, 255), labelAndId, dispEnd);

    // Circle
    float cx = pos.x + avail - circR - 8.f;
    float cy = pos.y + rowH * 0.5f;
    int   acR = (int)(g_accentColor[0] * 255);
    int   acG = (int)(g_accentColor[1] * 255);
    int   acB = (int)(g_accentColor[2] * 255);

    // Glow ring behind filled circle
    if (t > 0.01f)
        dl->AddCircleFilled(ImVec2(cx, cy), circR + 3.f,
            IM_COL32(acR, acG, acB, (int)(t * 35)));
    // Filled body (fades in)
    if (t > 0.01f)
        dl->AddCircleFilled(ImVec2(cx, cy), circR,
            IM_COL32(acR, acG, acB, (int)(t * 255)));
    // Outline -- accent when on, grey when off
    dl->AddCircle(ImVec2(cx, cy), circR,
        t > 0.5f ? IM_COL32(acR, acG, acB, 255)
                 : IM_COL32(90, 90, 96, (int)(200 - t * 100)),
        0, 1.4f);

    return pressed;
}

// Flat filled-track slider (float). Label left, right-aligned value text, click to type, full-width track.
static bool MenuSliderF(const char* labelAndId, float* v, float vmin, float vmax,
                        const char* fmt = "%.2f", bool clampOnEdit = true)
{
    const char* hash    = strstr(labelAndId, "##");
    const char* dispEnd = hash ? hash : labelAndId + strlen(labelAndId);

    const float rowH   = 36.f;
    const float trackH = 3.f;
    const float grabR  = 5.f;
    const float inputW = 64.f;  // width of the inline edit input

    ImVec2 pos   = ImGui::GetCursorScreenPos();
    float  avail = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    char valBuf[64];
    snprintf(valBuf, sizeof(valBuf), fmt, *v);
    ImVec2 valSz = ImGui::CalcTextSize(valBuf);

    ImGuiID id = ImGui::GetID(labelAndId);
    ImRect  bb(pos, ImVec2(pos.x + avail, pos.y + rowH));
    ImGui::ItemSize(bb);
    if (!ImGui::ItemAdd(bb, id)) return false;

    ImVec2 savedCursorPos = ImGui::GetCursorPos();

    ImGuiStorage* st = ImGui::GetStateStorage();
    bool editing   = st->GetFloat(id + 0xB000u, 0.f) > 0.5f;
    bool needFocus = st->GetFloat(id + 0xC000u, 0.f) > 0.5f;

    // Hover animate
    float ht = st->GetFloat(id + 0xA000u, 0.f);
    float dt = ImGui::GetIO().DeltaTime;
    bool  hov = ImGui::IsItemHovered();
    ht += ((hov ? 1.f : 0.f) - ht) * ImMin(1.f, dt * 10.f);
    st->SetFloat(id + 0xA000u, ht);

    // Value text click zone: right ~inputW px in the top text row
    float textRowH = rowH * 0.5f; // approx height of top label zone
    ImVec2 vtClickMin(pos.x + avail - inputW, pos.y);
    ImVec2 vtClickMax(pos.x + avail,          pos.y + textRowH);
    bool vtHov     = ImGui::IsMouseHoveringRect(vtClickMin, vtClickMax);
    bool vtClicked = vtHov && ImGui::IsMouseClicked(ImGuiMouseButton_Left);

    if (vtClicked && !editing) {
        st->SetFloat(id + 0xB000u, 1.f);
        st->SetFloat(id + 0xC000u, 1.f);
        editing = true; needFocus = true;
    }
    if (editing && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        st->SetFloat(id + 0xB000u, 0.f);
        editing = false;
    }

    // Slider drag (full width track, only when not editing, not clicking value zone)
    const float trackAreaW = avail;
    bool active  = (ImGui::GetActiveID() == id);
    bool changed = false;
    if (!editing) {
        bool sliderHov = hov && !vtHov;
        if (sliderHov && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            ImGui::SetActiveID(id, ImGui::GetCurrentWindow());
            ImGui::FocusWindow(ImGui::GetCurrentWindow());
        }
        if (active) {
            float t = ImClamp((ImGui::GetIO().MousePos.x - pos.x) / trackAreaW, 0.f, 1.f);
            float nv = vmin + t * (vmax - vmin);
            if (nv != *v) { *v = nv; changed = true; }
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) ImGui::ClearActiveID();
        }
    }

    float pct = ImClamp((*v - vmin) / (vmax - vmin), 0.f, 1.f);
    int   acR = (int)(g_accentColor[0] * 255);
    int   acG = (int)(g_accentColor[1] * 255);
    int   acB = (int)(g_accentColor[2] * 255);

    // Label (left side, top)
    float textY = pos.y + 6.f;
    dl->AddText(ImVec2(pos.x + 4.f, textY), IM_COL32(164,164,171,255), labelAndId, dispEnd);

    // Value text right-aligned (plain, no box) -- slightly brighter on hover
    if (!editing) {
        ImU32 valCol = (hov || vtHov) ? IM_COL32(200,200,207,255) : IM_COL32(126,126,132,255);
        float vtx = pos.x + avail - valSz.x - 4.f;
        float vty = pos.y + 6.f;
        dl->AddText(ImVec2(vtx, vty), valCol, valBuf);
    }

    // Full-width track
    float trackY = pos.y + rowH - trackH - 8.f;
    ImVec2 tMin(pos.x, trackY), tMax(pos.x + trackAreaW, trackY + trackH);
    dl->AddRectFilled(tMin, tMax, IM_COL32(255,255,255,13), 2.f);
    if (pct > 0.001f)
        dl->AddRectFilled(tMin, ImVec2(tMin.x + trackAreaW * pct, tMax.y),
            IM_COL32(acR, acG, acB, active ? 230 : (int)(155 + ht * 65)), 2.f);

    // Grab circle
    float grabX = pos.x + trackAreaW * pct;
    float grabY = trackY + trackH * 0.5f;
    dl->AddCircleFilled(ImVec2(grabX, grabY), grabR + (active ? 1.f : 0.f),
        IM_COL32(acR, acG, acB, 255));

    // Edit mode: render InputFloat inline at value text position
    if (editing) {
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(255,255,255,10));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.f, 1.f));
        ImGui::SetCursorScreenPos(ImVec2(pos.x + avail - inputW, pos.y + 2.f));
        ImGui::SetNextItemWidth(inputW);
        if (needFocus) { ImGui::SetKeyboardFocusHere(); st->SetFloat(id + 0xC000u, 0.f); }
        char inputId[80]; snprintf(inputId, sizeof(inputId), "##slvf%u", (unsigned)id);
        bool inputDone = ImGui::InputFloat(inputId, v, 0.f, 0.f, fmt,
            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        if (inputDone) {
            if (clampOnEdit) *v = ImClamp(*v, vmin, vmax); changed = true;
            st->SetFloat(id + 0xB000u, 0.f); editing = false;
        }
        if (!needFocus && !ImGui::IsItemActive()) {
            if (clampOnEdit) *v = ImClamp(*v, vmin, vmax);
            st->SetFloat(id + 0xB000u, 0.f);
        }
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
        ImGui::SetCursorPos(savedCursorPos);
    }

    return changed;
}

// Int slider with click-to-type inline edit. Full-width track, right-aligned value text (no box).
static bool MenuSliderI(const char* labelAndId, int* v, int vmin, int vmax,
                        const char* fmt = "%d", bool clampOnEdit = true)
{
    const char* hash    = strstr(labelAndId, "##");
    const char* dispEnd = hash ? hash : labelAndId + strlen(labelAndId);

    const float rowH   = 36.f;
    const float trackH = 3.f;
    const float grabR  = 5.f;
    const float inputW = 64.f;

    ImVec2 pos   = ImGui::GetCursorScreenPos();
    float  avail = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    char valBuf[64];
    snprintf(valBuf, sizeof(valBuf), fmt, *v);
    ImVec2 valSz = ImGui::CalcTextSize(valBuf);

    ImGuiID id = ImGui::GetID(labelAndId);
    ImRect  bb(pos, ImVec2(pos.x + avail, pos.y + rowH));
    ImGui::ItemSize(bb);
    if (!ImGui::ItemAdd(bb, id)) return false;

    ImVec2 savedCursorPos = ImGui::GetCursorPos();

    ImGuiStorage* st = ImGui::GetStateStorage();
    bool editing   = st->GetFloat(id + 0xB000u, 0.f) > 0.5f;
    bool needFocus = st->GetFloat(id + 0xC000u, 0.f) > 0.5f;

    float ht = st->GetFloat(id + 0xA000u, 0.f);
    float dt = ImGui::GetIO().DeltaTime;
    bool  hov = ImGui::IsItemHovered();
    ht += ((hov ? 1.f : 0.f) - ht) * ImMin(1.f, dt * 10.f);
    st->SetFloat(id + 0xA000u, ht);

    // Value text click zone: right ~inputW px in the top text row
    float textRowH = rowH * 0.5f;
    ImVec2 vtClickMin(pos.x + avail - inputW, pos.y);
    ImVec2 vtClickMax(pos.x + avail,          pos.y + textRowH);
    bool vtHov     = ImGui::IsMouseHoveringRect(vtClickMin, vtClickMax);
    bool vtClicked = vtHov && ImGui::IsMouseClicked(ImGuiMouseButton_Left);

    if (vtClicked && !editing) {
        st->SetFloat(id + 0xB000u, 1.f); st->SetFloat(id + 0xC000u, 1.f);
        editing = true; needFocus = true;
    }
    if (editing && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        st->SetFloat(id + 0xB000u, 0.f); editing = false;
    }

    const float trackAreaW = avail;
    bool active  = (ImGui::GetActiveID() == id);
    bool changed = false;
    if (!editing) {
        bool sliderHov = hov && !vtHov;
        if (sliderHov && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            ImGui::SetActiveID(id, ImGui::GetCurrentWindow());
            ImGui::FocusWindow(ImGui::GetCurrentWindow());
        }
        if (active) {
            float t  = ImClamp((ImGui::GetIO().MousePos.x - pos.x) / trackAreaW, 0.f, 1.f);
            int   nv = vmin + (int)(t * (vmax - vmin) + 0.5f);
            if (nv != *v) { *v = nv; changed = true; }
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) ImGui::ClearActiveID();
        }
    }

    float pct = ImClamp((float)(*v - vmin) / (float)(vmax - vmin), 0.f, 1.f);
    int   acR = (int)(g_accentColor[0] * 255);
    int   acG = (int)(g_accentColor[1] * 255);
    int   acB = (int)(g_accentColor[2] * 255);

    // Label
    float textY = pos.y + 6.f;
    dl->AddText(ImVec2(pos.x + 4.f, textY), IM_COL32(164,164,171,255), labelAndId, dispEnd);

    // Value text right-aligned (no box), brighter on hover
    if (!editing) {
        ImU32 valCol = (hov || vtHov) ? IM_COL32(200,200,207,255) : IM_COL32(126,126,132,255);
        float vtx = pos.x + avail - valSz.x - 4.f;
        float vty = pos.y + 6.f;
        dl->AddText(ImVec2(vtx, vty), valCol, valBuf);
    }

    // Full-width track
    float trackY = pos.y + rowH - trackH - 8.f;
    ImVec2 tMin(pos.x, trackY), tMax(pos.x + trackAreaW, trackY + trackH);
    dl->AddRectFilled(tMin, tMax, IM_COL32(255,255,255,13), 2.f);
    if (pct > 0.001f)
        dl->AddRectFilled(tMin, ImVec2(tMin.x + trackAreaW * pct, tMax.y),
            IM_COL32(acR, acG, acB, active ? 230 : (int)(155 + ht * 65)), 2.f);

    float grabX = pos.x + trackAreaW * pct;
    float grabY = trackY + trackH * 0.5f;
    dl->AddCircleFilled(ImVec2(grabX, grabY), grabR + (active ? 1.f : 0.f),
        IM_COL32(acR, acG, acB, 255));

    if (editing) {
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(255,255,255,10));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.f, 1.f));
        ImGui::SetCursorScreenPos(ImVec2(pos.x + avail - inputW, pos.y + 2.f));
        ImGui::SetNextItemWidth(inputW);
        if (needFocus) { ImGui::SetKeyboardFocusHere(); st->SetFloat(id + 0xC000u, 0.f); }
        char inputId[80]; snprintf(inputId, sizeof(inputId), "##slvi%u", (unsigned)id);
        bool inputDone = ImGui::InputInt(inputId, v, 0, 0,
            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        if (inputDone) {
            if (clampOnEdit) *v = ImClamp(*v, vmin, vmax); changed = true;
            st->SetFloat(id + 0xB000u, 0.f); editing = false;
        }
        if (!needFocus && !ImGui::IsItemActive()) {
            if (clampOnEdit) *v = ImClamp(*v, vmin, vmax);
            st->SetFloat(id + 0xB000u, 0.f);
        }
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
        ImGui::SetCursorPos(savedCursorPos);
    }

    return changed;
}

// Styled collapsible section header. Returns true while expanded. State persists per-ID via ImGuiStorage.
static bool MenuDropdown(const char* labelAndId)
{
    const char* hash    = strstr(labelAndId, "##");
    const char* dispEnd = hash ? hash : labelAndId + strlen(labelAndId);

    const float rowH     = 26.f;
    const float rounding = 5.f;

    ImVec2      pos   = ImGui::GetCursorScreenPos();
    float       avail = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl    = ImGui::GetWindowDrawList();

    ImGuiID id = ImGui::GetID(labelAndId);
    ImRect  bb(pos, ImVec2(pos.x + avail, pos.y + rowH));
    ImGui::ItemSize(bb);

    ImGuiStorage* st   = ImGui::GetStateStorage();
    bool          open = st->GetFloat(id, 0.f) > 0.5f;

    // When the header is scrolled off-screen, ItemAdd returns false.
    // Return the stored open state (not false) so callers still lay out
    // their content — preserving the correct ContentSize and scroll range.
    if (!ImGui::ItemAdd(bb, id)) return open;

    bool hov, held;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hov, &held);
    if (pressed) { open = !open; st->SetFloat(id, open ? 1.f : 0.f); }

    float ht = st->GetFloat(id + 0xE000u, 0.f);
    float dt = ImGui::GetIO().DeltaTime;
    ht += ((hov ? 1.f : 0.f) - ht) * ImMin(1.f, dt * 10.f);
    st->SetFloat(id + 0xE000u, ht);

    // Background — subtle tinted pill with rounded corners
    int bgAlpha = open ? 20 : (int)(10 + ht * 8);
    dl->AddRectFilled(pos, ImVec2(pos.x + avail, pos.y + rowH),
        IM_COL32(255, 255, 255, bgAlpha), rounding);
    // Border
    dl->AddRect(pos, ImVec2(pos.x + avail, pos.y + rowH),
        IM_COL32(255, 255, 255, (int)(18 + ht * 14)), rounding, 0, 1.f);
    // Accent left bar (when open)
    if (open) {
        int acR = (int)(g_accentColor[0] * 255);
        int acG = (int)(g_accentColor[1] * 255);
        int acB = (int)(g_accentColor[2] * 255);
        dl->AddRectFilled(
            ImVec2(pos.x + 2.f, pos.y + 5.f),
            ImVec2(pos.x + 4.f, pos.y + rowH - 5.f),
            IM_COL32(acR, acG, acB, 200), 1.f);
    }
    // Label
    float textH = ImGui::GetTextLineHeight();
    float textY = pos.y + (rowH - textH) * 0.5f;
    int   lv    = open ? 210 : (int)(164 + ht * 30);
    dl->AddText(ImVec2(pos.x + 10.f, textY),
        IM_COL32(lv, lv, lv + 4, 255), labelAndId, dispEnd);
    // Chevron (▸ right = closed, ▾ down = open)
    float cx      = pos.x + avail - 14.f;
    float cy      = pos.y + rowH * 0.5f;
    ImU32 chevCol = IM_COL32(180, 180, 188, (int)(140 + ht * 80));
    if (open)
        dl->AddTriangleFilled(
            ImVec2(cx - 4.f, cy - 2.f), ImVec2(cx + 4.f, cy - 2.f),
            ImVec2(cx, cy + 3.f), chevCol);
    else
        dl->AddTriangleFilled(
            ImVec2(cx - 2.f, cy - 4.f), ImVec2(cx - 2.f, cy + 4.f),
            ImVec2(cx + 3.f, cy), chevCol);

    return open;
}

// Row of equal-width buttons, one per value; the current one is highlighted.
static bool MenuSegment(const char* id, int* v, const char* const* labels, int n, int first = 0)
{
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float bw  = (ImGui::GetContentRegionAvail().x - gap * (n - 1)) / n;
    bool changed = false;
    ImGui::PushID(id);
    for (int i = 0; i < n; ++i) {
        if (i) ImGui::SameLine(0, gap);
        const bool active = (*v == first + i);
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.13f, 0.13f, 0.13f, 1.f));
        ImGui::PushID(i);
        if (ImGui::Button(labels[i], ImVec2(bw, 0.f)) && !active) { *v = first + i; changed = true; }
        ImGui::PopID();
        if (active) ImGui::PopStyleColor();
    }
    ImGui::PopID();
    return changed;
}

// Dim caption, then a full-width text field bound to a std::string.
static bool MenuInputString(const char* caption, const char* id, std::string& s)
{
    if (caption) ImGui::TextDisabled("%s", caption);
    char buf[512]{};
    strncpy(buf, s.c_str(), sizeof(buf) - 1);
    ImGui::SetNextItemWidth(-1.f);
    if (!ImGui::InputText(id, buf, sizeof(buf))) return false;
    s = buf;
    return true;
}

// Editable list of fixed-size text entries: one field + remove button per row, add button last.
template <class Entry>
static void MenuEntryList(const char* id, Entry* list, int& count, int& idx, int maxCount, const char* addLabel)
{
    const float gap     = ImGui::GetStyle().ItemSpacing.x;
    const float removeW = 28.f;
    const float fieldW  = ImGui::GetContentRegionAvail().x - removeW - gap;
    ImGui::PushID(id);
    for (int i = 0; i < count; ++i) {
        ImGui::PushID(i);
        ImGui::SetNextItemWidth(fieldW);
        ImGui::InputText("##entry", list[i].text, sizeof(list[i].text));
        ImGui::SameLine(0, gap);
        const bool remove = ImGui::Button("X", ImVec2(removeW, 0.f));
        ImGui::PopID();
        if (remove) {
            for (int j = i; j < count - 1; ++j) list[j] = list[j + 1];
            list[count - 1] = Entry{};
            --count;
            if (idx >= count) idx = 0;
            break;
        }
    }
    if (count < maxCount && ImGui::Button(addLabel, ImVec2(-1.f, 0.f))) list[count++] = Entry{};
    ImGui::PopID();
}

// Searchable font list + style flags (6 = the full set, 4 = bold/mono/sharp/pixel snap).
// Stores the file name only, so configs stay portable.
static void MenuFontPicker(const char* id, int& index, std::string& file, bool* flags, int nFlags)
{
    EnumerateFonts();
    ImGui::PushID(id);

    static std::unordered_map<ImGuiID, std::array<char, 64>> s_filters;
    char* filter = s_filters[ImGui::GetID("##filter")].data();
    ImGui::SetNextItemWidth(-1.f);
    ImGui::InputTextWithHint("##filter", "Search", filter, 64);
    const size_t flen = strlen(filter);
    auto matches = [&](const char* name) { return flen == 0 || _strnicmp(name, filter, flen) == 0; };

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.051f, 0.051f, 0.051f, 1.f));
    ImGui::BeginChild("##list", ImVec2(-1.f, 120.f), false, ImGuiWindowFlags_NoNav);
    if ((matches("default") || matches("segoe")) && ImGui::Selectable("Default (Segoe UI)", index == -1)) {
        index = -1;
        file.clear();
    }
    for (int i = 0; i < (int)g_fontList.size(); ++i) {
        const auto& fe = g_fontList[i];
        if (!matches(fe.displayName.c_str())) continue;
        ImGui::PushID(i);
        if (ImGui::Selectable(fe.displayName.c_str(), index == i)) {
            index = i;
            const size_t sl = fe.fullPath.rfind('\\');
            file = (sl != std::string::npos) ? fe.fullPath.substr(sl + 1) : fe.fullPath;
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();

    static const char* k_flagLabels[6] = { "Bold", "Monochrome", "Sharp", "Pixel snap", "Drop shadow", "Outline" };
    if (ImGui::BeginTable("##flags", 2)) {
        for (int i = 0; i < nFlags; ++i) {
            ImGui::TableNextColumn();
            MenuCheckbox(k_flagLabels[i], &flags[i]);
        }
        ImGui::EndTable();
    }
    ImGui::PopID();
}

// Material layers for a chams target: material combo, color, remove button per layer.
static const char* const k_chamMaterials[] = {
    "Original", "Flat", "Shaded", "Wireframe", "Fresnel", "Shine", "Tint", "None" };

static void MenuChamLayers(const char* labelAndId, bool& enabled, int& count, VmChamLayer_t* layers,
                           bool invertOption)
{
    MenuCheckbox(labelAndId, &enabled);
    if (!enabled) return;

    ImGui::PushID(labelAndId);
    const float gap    = ImGui::GetStyle().ItemSpacing.x;
    const float sw     = ImGui::GetFrameHeight();
    const float comboW = ImGui::GetContentRegionAvail().x - sw * 2.f - gap * 2.f;
    for (int i = 0; i < count; ++i)
    {
        VmChamLayer_t& L = layers[i];
        ImGui::PushID(i);
        const int mat = (L.matType >= 0 && L.matType < IM_ARRAYSIZE(k_chamMaterials)) ? L.matType : 0;
        ImGui::SetNextItemWidth(comboW);
        if (ImGui::BeginCombo("##mat", k_chamMaterials[mat])) {
            for (int m = 0; m < IM_ARRAYSIZE(k_chamMaterials); ++m)
                if (ImGui::Selectable(k_chamMaterials[m], mat == m)) L.matType = m;
            ImGui::EndCombo();
        }
        ImGui::SameLine(0, gap);
        ImGui::BeginDisabled(L.useSheen);
        ColorPickerPopup(L.color, 4, "##col");
        ImGui::EndDisabled();
        ImGui::SameLine(0, gap);
        ImGui::BeginDisabled(count <= 1);
        const bool remove = ImGui::Button("X", ImVec2(sw, sw));
        ImGui::EndDisabled();
        MenuCheckbox("Use sheen color##sheen", &L.useSheen);
        if (invertOption) MenuCheckbox("Invert normals##ignorez", &L.ignorez);
        ImGui::PopID();
        if (remove) {
            for (int j = i; j < count - 1; ++j) layers[j] = layers[j + 1];
            --count;
            break;
        }
    }
    if (count < k_vmMaxLayers && ImGui::Button("+ Add layer", ImVec2(-1.f, 0.f)))
        layers[count++] = VmChamLayer_t{};
    ImGui::PopID();
    ImGui::Spacing();
}

// Swatch + label pairs laid out in a grid, `columns` per row.
struct MenuColor { const char* label; float* col; int channels; };
static void MenuColorGrid(const char* id, std::initializer_list<MenuColor> colors, int columns = 2)
{
    if (!ImGui::BeginTable(id, columns)) return;
    for (const MenuColor& c : colors) {
        ImGui::TableNextColumn();
        ImGui::PushID(c.label);
        ColorPickerPopup(c.col, c.channels, "##c");
        ImGui::PopID();
        ImGui::SameLine();
        ImGui::TextDisabled("%s", c.label);
    }
    ImGui::EndTable();
}

// Six-way screen anchor: top left .. bottom right, as two rows of three.
static void MenuAnchor6(const char* id, int* pos)
{
    static const char* const k_top[] = { "Top L", "Top", "Top R" };
    static const char* const k_bot[] = { "Bot L", "Bottom", "Bot R" };
    ImGui::PushID(id);
    MenuSegment("##top", pos, k_top, 3, 0);
    MenuSegment("##bot", pos, k_bot, 3, 3);
    ImGui::PopID();
}

// Toggles for bit flags in a 2-column grid. labels[i] owns bit (1 << bits[i]).
static void MenuFlagGrid(const char* id, int& flags, const char* const* labels, const int* bits, int n)
{
    if (!ImGui::BeginTable(id, 2)) return;
    ImGui::PushID(id);
    for (int i = 0; i < n; ++i) {
        ImGui::TableNextColumn();
        bool on = (flags & (1 << bits[i])) != 0;
        if (MenuCheckbox(labels[i], &on)) flags = on ? (flags | (1 << bits[i])) : (flags & ~(1 << bits[i]));
    }
    ImGui::PopID();
    ImGui::EndTable();
}

// Stacked outline layers for a HUD box: first layer always present, extra ones removable.
static void MenuOutlineLayers(const char* id, OutlineLayer* layers, int& count)
{
    static const char* const k_sides[] = { "Top", "Right", "Bottom", "Left" };
    static const int k_sideBits[] = { 0, 1, 2, 3 };
    ImGui::PushID(id);
    for (int i = 0; i < count; ++i) {
        OutlineLayer& L = layers[i];
        ImGui::PushID(i);
        char label[32];
        snprintf(label, sizeof(label), i == 0 ? "Outline##en" : "Outline %d##en", i + 1);
        MenuCheckbox(label, &L.enable);
        bool remove = false;
        if (L.enable) {
            MenuSliderF("Thickness##t", &L.thick, 0.5f, 12.f, "%.1f");
            MenuSliderF("Blur##bl",     &L.blur,  0.f,  40.f, "%.0f");
            MenuColorGrid("##col", { { "Color", L.color, 4 } });
            ImGui::TextDisabled("Sides (none = all)");
            MenuFlagGrid("##sides", L.sideFlags, k_sides, k_sideBits, 4);
        }
        if (i > 0) remove = ImGui::Button("Remove outline", ImVec2(-1.f, 0.f));
        ImGui::PopID();
        if (remove) {
            for (int j = i; j < count - 1; ++j) layers[j] = layers[j + 1];
            --count;
            break;
        }
    }
    if (count < k_maxOutlineLayers && ImGui::Button("+ Add outline", ImVec2(-1.f, 0.f))) {
        OutlineLayer& L = layers[count++];
        L = OutlineLayer{};
        L.enable = true;
        L.color[0] = L.color[1] = L.color[2] = 0.f;
        L.color[3] = 0.6f;
    }
    ImGui::PopID();
}

// Per-side fades of a HUD box background, sides indexed L, R, T, B.
static void MenuSideGradients(const char* id, BgSideGrad* grads)
{
    static const char* const k_sides[] = { "Left fade", "Right fade", "Top fade", "Bottom fade" };
    ImGui::PushID(id);
    for (int i = 0; i < 4; ++i) {
        ImGui::PushID(i);
        MenuCheckbox(k_sides[i], &grads[i].enable);
        if (grads[i].enable) {
            MenuSliderF("Fade start##s", &grads[i].start, 0.f, 1.f, "%.2f");
            MenuSliderF("Hard edge##e",  &grads[i].end,   0.f, 1.f, "%.2f");
        }
        ImGui::PopID();
    }
    ImGui::PopID();
}

// Background corners that get rounded (none ticked = all), bits TL 0, TR 1, BL 2, BR 3.
static void MenuCornerFlags(const char* id, int& flags)
{
    static const char* const k_corners[] = { "Top left", "Top right", "Bottom left", "Bottom right" };
    static const int k_cornerBits[] = { 0, 1, 2, 3 };
    ImGui::TextDisabled("Rounded corners (none = all)");
    MenuFlagGrid(id, flags, k_corners, k_cornerBits, 4);
}

static void MenuBlob(const char* id, bool& enable, bool* useSheen, float* color3,
                     float& alpha, float& size, float& speed)
{
    ImGui::PushID(id);
    MenuCheckbox("Gradient blob##en", &enable);
    if (enable) {
        if (useSheen) MenuCheckbox("Use sheen color##sheen", useSheen);
        if (!useSheen || !*useSheen) MenuColorGrid("##col", { { "Blob color", color3, 3 } });
        MenuSliderF("Opacity##a", &alpha, 0.f,  1.f, "%.2f");
        MenuSliderF("Spread##s",  &size,  0.1f, 2.f, "%.2f");
        MenuSliderF("Speed##v",   &speed, 0.1f, 5.f, "%.2f");
    }
    ImGui::PopID();
}

static void MenuPrecip(const char* id, bool& enable, int& mode, int& count, float& speed,
                       float& size, float& wind, float* color4)
{
    ImGui::PushID(id);
    MenuCheckbox("Precipitation##en", &enable);
    if (enable) {
        static const char* const k_modes[] = { "Rain", "Snow" };
        MenuSegment("##mode", &mode, k_modes, 2);
        MenuSliderI("Amount##n", &count, 1, 200);
        MenuSliderF("Speed##v",  &speed, 0.1f, 5.f, "%.2f");
        MenuSliderF("Size##s",   &size,  0.1f, 5.f, "%.2f");
        MenuSliderF("Wind##w",   &wind, -1.f,  1.f, "%.2f");
        MenuColorGrid("##col", { { "Color", color4, 4 } });
    }
    ImGui::PopID();
}

static void MenuBoxShadow(const char* id, bool& enable, float& offX, float& offY, float& blur, float* color4)
{
    ImGui::PushID(id);
    MenuCheckbox("Box shadow##en", &enable);
    if (enable) {
        MenuSliderF("Offset X##x", &offX, -30.f, 30.f, "%.1f");
        MenuSliderF("Offset Y##y", &offY, -30.f, 30.f, "%.1f");
        MenuSliderF("Blur##b",     &blur,   0.f, 40.f, "%.1f");
        MenuColorGrid("##col", { { "Shadow color", color4, 4 } });
    }
    ImGui::PopID();
}

static void MenuInsets(const char* id, float& l, float& r, float& t, float& b)
{
    ImGui::PushID(id);
    MenuSliderF("Inset left##l",   &l, -50.f, 200.f, "%.0f px");
    MenuSliderF("Inset right##r",  &r, -50.f, 200.f, "%.0f px");
    MenuSliderF("Inset top##t",    &t, -50.f, 200.f, "%.0f px");
    MenuSliderF("Inset bottom##b", &b, -50.f, 200.f, "%.0f px");
    ImGui::PopID();
}

// ---- Open/close fade -----------------------------------------
// 0.1 s each way. Scaling each vertex's alpha fades every layer on its own, so
// opaque things stacked on the see-through panel (images, dark section lines)
// look solid while the panel is still a ghost. So while fading, the menu's draw
// lists render into an offscreen target that is composited once at the fade
// alpha. Vertex scaling is only the fallback when that target can't be made.
static float s_menuFade = 0.f;
static constexpr const char* k_menuWindow = "infovisual##main";

float Menu_Alpha()   { return s_menuFade; }
bool  Menu_Visible() { return g_showMenu || s_menuFade > 0.f; }

// ParentWindow, not RootWindowPopupTree: in 1.90 that one only links popups, so
// child windows (##left/##right and everything in them) point at themselves.
// ParentWindow covers children and popups opened from inside them.
static bool MenuInTree(ImGuiWindow* w, ImGuiWindow* root)
{
    if (!w->Active) return false;
    for (ImGuiWindow* p = w; p; p = p->ParentWindow)
        if (p == root) return true;
    return false;
}

// Minesweeper and the web panel chrome are their own top-level windows but open and
// close with the menu. (The web panel's chromeless "##websurf_bare" is not in the set.)
struct MenuSide { ImGuiWindow* ms; ImGuiWindow* web; };
static MenuSide MenuSideWindows()
{
    return { ImGui::FindWindowByName("Minesweeper##ms"), ImGui::FindWindowByName("Web##websurf") };
}
static bool MenuInFade(ImGuiWindow* w, ImGuiWindow* root, const MenuSide& s)
{
    return MenuInTree(w, root) || (s.ms && MenuInTree(w, s.ms)) || (s.web && MenuInTree(w, s.web));
}

static void MenuApplyFade(float a)
{
    ImGuiWindow* root = ImGui::FindWindowByName(k_menuWindow);
    if (!root) return;
    const MenuSide ms = MenuSideWindows();
    for (ImGuiWindow* w : GImGui->Windows) {
        if (!MenuInFade(w, root, ms)) continue;
        for (ImDrawVert& v : w->DrawList->VtxBuffer) {
            const ImU32 alpha = (ImU32)(((v.col >> IM_COL32_A_SHIFT) & 0xFF) * a + 0.5f);
            v.col = (v.col & ~IM_COL32_A_MASK) | (alpha << IM_COL32_A_SHIFT);
        }
    }
}

// Backbuffer-sized A8R8G8B8 target. D3DPOOL_DEFAULT: released on reset and eject.
static IDirect3DTexture9* s_layerTex  = nullptr;
static IDirect3DSurface9* s_layerSurf = nullptr;
static UINT               s_layerW = 0, s_layerH = 0;

void Menu_ReleaseLayer()
{
    if (s_layerSurf) { s_layerSurf->Release(); s_layerSurf = nullptr; }
    if (s_layerTex)  { s_layerTex->Release();  s_layerTex  = nullptr; }
    s_layerW = s_layerH = 0;
}

static bool MenuLayer_Ensure(IDirect3DDevice9* dev, UINT w, UINT h)
{
    if (s_layerSurf && s_layerW == w && s_layerH == h) return true;
    Menu_ReleaseLayer();
    if (FAILED(dev->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8,
                                  D3DPOOL_DEFAULT, &s_layerTex, nullptr)) || !s_layerTex) {
        DebugLog("menu layer: CreateTexture %ux%u failed", w, h);
        s_layerTex = nullptr;
        return false;
    }
    if (FAILED(s_layerTex->GetSurfaceLevel(0, &s_layerSurf))) { Menu_ReleaseLayer(); return false; }
    s_layerW = w; s_layerH = h;
    return true;
}

// Draws the layer over the current target. ImGui's blend (color SRCALPHA, alpha
// ONE/INVSRCALPHA) onto a zero-cleared target leaves premultiplied color, so
// ONE/INVSRCALPHA with texture*a is exactly "whole menu at opacity a".
static void MenuLayer_Composite(IDirect3DDevice9* dev, float a)
{
    const DWORD a8 = (DWORD)(a * 255.f + 0.5f);
    const D3DCOLOR col = D3DCOLOR_ARGB(a8, a8, a8, a8);
    const float x1 = (float)s_layerW - 0.5f, y1 = (float)s_layerH - 0.5f;
    struct V { float x, y, z, rhw; D3DCOLOR c; float u, v; };
    const V quad[4] = {
        { -0.5f, -0.5f, 0.f, 1.f, col, 0.f, 0.f },
        {  x1,   -0.5f, 0.f, 1.f, col, 1.f, 0.f },
        { -0.5f,  y1,   0.f, 1.f, col, 0.f, 1.f },
        {  x1,    y1,   0.f, 1.f, col, 1.f, 1.f },
    };
    dev->SetPixelShader(nullptr);
    dev->SetVertexShader(nullptr);
    dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
    dev->SetRenderState(D3DRS_ZENABLE, FALSE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    dev->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    dev->SetTextureStageState(0, D3DTSS_COLOROP,   D3DTOP_MODULATE);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP,   D3DTOP_MODULATE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    dev->SetTextureStageState(1, D3DTSS_COLOROP,   D3DTOP_DISABLE);
    dev->SetTextureStageState(1, D3DTSS_ALPHAOP,   D3DTOP_DISABLE);
    dev->SetTexture(0, s_layerTex);
    // TF2's wrapper turns sRGB decode back on inside SetTexture, so this goes after it.
    dev->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, 0);
    dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSU,  D3DTADDRESS_CLAMP);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSV,  D3DTADDRESS_CLAMP);
    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(V));
}

static void MenuDrawDataInit(ImDrawData& out, const ImDrawData& src)
{
    out.Valid            = src.Valid;
    out.DisplayPos       = src.DisplayPos;
    out.DisplaySize      = src.DisplaySize;
    out.FramebufferScale = src.FramebufferScale;
    out.OwnerViewport    = src.OwnerViewport;
}

static void MenuDrawDataAdd(ImDrawData& out, ImDrawList* dl)
{
    out.CmdLists.push_back(dl);
    out.CmdListsCount  = out.CmdLists.Size;
    out.TotalVtxCount += dl->VtxBuffer.Size;
    out.TotalIdxCount += dl->IdxBuffer.Size;
}

void Menu_RenderDrawData(ImDrawData* dd)
{
    const float a = s_menuFade;
    ImGuiWindow* root = (a > 0.f && a < 1.f) ? ImGui::FindWindowByName(k_menuWindow) : nullptr;
    if (!dd || !root || !g_d3dDev) { ImGui_ImplDX9_RenderDrawData(dd); return; }
    IDirect3DDevice9* dev = g_d3dDev;

    // Split in draw order: lists before the menu, the menu tree, everything after.
    // A menu popup that sits apart from the root is pulled up next to it.
    ImVector<ImDrawList*> mine;
    const MenuSide ms = MenuSideWindows();
    for (ImGuiWindow* w : GImGui->Windows)
        if (MenuInFade(w, root, ms)) mine.push_back(w->DrawList);
    ImDrawData before, menu, after;
    MenuDrawDataInit(before, *dd); MenuDrawDataInit(menu, *dd); MenuDrawDataInit(after, *dd);
    for (ImDrawList* dl : dd->CmdLists) {
        if (mine.contains(dl))              MenuDrawDataAdd(menu, dl);
        else if (menu.CmdListsCount == 0)   MenuDrawDataAdd(before, dl);
        else                                MenuDrawDataAdd(after, dl);
    }
    if (menu.CmdListsCount == 0) { ImGui_ImplDX9_RenderDrawData(dd); return; }

    IDirect3DSurface9* rtOld = nullptr;
    D3DSURFACE_DESC    desc{};
    if (FAILED(dev->GetRenderTarget(0, &rtOld)) || !rtOld || FAILED(rtOld->GetDesc(&desc)) ||
        !MenuLayer_Ensure(dev, desc.Width, desc.Height)) {
        if (rtOld) rtOld->Release();
        MenuApplyFade(a);
        ImGui_ImplDX9_RenderDrawData(dd);
        return;
    }
    IDirect3DStateBlock9* sb = nullptr;
    if (FAILED(dev->CreateStateBlock(D3DSBT_ALL, &sb)) || !sb) {
        rtOld->Release();
        MenuApplyFade(a);
        ImGui_ImplDX9_RenderDrawData(dd);
        return;
    }
    sb->Capture();

    if (before.CmdListsCount) ImGui_ImplDX9_RenderDrawData(&before);

    IDirect3DSurface9* dsOld = nullptr;
    dev->GetDepthStencilSurface(&dsOld);   // may legitimately be none
    dev->SetRenderTarget(0, s_layerSurf);  // also resets viewport + scissor to the layer
    dev->SetDepthStencilSurface(nullptr);  // ImGui draws with Z off; avoids size/MSAA mismatch
    dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    // Layer alpha is the coverage the composite divides by; a game-side alpha
    // write mask would leave it 0 and turn the menu additive. Backend never sets it.
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    dev->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.f, 0);
    ImGui_ImplDX9_RenderDrawData(&menu);
    dev->SetRenderTarget(0, rtOld);
    dev->SetDepthStencilSurface(dsOld);
    if (dsOld) dsOld->Release();
    rtOld->Release();

    MenuLayer_Composite(dev, a);
    sb->Apply();
    sb->Release();

    if (after.CmdListsCount) ImGui_ImplDX9_RenderDrawData(&after);
}

static void RenderMenuBody();

// Moving gradient behind the crosshair preview, TF2 world colors. CPU port of a shadertoy: noise-driven
// rotation + sine warp over two blended two-tone gradients. Evaluated on a vertex grid,
// each cell gouraud-shaded; the field is low frequency so 32 cells looks per-pixel.
static void DrawCrosshairPreviewScene(ImDrawList* dl, ImVec2 p0, float side)
{
    static float s_t = 0.f;
    s_t += std::min(ImGui::GetIO().DeltaTime, 0.1f);

    auto fract = [](double x) { return (float)(x - floor(x)); };
    auto S     = [](float a, float b, float x) {   // GLSL smoothstep, reversed edges allowed
        const float t = ImClamp((x - a) / (b - a), 0.f, 1.f);
        return t * t * (3.f - 2.f * t);
    };
    // iq gradient noise (2014), result 0..1
    auto noise = [&](float px, float py) {
        const float ix = floorf(px), iy = floorf(py), fx = px - ix, fy = py - iy;
        const float ux = fx * fx * (3.f - 2.f * fx), uy = fy * fy * (3.f - 2.f * fy);
        auto g = [&](float ox, float oy) {
            const double hx = (ix + ox) * 2127.1 + (iy + oy) * 81.17;
            const double hy = (ix + ox) * 1269.5 + (iy + oy) * 283.37;
            return (fract(sin(hx) * 43758.5453) * 2.f - 1.f) * (fx - ox)
                 + (fract(sin(hy) * 43758.5453) * 2.f - 1.f) * (fy - oy);
        };
        const float n = ImLerp(ImLerp(g(0.f, 0.f), g(1.f, 0.f), ux), ImLerp(g(0.f, 1.f), g(1.f, 1.f), ux), uy);
        return 0.5f + 0.5f * n;
    };

    const float c5 = cosf(-5.f * IM_PI / 180.f), s5 = sinf(-5.f * IM_PI / 180.f);
    auto shade = [&](float u, float v) {   // u,v 0..1, v up (shadertoy uv); box is square so ratio = 1
        float x = u - 0.5f, y = v - 0.5f;
        const float deg = noise(s_t * 0.1f, x * y);
        const float a   = ((deg - 0.5f) * 720.f + 180.f) * IM_PI / 180.f;
        const float c = cosf(a), s = sinf(a);
        const float rx = x * c - y * s, ry = x * s + y * c;   // tuv *= Rot(a)
        x = rx; y = ry;

        const float speed = s_t * 2.f;
        x += sinf(y * 5.f + speed) / 30.f;
        y += sinf(x * 7.5f + speed) / 15.f;

        const float k = S(-0.3f, 0.2f, x * c5 - y * s5);   // (tuv * Rot(-5deg)).x
        const float m = S(0.5f, -0.3f, y);
        // TF2 palette: top = badlands tan -> hazy sky blue, bottom = RED paint -> BLU paint
        const float l1[3] = { ImLerp(0.800f, 0.550f, k), ImLerp(0.660f, 0.700f, k), ImLerp(0.470f, 0.820f, k) };
        const float l2[3] = { ImLerp(0.660f, 0.330f, k), ImLerp(0.250f, 0.450f, k), ImLerp(0.220f, 0.550f, k) };
        return IM_COL32((int)(ImLerp(l1[0], l2[0], m) * 255.f), (int)(ImLerp(l1[1], l2[1], m) * 255.f),
                        (int)(ImLerp(l1[2], l2[2], m) * 255.f), 255);
    };

    enum { N = 32 };
    ImU32 col[N + 1][N + 1];
    for (int j = 0; j <= N; ++j)
        for (int i = 0; i <= N; ++i)
            col[j][i] = shade((float)i / N, 1.f - (float)j / N);   // screen row j, y flipped

    const float cell = side / N;
    for (int j = 0; j < N; ++j)
        for (int i = 0; i < N; ++i)
            dl->AddRectFilledMultiColor(ImVec2(p0.x + i * cell, p0.y + j * cell),
                                        ImVec2(p0.x + (i + 1) * cell, p0.y + (j + 1) * cell),
                                        col[j][i], col[j][i + 1], col[j + 1][i + 1], col[j + 1][i]);
}

void RenderMenu()
{
    const float dt = ImGui::GetIO().DeltaTime;
    s_menuFade = ImClamp(s_menuFade + (g_showMenu ? dt : -dt) / 0.1f, 0.f, 1.f);
    if (s_menuFade <= 0.f) return;

    RenderMenuBody();   // fade applied at draw time, see Menu_RenderDrawData
}

static void RenderMenuBody()
{
    if (g_menuFont && g_menuFont->IsLoaded()) ImGui::PushFont(g_menuFont);

    // ---- Layout constants --------------------------------------
    ImGuiIO& io = ImGui::GetIO();
    const float sidebarW  = 162.f;   // sidebar width
    const float col1W     = 295.f;   // left content column
    const float col2W     = col1W;   // right quick-settings column, same width as left
    const float menuH     = 620.f;   // total window height
    const float totalW    = sidebarW + col1W + col2W;
    const float titleH    = 68.f;    // compact brand area (always same height)
    const float tabH      = 27.f;    // height of each tab button
    const float tabPadX   = 12.f;    // left text indent inside tab

    // ---- Palette -----------------------------------------------
    const ImU32 col_sidebar      = IM_COL32((int)(g_menuBgColor[0]*255),(int)(g_menuBgColor[1]*255),(int)(g_menuBgColor[2]*255), 245);
    const ImU32 col_content      = IM_COL32((int)(g_menuBgColor[0]*255),(int)(g_menuBgColor[1]*255),(int)(g_menuBgColor[2]*255), 238);
    const ImU32 col_divider      = IM_COL32(255, 255, 255,  15); // rgba(255,255,255,0.06)
    const ImU32 col_outer_border = IM_COL32(255, 255, 255,  15); // subtle white border
    const ImU32 col_accent       = IM_COL32((int)(g_accentColor[0]*255), (int)(g_accentColor[1]*255), (int)(g_accentColor[2]*255), 255);
    const ImU32 col_tab_active   = IM_COL32(255, 255, 255,  10); // rgba(255,255,255,0.04)
    const ImU32 col_tab_hover    = IM_COL32(255, 255, 255,   6); // rgba(255,255,255,0.025)
    // Tab text tones ride on the user text color: dim/hover/active are the same
    // hue scaled by the old #808085 / #BDBDC0 / #F0F0F2 ratios vs #DCDCDC.
    const float tcR = g_menuTextColor[0], tcG = g_menuTextColor[1], tcB = g_menuTextColor[2];
    auto TextTone = [&](float m) {
        float r = tcR * m, g = tcG * m, b = tcB * m;
        return ImVec4(r > 1.f ? 1.f : r, g > 1.f ? 1.f : g, b > 1.f ? 1.f : b, 1.f);
    };
    const ImVec4 col_text_dim    = TextTone(0.582f); // inactive
    const ImVec4 col_text_act    = TextTone(1.090f);
    const ImVec4 col_text_inact  = TextTone(0.859f); // hover
    const ImVec4 col_accent_v4   = ImVec4(g_accentColor[0], g_accentColor[1], g_accentColor[2], 1.f);

    // ---- Window setup ------------------------------------------
    ImGui::SetNextWindowSize(ImVec2(totalW, menuH), ImGuiCond_Always);
    ImGui::SetNextWindowPos(
        ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
        ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));

    // Zero padding + no background -- we draw everything ourselves
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 5.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize,  ImVec2(totalW, menuH));
    // Closing: still drawn for the fade, but it takes no clicks.
    ImGui::Begin(k_menuWindow, &g_showMenu,
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoResize   | ImGuiWindowFlags_NoTitleBar  |
        ImGuiWindowFlags_NoBackground |
        (g_showMenu ? 0 : ImGuiWindowFlags_NoInputs));
    ImGui::PopStyleVar(3);

    ImVec2 vWinPos  = ImGui::GetWindowPos();
    ImVec2 vWinSize = ImGui::GetWindowSize();
    auto*  dl       = ImGui::GetWindowDrawList();

    // ---- Draw background ---------------------------------------
    if (g_menuBgBlur && g_d3dDev)
        DrawBgBlurRect(dl, vWinPos.x, vWinPos.y, vWinPos.x + totalW, vWinPos.y + menuH,
            6.f, ImDrawFlags_RoundCornersAll);
    // Content panel (darker base -- fills full rect for rounded corners)
    dl->AddRectFilled(
        vWinPos,
        ImVec2(vWinPos.x + totalW, vWinPos.y + menuH),
        col_content, 6.f, ImDrawFlags_RoundCornersAll);
    // Sidebar overlay (slightly lighter, left-side rounding only)
    dl->AddRectFilled(
        vWinPos,
        ImVec2(vWinPos.x + sidebarW, vWinPos.y + menuH),
        col_sidebar, 6.f, ImDrawFlags_RoundCornersLeft);
    // Outer border
    dl->AddRect(
        vWinPos,
        ImVec2(vWinPos.x + totalW, vWinPos.y + menuH),
        col_outer_border, 6.f, ImDrawFlags_RoundCornersAll, 1.f);
    // Thin divider between sidebar and left column
    dl->AddRectFilled(
        ImVec2(vWinPos.x + sidebarW - 1.f, vWinPos.y),
        ImVec2(vWinPos.x + sidebarW,        vWinPos.y + menuH),
        col_divider);
    // Thin divider between left column and right column
    dl->AddRectFilled(
        ImVec2(vWinPos.x + sidebarW + col1W,        vWinPos.y),
        ImVec2(vWinPos.x + sidebarW + col1W + 1.f,  vWinPos.y + menuH),
        col_divider);

    // ---- Draw sidebar title area (compact: logo + label stacked) ----
    {
        const char* title = "infovisual";
        ImVec2 ts = ImGui::CalcTextSize(title);
        const float logoH = 30.f;
        const float logoW = logoH * kLogoAspect;
        const float logoX = vWinPos.x + (sidebarW - logoW) * 0.5f;
        const float logoY = vWinPos.y + 10.f;
        float tx = vWinPos.x + (sidebarW - ts.x) * 0.5f;
        float ty;
        if (g_logoTex) {
            DrawLogo(dl, logoX, logoY, logoW, logoH,
                IM_COL32((int)(g_accentColor[0]*255),(int)(g_accentColor[1]*255),(int)(g_accentColor[2]*255),220));
            ty = logoY + logoH + 5.f;
        } else {
            ty = vWinPos.y + (titleH - ts.y) * 0.5f;
        }
        dl->AddText(ImVec2(tx, ty), col_accent, title);
        // Separator below title area
        dl->AddRectFilled(
            ImVec2(vWinPos.x,              vWinPos.y + titleH),
            ImVec2(vWinPos.x + sidebarW,   vWinPos.y + titleH + 1.f),
            col_divider);
    }

    // ---- Skins tab shared state (accessed from both left and right columns) ----
    static int  s_skinWpnSel  = 0;
    static int  s_skinWarpSel = 0;
    static int  s_skinLastWpn = -1;
    static char s_skinWpFilter[64] = {};

    // ---- Draw tabs (grouped sidebar) ---------------------------
    static int s_tab = 0;
    struct TabEntry { const char* label; };
    static const TabEntry tabs[] = {
        { "Watermark" }, { "Names" }, { "Sheen" },
        { "Visuals"   }, { "Crosshair" }, { "Misc" }, { "Skins" }, { "Images" }, { "Config" }
    };
    static const int tabCount = 9;
    IDirect3DTexture9* const tabIcons[tabCount] = {
        g_tabIconWatermarkTex, g_tabIconNamesTex, g_tabIconSheenTex,
        g_tabIconVisualsTex,   g_tabIconCrosshairTex, g_tabIconMiscTex,
        g_tabIconSkinsTex,     g_tabIconImagesTex,    g_tabIconConfigTex
    };

    float curY = titleH + 8.f;        // running Y offset from window top

    ImVec2 mouse = ImGui::GetMousePos();

    for (int ti = 0; ti < tabCount; ti++)
    {
        float ty0 = vWinPos.y + curY;
        float ty1 = ty0 + tabH;
        float tx0 = vWinPos.x + 6.f;
        float tx1 = vWinPos.x + sidebarW - 6.f;
        bool  active = (s_tab == ti);
        bool  hov    = (mouse.x >= tx0 && mouse.x < tx1 &&
                        mouse.y >= ty0 && mouse.y < ty1);

        // Background
        if (active) {
            dl->AddRectFilled(ImVec2(tx0, ty0), ImVec2(tx1, ty1), col_tab_active, 4.f, ImDrawFlags_RoundCornersAll);
            // Accent left bar
            dl->AddRectFilled(ImVec2(tx0 + 1.f, ty0 + 6.f), ImVec2(tx0 + 3.f, ty1 - 6.f), col_accent, 1.f);
        } else if (hov)
            dl->AddRectFilled(ImVec2(tx0, ty0), ImVec2(tx1, ty1), col_tab_hover, 4.f, ImDrawFlags_RoundCornersAll);

        // Icon + label
        ImVec4 textCol = active ? col_accent_v4 : (hov ? col_text_inact : col_text_dim);
        float  labelX  = tx0 + tabPadX;
        float  textSzY = ImGui::GetTextLineHeight();
        if (g_menuShowTabIcons && tabIcons[ti]) {
            const float iY = ty0 + (tabH - textSzY) * 0.5f;
            dl->AddImage((ImTextureID)tabIcons[ti],
                ImVec2(labelX, iY), ImVec2(labelX + textSzY, iY + textSzY),
                ImVec2(0,0), ImVec2(1,1),
                ImGui::ColorConvertFloat4ToU32(textCol));
            labelX += textSzY + 5.f;
        }
        float textY2 = ty0 + (tabH - textSzY) * 0.5f;
        dl->AddText(ImVec2(labelX, textY2),
            ImGui::ColorConvertFloat4ToU32(textCol), tabs[ti].label);

        // Click via invisible button (cursor is local to the window)
        ImGui::SetCursorPos(ImVec2(6.f, curY));
        ImGui::InvisibleButton(tabs[ti].label, ImVec2(sidebarW - 12.f, tabH));
        if (ImGui::IsItemClicked()) s_tab = ti;

        curY += tabH + 2.f;
    }

    // ---- Sidebar footer: F11 hint ------------------------------
    {
        const char* hint = "F11 to eject";
        ImVec2 ts = ImGui::CalcTextSize(hint);
        float  tx = vWinPos.x + (sidebarW - ts.x) * 0.5f;
        float  ty = vWinPos.y + menuH - ts.y - 8.f;
        // Separator above footer
        dl->AddRectFilled(
            ImVec2(vWinPos.x,            ty - 6.f),
            ImVec2(vWinPos.x + sidebarW, ty - 5.f),
            col_divider);
        dl->AddText(ImVec2(tx, ty), IM_COL32(60, 60, 60, 255), hint);
    }

    // ---- Left content column -----------------------------------
    ImGui::SetCursorPos(ImVec2(sidebarW, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.f, 14.f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::BeginChild("##left", ImVec2(col1W, menuH), ImGuiChildFlags_AlwaysUseWindowPadding);

    // ---- Tab 0: Watermark --------------------------------------
    if (s_tab == 0)
    {
        MenuSectionLabel("WATERMARK");
        MenuCheckbox("Enable##wmenable", &g_enableOverlay);
        if (g_enableOverlay)
        {
            ImGui::Spacing();
            MenuInputString("Title", "##label", g_customLabel);
            ImGui::Spacing();

            if (MenuDropdown("Title animation##lanim"))
            {
                static const char* k_animModes[] = { "Off", "Scroll", "Typewriter" };
                MenuSegment("##lanimmode", &g_labelAnimMode, k_animModes, 3);
                if (g_labelAnimMode == 1)
                    MenuSliderF("Speed##lscrollspd", &g_labelScrollSpeed, 0.5f, 20.f, "%.1f");
                if (g_labelAnimMode == 2) {
                    MenuSliderF("Type speed##ltypespd",   &g_labelTypeSpeed,  0.5f, 30.f, "%.1f");
                    MenuSliderF("Erase speed##lerasespd", &g_labelEraseSpeed, 0.5f, 30.f, "%.1f");
                    MenuSliderF("Hold##lholdtime",        &g_labelHoldTime,   0.1f, 5.f,  "%.2f s");
                }
                MenuCheckbox("Cycle titles##tcyen", &g_titleCycleEnable);
                if (g_titleCycleEnable) {
                    MenuSliderF("Interval##ltitleint", &g_titleInterval, 0.1f, 30.f, "%.1f s");
                    MenuEntryList("##titles", g_titleList, g_titleCount, g_titleIdx, k_maxTitleCycle, "+ Add title");
                }
                ImGui::Spacing();
            }

            MenuSectionLabel("ELEMENTS");
            MenuCheckbox("Background##wmbg",  &g_drawWatermarkBg);
            MenuCheckbox("Title##wmtitle",    &g_drawWatermarkText);
            MenuCheckbox("Time##wmtime",      &g_drawTime);
            MenuCheckbox("FPS##wmfps",        &g_drawFPS);
            MenuCheckbox("Ping##wmping",      &g_drawPing);
            MenuCheckbox("Loss##wmloss",      &g_drawLoss);
            MenuCheckbox("Username##wmuser",  &g_wmShowUsername);
            if (g_wmShowUsername)
            {
                static const char* k_userSources[] = { "Custom", "In-game", "Windows" };
                MenuSegment("##usrc", &g_wmLabelSource, k_userSources, 3);
                if (g_wmLabelSource == 0)
                    MenuInputString(nullptr, "##userlabel", g_wmUsernameLabel);
                ImGui::Spacing();
            }
            MenuCheckbox("Logo##wmlogo", &g_wmShowLogo);
            MenuCheckbox("Ghost logo behind title##wmlogog", &g_wmLogoGhost);
            if (g_wmLogoGhost)
                MenuSliderF("Ghost opacity##wmlogoga", &g_wmLogoGhostAlpha, 0.f, 1.f, "%.2f");

            MenuSectionLabel("STYLE");
            ImGui::TextDisabled("Separator");
            ImGui::SetNextItemWidth(-1.f);
            ImGui::InputText("##wmsep", g_wmSeparator, sizeof(g_wmSeparator));
            MenuCheckbox("Icons instead of separators##wmuseicons", &g_wmUseIcons);
            MenuSliderF("Size##scale", &g_watermarkScale, 0.5f, 2.5f, "%.2f");
            ImGui::Spacing();

            if (MenuDropdown("Font##wmfont")) {
                MenuFontPicker("##wmfontpick", g_wmFontIndex, g_wmFontFile, g_wmFontFlags, 6);
                ImGui::Spacing();
            }

            if (MenuDropdown("Custom logo image##wmlogoimgtree"))
            {
                const float btnW = 60.f;
                const float gap  = ImGui::GetStyle().ItemSpacing.x;
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - btnW - gap);
                ImGui::InputText("##wmlogopath", g_wmLogoImgPath, MAX_PATH, ImGuiInputTextFlags_ReadOnly);
                ImGui::SameLine(0, gap);
                if (ImGui::Button("Browse##wmlogobr", ImVec2(btnW, 0.f)))
                    FilePick_Start(FILEPICK_LOGO);
                if (g_wmLogoImgPath[0]) {
                    if (ImGui::Button("Reload##wmlogorld")) g_wmLogoImgNeedsLoad = true;
                    ImGui::SameLine();
                    if (ImGui::Button("Clear##wmlogoclr")) {
                        WmLogoImg_ReleaseFrames();
                        g_wmLogoImgPath[0] = '\0';
                    }
                }
                if (!g_wmLogoImgFrames.empty()) {
                    const int fc = (int)g_wmLogoImgFrames.size();
                    ImGui::TextDisabled("%dx%d, %d frame%s", g_wmLogoImgW, g_wmLogoImgH, fc, fc == 1 ? "" : "s");
                } else if (g_wmLogoImgPath[0]) {
                    ImGui::TextDisabled("Not loaded");
                }
                MenuSliderF("Scale##wmlogoscale", &g_wmLogoImgScale, 0.05f, 5.f, "%.2f");
                float rotDeg = g_wmLogoImgRotation * (180.f / 3.14159265f);
                if (MenuSliderF("Rotation##wmlogorot", &rotDeg, -180.f, 180.f, "%.0f deg"))
                    g_wmLogoImgRotation = rotDeg * (3.14159265f / 180.f);
                MenuCheckbox("Tint by title color##wmlogocol", &g_wmLogoImgColorize);
                ImGui::Spacing();
            }
        }
    } // end s_tab == 0

    // ---- Tab 1: Names ------------------------------------------
    if (s_tab == 1)
    {
        MenuSectionLabel("NAMES");
        static const char* k_streamerModes[] = { "Off", "Class", "Team", "All" };
        MenuSegment("##streamermode", &g_streamerMode, k_streamerModes, 4);
        ImGui::TextDisabled(
            g_streamerMode == 0 ? "Real names shown" :
            g_streamerMode == 1 ? "Others shown as their class" :
            g_streamerMode == 2 ? "Others shown as team labels" :
                                  "Everyone shown as one label");
        ImGui::Spacing();

        if (g_streamerMode == 2)
        {
            MenuInputString("Teammate label", "##teamlabel", g_streamerTeamLabel);
            MenuCheckbox("Cycle teammate label##rtlcyn", &g_enableTeamLabelCycle);
            if (g_enableTeamLabelCycle)
                MenuEntryList("##teamlabels", g_teamLabelList, g_teamLabelCount, g_teamLabelIdx, k_maxNames, "+ Add label");
            ImGui::Spacing();
            MenuInputString("Enemy label", "##enemylabel", g_streamerEnemyLabel);
            MenuCheckbox("Cycle enemy label##relcyn", &g_enableEnemyLabelCycle);
            if (g_enableEnemyLabelCycle)
                MenuEntryList("##enemylabels", g_enemyLabelList, g_enemyLabelCount, g_enemyLabelIdx, k_maxNames, "+ Add label");
            ImGui::Spacing();
        }
        else if (g_streamerMode == 3)
        {
            MenuInputString("Everyone label", "##evlabel", g_streamerEveryoneLabel);
            MenuCheckbox("Cycle everyone label##revcyn", &g_enableEveryoneLabelCycle);
            if (g_enableEveryoneLabelCycle)
                MenuEntryList("##evlabels", g_everyoneLabelList, g_everyoneLabelCount, g_everyoneLabelIdx, k_maxNames, "+ Add label");
            ImGui::Spacing();
        }

        MenuSectionLabel("YOUR NAME");
        MenuCheckbox("Custom name##usecustomname", &g_useCustomUsername);
        if (g_useCustomUsername)
        {
            MenuInputString(nullptr, "##customusername", g_customUsername);
            MenuCheckbox("Cycle name##rcyn", &g_enableNameCycle);
            if (g_enableNameCycle)
                MenuEntryList("##names", g_nameList, g_nameCount, g_nameIdx, k_maxNames, "+ Add name");
        }

        // Cycle trigger + interval, shown whenever any cycle is active
        const bool anyCycle = g_enableNameCycle
            || (g_streamerMode == 2 && (g_enableTeamLabelCycle || g_enableEnemyLabelCycle))
            || (g_streamerMode == 3 && g_enableEveryoneLabelCycle);
        if (anyCycle)
        {
            MenuSectionLabel("CYCLE");
            static const char* k_triggers[] = { "Timer", "On kill" };
            MenuSegment("##nametrigger", &g_nameTrigger, k_triggers, 2);
            if (g_nameTrigger == 0) {
                MenuSliderF("Interval##rcynint", &g_nameInterval, 0.1f, 60.f, "%.1f s");
                if (g_enableNameCycle)
                    MenuCheckbox("Show in custom killfeed##kfnamelive", &g_kfNameLive);
            }
        }
    } // end s_tab == 1

    // ---- Tab 2: Weapon Sheen -----------------------------------
    if (s_tab == 2)
    {
        MenuSectionLabel("SHEEN");
        MenuCheckbox("Enable##sheenenable", &g_enableSheen);
        if (g_enableSheen)
        {
            MenuCheckbox("Animated##sheenanim", &g_enableSheenAnim);
            ImGui::Spacing();
            if (g_enableSheenAnim)
            {
                static const char* k_colorCounts[] = { "2 colors", "3 colors", "4 colors" };
                MenuSegment("##sheencount", &g_sheenColorCount, k_colorCounts, 3, 2);

                // one swatch per color, spread over the row
                const int   n     = std::clamp(g_sheenColorCount, 2, 4);
                const float gap   = ImGui::GetStyle().ItemSpacing.x;
                const float colW  = (ImGui::GetContentRegionAvail().x - gap * (n - 1)) / n;
                const float baseX = ImGui::GetCursorPosX();
                static const char* k_pickerIds[4] = { "##sc0", "##sc1", "##sc2", "##sc3" };
                for (int i = 0; i < n; i++) {
                    if (i > 0) ImGui::SameLine(baseX + i * (colW + gap));
                    ColorPickerPopup(g_sheenColors[i], 3, k_pickerIds[i]);
                }
                MenuSliderF("Cycle speed##sheenanimspeed", &g_sheenAnimSpeed, 0.1f, 10.f, "%.1f");
                if (g_sheenAnimSpeed < 0.1f) g_sheenAnimSpeed = 0.1f;

                // live preview bar
                float preview[3];
                GetAnimatedSheenColor(preview);
                const ImVec2 pos   = ImGui::GetCursorScreenPos();
                const float  avail = ImGui::GetContentRegionAvail().x;
                ImGui::GetWindowDrawList()->AddRectFilled(pos, ImVec2(pos.x + avail, pos.y + 14.f),
                    IM_COL32((int)(preview[0] * 255), (int)(preview[1] * 255), (int)(preview[2] * 255), 255), 4.f);
                ImGui::Dummy(ImVec2(avail, 14.f));
            }
            else
            {
                ImGui::TextDisabled("Color");
                ImGui::SameLine();
                ColorPickerPopup(g_sheenColor, 3, "##sheencolor");
            }
        }
    } // end s_tab == 2

    // ---- Tab 3: Visuals ----------------------------------------
    if (s_tab == 3)
    {
        // ---- World -------------------------------------------------
        MenuSectionLabel("WORLD");
        MenuCheckbox("Flip world##flipworld", &g_flipWorld);

        MenuCheckbox("Custom fog##fogenable", &g_enableFog);
        if (g_enableFog)
        {
            const float sw = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;
            MenuCheckbox("Use sheen color##fog", &g_fogUseSheenColor,
                         g_fogUseSheenColor ? 0.f : ImGui::GetContentRegionAvail().x - sw);
            if (!g_fogUseSheenColor) {
                ImGui::SameLine();
                ColorPickerPopup(g_fogColor, 3, "##fogcolor");
            }
            MenuSliderF("Start##fogstart",  &g_fogStart,   -1000.f, 3500.f, "%.0f u");
            MenuSliderF("End##fogend",      &g_fogEnd,     -1000.f, 3500.f, "%.0f u");
            MenuSliderF("Density##fogdens", &g_fogDensity,  0.f,    1.f,    "%.2f");
            MenuCheckbox("Skybox uses world fog##foglink", &g_fogLinkSkybox);
            if (!g_fogLinkSkybox) {
                MenuSliderF("Skybox start##fogskystart",  &g_fogSkyboxStart,   -1000.f, 3500.f, "%.0f u");
                MenuSliderF("Skybox end##fogskyend",      &g_fogSkyboxEnd,     -1000.f, 3500.f, "%.0f u");
                MenuSliderF("Skybox density##fogskyd",    &g_fogSkyboxDensity,  0.f,    1.f,    "%.2f");
            }
            ImGui::Spacing();
        }

        MenuCheckbox("Custom skybox##skyenable", &g_enableSkybox);
        if (g_enableSkybox)
        {
            ImGui::SetNextItemWidth(-1.f);
            ImGui::InputTextWithHint("##skyboxname", "sky name", g_skyboxName, sizeof(g_skyboxName));
            static const char* const k_skyPresets[] = { "sky_tf2_04", "sky_upward", "sky_well_01",
                                                        "sky_dustbowl_01", "sky_goldrush_01", "sky_alpinestorm_01" };
            static const char* const k_skyLabels[]  = { "TF2", "Upward", "Well", "Dustbowl", "Goldrush", "Nightfall" };
            int preset = -1;
            for (int i = 0; i < 6; ++i)
                if (strcmp(g_skyboxName, k_skyPresets[i]) == 0) preset = i;
            const bool row0 = MenuSegment("##skypre0", &preset, k_skyLabels,     3, 0);
            const bool row1 = MenuSegment("##skypre1", &preset, k_skyLabels + 3, 3, 3);
            if (row0 || row1)
                strncpy(g_skyboxName, k_skyPresets[preset], sizeof(g_skyboxName) - 1);
            ImGui::Spacing();
        }

        if (MenuCheckbox("Modulate##modenable", &g_enableModulate)) g_modWorldValuesDirty = true;
        if (g_enableModulate)
        {
            if (ImGui::BeginTable("##modcols", 3)) {
                static const char* const k_modLabels[3] = { "World", "Sky", "Props" };
                float* const cols[3] = { g_modulateWorld, g_modulateSky, g_modulateProps };
                for (int i = 0; i < 3; ++i) {
                    ImGui::TableNextColumn();
                    ImGui::PushID(i);
                    ColorPickerPopup(cols[i], 3, "##mod");
                    ImGui::PopID();
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", k_modLabels[i]);
                }
                ImGui::EndTable();
            }
            // only mark dirty when a value actually changed
            static float s_prev[9] = { -1, -1, -1, -1, -1, -1, -1, -1, -1 };
            float now[9];
            memcpy(now, g_modulateWorld, 12); memcpy(now + 3, g_modulateSky, 12); memcpy(now + 6, g_modulateProps, 12);
            if (memcmp(s_prev, now, sizeof(now)) != 0) {
                g_modWorldValuesDirty = true;
                memcpy(s_prev, now, sizeof(now));
            }
            ImGui::Spacing();
        }

        MenuCheckbox("Map textures##mtenable", &g_enableMapTextures);
        if (g_enableMapTextures)
        {
            const float gap    = ImGui::GetStyle().ItemSpacing.x;
            const float comboW = ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - gap;
            for (int i = 0; i < k_mapTexCatCount; ++i)
            {
                ImGui::PushID(i);
                MapTexCat_t& C = g_mapTexCats[i];
                if (C.preset < 0 || C.preset >= k_mapTexPresetCount) C.preset = 0;

                ImGui::TextDisabled("%s", MapTexCategoryLabel(i));
                ImGui::SetNextItemWidth(comboW);
                if (ImGui::BeginCombo("##mtpreset", MapTexPresetLabel(C.preset))) {
                    for (int p = 0; p < k_mapTexPresetCount; ++p)
                        if (ImGui::Selectable(MapTexPresetLabel(p), C.preset == p)) C.preset = p;
                    ImGui::EndCombo();
                }
                ImGui::SameLine(0, gap);
                ImGui::BeginDisabled(C.preset == 0);
                ColorPickerPopup(C.tint, 4, "##mttint");
                ImGui::EndDisabled();
                if (C.preset == k_mapTexPresetCustom) {
                    ImGui::SetNextItemWidth(comboW);
                    ImGui::InputTextWithHint("##mtcustom", "nature/snow_full001", C.custom, sizeof(C.custom));
                }
                ImGui::PopID();
            }
            if (ImGui::Button("Reset all##mtreset", ImVec2(-1.f, 0.f)))
                for (int i = 0; i < k_mapTexCatCount; ++i) g_mapTexCats[i] = MapTexCat_t{};
            ImGui::Spacing();
        }

        MenuChamLayers("Prop chams##propchams", g_enablePropChams, g_propChamCount, g_propChamLayers, false);

        MenuCheckbox("Weather override##weather", &g_weatherEnabled);
        if (g_weatherEnabled)
        {
            static const char* const k_weatherModes[] = { "Off", "Rain", "Snow", "Custom" };
            MenuSegment("##weathermode", &g_rainMode, k_weatherModes, 4);

            if (g_rainMode == 0)
            {
                ImGui::TextDisabled("Map weather removed");
            }
            else if (g_rainMode == 1)
            {
                MenuSliderF("Alpha##rainalpha",         &g_rainAlpha,   0.f,   1.f,    "%.2f");
                MenuSliderF("Speed##rainspeed",         &g_rainSpeed,   50.f,  2000.f, "%.0f");
                MenuSliderF("Width##rainwidth",         &g_rainWidth,   0.1f,  5.f,    "%.2f");
                MenuSliderF("Length##rainlength",       &g_rainLength,  0.01f, 1.f,    "%.2f");
                MenuSliderF("Radius##rainradius",       &g_rainRadius,  100.f, 4000.f, "%.0f u");
                MenuSliderF("Side velocity##rainside",  &g_rainSideVel, 0.f,   500.f,  "%.0f");
            }
            else if (g_rainMode == 2)
            {
                MenuSliderF("Density##snowdensity",     &g_snowDensity, 0.1f,  1.f,    "%.2f");
                MenuSliderF("Alpha##snowalpha",         &g_snowAlpha,   0.f,   1.f,    "%.2f");
                MenuSliderF("Speed##snowspeed",         &g_snowSpeed,   50.f,  2000.f, "%.0f");
                MenuSliderF("Width##snowwidth",         &g_snowWidth,   0.1f,  5.f,    "%.2f");
                MenuSliderF("Length##snowlength",       &g_snowLength,  0.01f, 1.f,    "%.2f");
                MenuSliderF("Radius##snowradius",       &g_snowRadius,  100.f, 4000.f, "%.0f u");
                MenuSliderF("Side velocity##snowside",  &g_snowSideVel, 0.f,   500.f,  "%.0f");
                ImGui::TextDisabled("Sprite material (Enter to apply)");
                ImGui::SetNextItemWidth(-1.f);
                if (ImGui::InputText("##snowspritename", g_snowSpriteName, sizeof(g_snowSpriteName),
                                     ImGuiInputTextFlags_EnterReturnsTrue))
                    s_snowSpriteDirty = true;
            }
            else if (g_rainMode == 3)
            {
                // custom particle systems (CTE_DispatchEffect, no CPrecipitation)
                MenuSliderF("Radius##pgradius",   &g_particleGridRadius,    500.f,   8000.f, "%.0f u");
                MenuSliderF("Spacing##pgspacing", &g_particleSpacingOffset, -2500.f, 2500.f, "%+.0f u");
                MenuSliderI("Layers##playercount", &g_particleLayerCount,   1, 8);
                ImGui::TextDisabled("Particle per layer (Enter to apply)");
                for (int li = 0; li < g_particleLayerCount && li < 8; ++li)
                {
                    ImGui::PushID(li);
                    ImGui::SetNextItemWidth(-1.f);
                    ImGui::InputTextWithHint("##ln", "env_snow_stormfront_001", g_particleLayerName[li],
                                             sizeof(g_particleLayerName[li]), ImGuiInputTextFlags_EnterReturnsTrue);
                    MenuSliderF("Height##lz", &g_particleLayerZ[li], -16000.f, 16000.f, "%.0f u");
                    ImGui::PopID();
                }
                ImGui::TextDisabled("Shows indoors too; old particles stay until map change.");
                if (!g_fnDispatchEffect || !g_fnGetParticleIndex)
                    ImGui::TextDisabled("Waiting for particle functions...");
            }
            ImGui::Spacing();
        }

        // ---- Players -----------------------------------------------
        MenuSectionLabel("PLAYERS");
        MenuChamLayers("Player chams##plchams", g_enablePlChams, g_plChamCount, g_plChamLayers, false);
        if (g_enablePlChams) {
            MenuCheckbox("Include local player##pllocal", &g_plChamsLocalPlayer);
            MenuCheckbox("Weapons and cosmetics##plattach", &g_plChamsAttachments);
            ImGui::Spacing();
        }

        MenuCheckbox("No hats##nohats", &g_noHats);
        MenuCheckbox("Zombify##zombify", &g_zombify);
        if (g_zombify)
        {
            static const char* const k_zombifyModes[] = { "Enemies", "Everyone", "Everyone + me" };
            MenuSegment("##zombifymode", &g_zombifyMode, k_zombifyModes, 3);
            ImGui::Spacing();
        }

        MenuCheckbox("Only ragdolls of your kills##ragonlykills", &g_ragdollOnlyKills);
        MenuCheckbox("Ragdoll effects##ragfx", &g_ragdollEffectEnabled);
        if (g_ragdollEffectEnabled)
        {
            if (ImGui::BeginTable("##ragfxgrid", 2)) {
                const std::pair<const char*, bool*> k_effects[] = {
                    { "Burning##ragdoll",      &g_ragdollEffectBurning  },
                    { "Electrocuted##ragdoll", &g_ragdollEffectElectro  },
                    { "Ash##ragdoll",          &g_ragdollEffectAsh      },
                    { "Dissolve##ragdoll",     &g_ragdollEffectDissolve },
                    { "Gold##ragdoll",         &g_ragdollEffectGold     },
                    { "Ice##ragdoll",          &g_ragdollEffectIce      },
                };
                for (const auto& [label, flag] : k_effects) {
                    ImGui::TableNextColumn();
                    MenuCheckbox(label, flag);
                }
                ImGui::EndTable();
            }
            ImGui::Spacing();
        }

        MenuCheckbox("Dynamic light##dlenable", &g_enableDLight);
        if (g_enableDLight)
        {
            const float sw = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;
            MenuCheckbox("Use sheen color##dlsheen", &g_dlightUseSheenColor,
                         g_dlightUseSheenColor ? 0.f : ImGui::GetContentRegionAvail().x - sw);
            if (!g_dlightUseSheenColor) {
                ImGui::SameLine();
                ColorPickerPopup(g_dlightColor, 3, "##dlcolorpicker");
            }
            MenuSliderF("Radius##dlradius", &g_dlightRadius, 10.f, 600.f, "%.0f u");
            ImGui::Spacing();
        }

        // ---- Ambient occlusion --------------------------------------
        MenuSectionLabel("AMBIENT OCCLUSION");
        MenuCheckbox("Enable##aoenable", &g_aoEnable);
        if (g_aoEnable && MenuDropdown("Settings##aodrop"))
        {
            MenuSliderF("Intensity##aoint",   &g_aoIntensity, 0.f,   4.f,    "%.2f");
            MenuSliderF("Radius##aorad",      &g_aoRadius,    4.f,   200.f,  "%.0f u");
            MenuSliderI("Samples##aosmp",     &g_aoSamples,   4,     64);
            MenuSliderF("Bias##aobias",       &g_aoBias,      0.f,   0.8f,   "%.2f");
            MenuSliderF("Blur##aoblur",       &g_aoBlur,      0.f,   4.f,    "%.2f");
            MenuSliderF("Resolution##aoscl",  &g_aoScale,     0.25f, 1.f,    "%.2f");
            MenuSliderF("Fade start##aofs",   &g_aoFadeStart, 0.f,   4000.f, "%.0f u");
            MenuSliderF("Fade end##aofe",     &g_aoFadeEnd,   0.f,   8000.f, "%.0f u");
            if (g_aoFadeEnd < g_aoFadeStart + 1.f) g_aoFadeEnd = g_aoFadeStart + 1.f;
            MenuCheckbox("Keep off the viewmodel##aovm", &g_aoViewmodel);
            MenuCheckbox("Fade under fog##aofog", &g_aoFogFade);
            ImGui::Spacing();
        }

        // ---- Depth of field -----------------------------------------
        MenuSectionLabel("DEPTH OF FIELD");
        MenuCheckbox("Enable##dofenable", &g_dofEnable);
        if (g_dofEnable && MenuDropdown("Settings##dofdrop"))
        {
            MenuCheckbox("Auto focus##dofaf", &g_dofAutoFocus);
            if (g_dofAutoFocus) {
                MenuSliderF("Focus speed##doffs", &g_dofFocusSpeed, 0.1f, 40.f,   "%.1f /s");
                MenuSliderF("Focus limit##doffl", &g_dofFocusLimit, 64.f, 8192.f, "%.0f u");
            } else {
                MenuSliderF("Focal distance##dofd", &g_dofDistance, 16.f, 8192.f, "%.0f u");
            }
            MenuSliderF("Sharp band##dofrng",      &g_dofRange,          0.f, 2000.f, "%.0f u");
            MenuSliderF("Ramp##dofrmp",            &g_dofRamp,           1.f, 4000.f, "%.0f u");
            MenuSliderF("Max blur##dofmb",         &g_dofMaxBlur,        0.f, 60.f,   "%.1f px");
            MenuSliderF("Bokeh boost##dofbb",      &g_dofBokehBoost,     0.f, 16.f,   "%.1f");
            MenuSliderF("Bokeh threshold##dofbt",  &g_dofBokehThreshold, 0.f, 1.f,    "%.2f");
            MenuCheckbox("Near blur##dofnb",             &g_dofNearBlur);
            MenuCheckbox("Viewmodel stays sharp##dofvm", &g_dofViewmodelSharp);
            MenuCheckbox("Smoothing pass##dofsm",        &g_dofSmooth);
            ImGui::Spacing();
        }
        // ---- Frame blending ------------------------------------------
        MenuSectionLabel("FRAME BLENDING");
        MenuCheckbox("Enable##fbenable", &g_fbEnable);
        if (g_fbEnable && MenuDropdown("Settings##fbdrop"))
        {
            MenuSliderF("Shutter##fbshut",     &g_fbShutter,   0.f,   60.f,  "%.1f ms");
            MenuSliderF("World amount##fbamt", &g_fbAmount,    0.f,   4.f,   "%.2f");
            MenuSliderI("Samples##fbsmp",      &g_fbSamples,   4,     64);
            MenuSliderF("Max streak##fbmax",   &g_fbMaxStreak, 0.01f, 0.2f,  "%.3f h");
            MenuCheckbox("Blur other players##fbply", &g_fbPlayers);
            MenuCheckbox("Blur the viewmodel##fbvm",  &g_fbViewmodel);
            if (g_fbViewmodel)
                MenuSliderF("Viewmodel amount##fbvma", &g_fbVmAmount, 0.f, 16.f, "%.2f");
            ImGui::Spacing();
        }

        if (const char* fx = ScreenFx_Status(); fx[0])
            ImGui::TextDisabled("%s", fx);

        // ---- Bloom --------------------------------------
        MenuSectionLabel("BLOOM");
        MenuCheckbox("Enable##bloomenable", &g_bloomEnable);
        if (g_bloomEnable)
        {
            if (MenuDropdown("Settings##bloomdrop")) {
            MenuSliderF("Intensity##bloomint",  &g_bloomIntensity, 0.f, 3.f, "%.2f");
            MenuSliderF("Threshold##bloomthr",  &g_bloomThreshold, 0.f, 1.f, "%.2f");
            MenuSliderF("Radius##bloomrad",     &g_bloomRadius,    0.f, 1.f, "%.2f");
            MenuSliderF("Saturation##bloomsat", &g_bloomSat,       0.f, 3.f, "%.2f");
            }
        }
        ImGui::Spacing();

        // ---- Color Correction --------------------------------------
        MenuSectionLabel("COLOR CORRECTION");
        MenuCheckbox("Enable##ccenable", &g_colorCorrEnable);
        if (g_colorCorrEnable)
        {
            if (MenuDropdown("Settings##ccdrop")) {
            // --- Exposure ---
            MenuSectionLabel("EXPOSURE");
            MenuSliderF("Exposure##ccexp", &g_colorCorrExposure, -3.f, 3.f, "%+.2f EV");
            ImGui::Spacing();

            // --- Levels ---
            MenuSectionLabel("LEVELS");
            ImGui::TextDisabled("Input");
            MenuSliderF("Black##cclvinb", &g_colorCorrLevInBlack, 0.f, 0.99f, "%.2f");
            MenuSliderF("White##cclvinw", &g_colorCorrLevInWhite, 0.01f, 1.f,  "%.2f");
            MenuSliderF("Gamma##cclvgam", &g_colorCorrLevGamma,   0.1f, 10.f,  "%.2f");
            ImGui::Spacing();
            ImGui::TextDisabled("Output");
            MenuSliderF("Black##cclvoutb", &g_colorCorrLevOutBlack, 0.f, 1.f, "%.2f");
            MenuSliderF("White##cclvoutw", &g_colorCorrLevOutWhite, 0.f, 1.f, "%.2f");
            // keep inBlack < inWhite
            if (g_colorCorrLevInBlack >= g_colorCorrLevInWhite)
                g_colorCorrLevInWhite = g_colorCorrLevInBlack + 0.01f;
            ImGui::Spacing();

            // --- Curves ---
            MenuSectionLabel("CURVES");
            {
                // Tone curves, Photoshop style: click the graph to add a point (on the curve
                // it lands on the curve, elsewhere at the mouse), drag to move it in x and y,
                // right-click / Delete / drag it off the graph to remove it. Monotone cubic
                // spline in cccurve.h; ColorCorr_Apply bakes all four into the shader LUT.
                static int    s_cvChan = 0;    // 0 master, 1 R, 2 G, 3 B
                static int    s_cvSel  = -1;   // selected point on the active channel
                static int    s_cvDrag = -1;   // point being dragged
                static ImVec2 s_cvGrab;        // point minus mouse at grab time, so a point never jumps

                static const ImU32 k_chanLineActive[4] = {
                    IM_COL32(220,185, 85,230), // M — gold
                    IM_COL32(255, 80, 80,230), // R — red
                    IM_COL32( 80,210, 80,230), // G — green
                    IM_COL32( 80,130,255,230), // B — blue
                };
                static const ImU32 k_chanLineDim[4] = {
                    IM_COL32(180,150, 60, 80),
                    IM_COL32(200, 60, 60, 80),
                    IM_COL32( 60,170, 60, 80),
                    IM_COL32( 60,100,200, 80),
                };
                static const ImU32 k_chanRamp[4] = {   // bottom gradient bar end color
                    IM_COL32(255,255,255,255), IM_COL32(255,0,0,255),
                    IM_COL32(0,255,0,255),     IM_COL32(0,0,255,255),
                };

                // Channel buttons
                {
                    const char* k_lbls[4] = { "M", "R", "G", "B" };
                    const float avail = ImGui::GetContentRegionAvail().x;
                    const float gap   = ImGui::GetStyle().ItemSpacing.x;
                    const float bw    = (avail - gap * 3.f) / 4.f;
                    for (int ch = 0; ch < 4; ch++) {
                        if (ch > 0) ImGui::SameLine(0, gap);
                        bool active = (s_cvChan == ch);
                        if (active) {
                            ImGui::PushStyleColor(ImGuiCol_Button,
                                ImVec4(ImColor(k_chanLineActive[ch])));
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0,0,0,1));
                        }
                        ImGui::PushID(ch);
                        if (ImGui::Button(k_lbls[ch], ImVec2(bw, 0))) {
                            s_cvChan = ch;
                            s_cvSel = s_cvDrag = -1;
                        }
                        ImGui::PopID();
                        if (active) ImGui::PopStyleColor(2);
                    }
                }
                ImGui::Spacing();

                CcCurve& cv = g_ccCurve[s_cvChan];
                if (s_cvSel  >= cv.n) s_cvSel  = -1;   // a preset, reset or config load shrank it
                if (s_cvDrag >= cv.n) s_cvDrag = -1;

                // Graph widget
                const float  sz   = ImGui::GetContentRegionAvail().x;
                const float  h    = std::floor(sz * 0.8f);
                const ImVec2 orig = ImGui::GetCursorScreenPos();
                const ImVec2 br(orig.x + sz, orig.y + h);
                auto toScr = [&](float x, float y) { return ImVec2(orig.x + x * sz, orig.y + (1.f - y) * h); };

                ImGui::InvisibleButton("##cvedit", ImVec2(sz, h),
                    ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
                const bool   cvHov = ImGui::IsItemHovered();
                const ImVec2 mp    = ImGui::GetMousePos();
                const float  mx    = std::clamp((mp.x - orig.x) / sz, 0.f, 1.f);
                const float  my    = std::clamp(1.f - (mp.y - orig.y) / h, 0.f, 1.f);

                int hovPt = -1;
                if (cvHov && s_cvDrag < 0) {
                    float best = 10.f;
                    for (int i = 0; i < cv.n; i++) {
                        const ImVec2 p = toScr(cv.x[i], cv.y[i]);
                        const float  d = hypotf(mp.x - p.x, mp.y - p.y);
                        if (d < best) { best = d; hovPt = i; }
                    }
                }

                if (cvHov && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    if (hovPt >= 0) {
                        s_cvSel = s_cvDrag = hovPt;
                    } else {
                        const float cy = CcCurve_Eval(cv, mx);
                        const float py = std::fabs(toScr(mx, cy).y - mp.y) < 12.f ? cy : my;
                        const int   at = CcCurve_Insert(cv, mx, py);
                        if (at >= 0) s_cvSel = s_cvDrag = at;
                    }
                    if (s_cvDrag >= 0) {
                        const ImVec2 p = toScr(cv.x[s_cvDrag], cv.y[s_cvDrag]);
                        s_cvGrab = ImVec2(p.x - mp.x, p.y - mp.y);
                    }
                }
                if (cvHov && hovPt >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Right) &&
                    CcCurve_Remove(cv, hovPt))
                    s_cvSel = -1;

                if (s_cvDrag >= 0) {
                    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                        s_cvDrag = -1;
                    } else {
                        // Interior points dragged well off the graph are removed, like Photoshop.
                        // End points only clamp: dragging black/white to a corner overshoots a lot.
                        const float kOff = 40.f;
                        const bool  off  = mp.x < orig.x - kOff || mp.x > br.x + kOff ||
                                           mp.y < orig.y - kOff || mp.y > br.y + kOff;
                        if (off && s_cvDrag > 0 && s_cvDrag < cv.n - 1 && CcCurve_Remove(cv, s_cvDrag))
                            s_cvSel = s_cvDrag = -1;
                        else
                            CcCurve_Move(cv, s_cvDrag,
                                         (mp.x + s_cvGrab.x - orig.x) / sz,
                                         1.f - (mp.y + s_cvGrab.y - orig.y) / h);
                    }
                }
                if (s_cvSel >= 0 && s_cvDrag < 0 && !ImGui::GetIO().WantTextInput &&
                    ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
                    ImGui::IsKeyPressed(ImGuiKey_Delete, false) && CcCurve_Remove(cv, s_cvSel))
                    s_cvSel = -1;

                // Draw
                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddRectFilled(orig, br, IM_COL32(14, 14, 18, 220), 4.f);
                dl->PushClipRect(ImVec2(orig.x - 7.f, orig.y - 7.f), ImVec2(br.x + 7.f, br.y + 7.f), true);
                for (int gi = 1; gi < 8; gi++) {   // grid: quarters, faint eighths
                    const ImU32 gc = (gi % 2) ? IM_COL32(35, 35, 43, 110) : IM_COL32(50, 50, 60, 150);
                    const float gx = orig.x + sz * gi / 8.f;
                    const float gy = orig.y + h  * gi / 8.f;
                    dl->AddLine(ImVec2(gx, orig.y), ImVec2(gx, br.y), gc);
                    dl->AddLine(ImVec2(orig.x, gy), ImVec2(br.x, gy), gc);
                }
                dl->AddLine(ImVec2(orig.x, br.y), ImVec2(br.x, orig.y), IM_COL32(55, 55, 70, 160));

                auto drawCurve = [&](const CcCurve& c, ImU32 col, float lw) {
                    enum { kSeg = 160 };
                    ImVec2 pts[kSeg + 1];
                    for (int s = 0; s <= kSeg; s++) {
                        const float t = s / (float)kSeg;
                        pts[s] = toScr(t, CcCurve_Eval(c, t));
                    }
                    dl->AddPolyline(pts, kSeg + 1, col, ImDrawFlags_None, lw);
                };
                for (int ch = 0; ch < 4; ch++)   // other channels dim, identity ones hidden
                    if (ch != s_cvChan && !CcCurve_IsIdentity(g_ccCurve[ch]))
                        drawCurve(g_ccCurve[ch], k_chanLineDim[ch], 1.f);
                const ImU32 activeCol = k_chanLineActive[s_cvChan];
                drawCurve(cv, activeCol, 2.f);

                // hover guide: where a click would land
                if (cvHov && s_cvDrag < 0 && hovPt < 0) {
                    const ImVec2 cp = toScr(mx, CcCurve_Eval(cv, mx));
                    dl->AddLine(ImVec2(mp.x, orig.y), ImVec2(mp.x, br.y), IM_COL32(255, 255, 255, 28));
                    dl->AddCircle(cp, 3.5f, IM_COL32(255, 255, 255, 120));
                }

                for (int i = 0; i < cv.n; i++) {
                    const ImVec2 p   = toScr(cv.x[i], cv.y[i]);
                    const bool   hot = (i == hovPt || i == s_cvDrag);
                    const float  r   = hot ? 5.5f : 4.5f;
                    if (i == s_cvSel) {
                        dl->AddRectFilled(ImVec2(p.x - r, p.y - r), ImVec2(p.x + r, p.y + r), IM_COL32(255, 255, 255, 255));
                        dl->AddRect(ImVec2(p.x - r, p.y - r), ImVec2(p.x + r, p.y + r), activeCol, 0.f, 0, 1.5f);
                    } else {
                        dl->AddCircleFilled(p, r, hot ? IM_COL32(255, 255, 255, 255) : IM_COL32(14, 14, 18, 255));
                        dl->AddCircle(p, r, activeCol, 0, 1.5f);
                    }
                }
                dl->PopClipRect();
                dl->AddRect(orig, br, IM_COL32(65, 65, 75, 200), 4.f);

                // input ramp under the graph
                {
                    const ImVec2 ro = ImGui::GetCursorScreenPos();
                    const ImU32  rc = k_chanRamp[s_cvChan];
                    dl->AddRectFilledMultiColor(ro, ImVec2(ro.x + sz, ro.y + 5.f),
                                                IM_COL32(0, 0, 0, 255), rc, rc, IM_COL32(0, 0, 0, 255));
                    ImGui::Dummy(ImVec2(sz, 5.f));
                }

                // readout: the point under the mouse / being dragged, else the curve at the mouse
                {
                    const int pi = s_cvDrag >= 0 ? s_cvDrag : hovPt;
                    if (pi >= 0)
                        ImGui::TextDisabled("Point %d/%d   In %d   Out %d", pi + 1, cv.n,
                            (int)std::lround(cv.x[pi] * 255.f), (int)std::lround(cv.y[pi] * 255.f));
                    else if (cvHov)
                        ImGui::TextDisabled("In %d   Out %d", (int)std::lround(mx * 255.f),
                            (int)std::lround(CcCurve_Eval(cv, mx) * 255.f));
                    else
                        ImGui::TextDisabled("Click: add   Drag: move   Right-click / Del: remove");
                }

                // selected point, exact values (0-255 like Photoshop)
                {
                    const float gap  = ImGui::GetStyle().ItemSpacing.x;
                    const float delW = ImGui::CalcTextSize("Delete").x + ImGui::GetStyle().FramePadding.x * 2.f;
                    const float fw   = (ImGui::GetContentRegionAvail().x - delW - gap * 2.f) * 0.5f;
                    const bool  has  = s_cvSel >= 0;
                    float in  = has ? cv.x[s_cvSel] * 255.f : 0.f;
                    float out = has ? cv.y[s_cvSel] * 255.f : 0.f;
                    ImGui::BeginDisabled(!has);
                    ImGui::SetNextItemWidth(fw);
                    bool edited = ImGui::DragFloat("##cvin", &in, 0.25f, 0.f, 255.f, has ? "In %.0f" : "In -");
                    ImGui::SameLine(0, gap);
                    ImGui::SetNextItemWidth(fw);
                    edited |= ImGui::DragFloat("##cvout", &out, 0.25f, 0.f, 255.f, has ? "Out %.0f" : "Out -");
                    if (edited && has) CcCurve_Move(cv, s_cvSel, in / 255.f, out / 255.f);
                    ImGui::SameLine(0, gap);
                    ImGui::BeginDisabled(cv.n <= 2);
                    if (ImGui::Button("Delete##cvdel", ImVec2(delW, 0.f)) && has && CcCurve_Remove(cv, s_cvSel))
                        s_cvSel = -1;
                    ImGui::EndDisabled();
                    ImGui::EndDisabled();
                }

                // Presets (active channel) + resets
                {
                    struct CvPreset { const char* name; int n; float p[8]; };   // x,y pairs
                    static const CvPreset k_presets[] = {
                        { "Linear",          2, { 0.f, 0.f,    1.f, 1.f } },
                        { "Soft contrast",   4, { 0.f, 0.f,    0.25f, 0.20f, 0.75f, 0.80f, 1.f, 1.f } },
                        { "Strong contrast", 4, { 0.f, 0.f,    0.25f, 0.13f, 0.75f, 0.87f, 1.f, 1.f } },
                        { "Faded film",      4, { 0.f, 0.08f,  0.25f, 0.27f, 0.75f, 0.76f, 1.f, 0.92f } },
                        { "Brighten",        3, { 0.f, 0.f,    0.5f, 0.62f,  1.f, 1.f } },
                        { "Darken",          3, { 0.f, 0.f,    0.5f, 0.38f,  1.f, 1.f } },
                        { "Crush blacks",    3, { 0.f, 0.f,    0.12f, 0.f,   1.f, 1.f } },
                        { "Invert",          2, { 0.f, 1.f,    1.f, 0.f } },
                    };
                    const float gap = ImGui::GetStyle().ItemSpacing.x;
                    const float bw3 = (ImGui::GetContentRegionAvail().x - gap * 2.f) / 3.f;
                    ImGui::SetNextItemWidth(bw3);
                    if (ImGui::BeginCombo("##cvpreset", "Preset", ImGuiComboFlags_HeightLarge)) {
                        for (const CvPreset& pr : k_presets) {
                            if (!ImGui::Selectable(pr.name)) continue;
                            CcCurve c;
                            c.n = pr.n;
                            for (int i = 0; i < pr.n; i++) { c.x[i] = pr.p[i * 2]; c.y[i] = pr.p[i * 2 + 1]; }
                            cv = c;
                            s_cvSel = s_cvDrag = -1;
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::SameLine(0, gap);
                    if (ImGui::Button("Reset channel##cvcurvereset", ImVec2(bw3, 0.f))) {
                        CcCurve_Reset(cv);
                        s_cvSel = s_cvDrag = -1;
                    }
                    ImGui::SameLine(0, gap);
                    if (ImGui::Button("Reset curves##cvresetall", ImVec2(bw3, 0.f))) {
                        for (CcCurve& c : g_ccCurve) CcCurve_Reset(c);
                        s_cvSel = s_cvDrag = -1;
                    }
                }
            }
            ImGui::Spacing();

            // --- Highlights / Shadows ---
            MenuSectionLabel("TONE");
            MenuSliderF("Highlights##cchigh", &g_colorCorrHighlights, -1.f, 1.f, "%+.2f");
            MenuSliderF("Shadows##ccshad",    &g_colorCorrShadows,    -1.f, 1.f, "%+.2f");
            ImGui::Spacing();

            // --- Color ---
            MenuSectionLabel("COLOR");
            MenuSliderF("Red##ccr",            &g_colorCorrR,        0.f,    2.f,    "%.2f");
            MenuSliderF("Green##ccg",          &g_colorCorrG,        0.f,    2.f,    "%.2f");
            MenuSliderF("Blue##ccb",           &g_colorCorrB,        0.f,    2.f,    "%.2f");
            MenuSliderF("Hue##cchue",          &g_colorCorrHue,     -180.f,  180.f,  "%.1f");
            MenuSliderF("Saturation##ccsat",   &g_colorCorrSat,      0.f,    3.f,    "%.2f");
            MenuSliderF("Contrast##cccon",     &g_colorCorrContrast, 0.5f,   1.5f,   "%.3f");
            MenuSliderF("Temperature##cctemp", &g_colorCorrTemp,    -1.f,    1.f,    "%+.2f");
            ImGui::Spacing();

            // Reset all
            if (ImGui::Button("Reset all##ccreset", ImVec2(-1.f, 0.f))) {
                g_colorCorrExposure    = 0.f;
                g_colorCorrLevInBlack  = 0.f; g_colorCorrLevInWhite  = 1.f;
                g_colorCorrLevGamma    = 1.f;
                g_colorCorrLevOutBlack = 0.f; g_colorCorrLevOutWhite = 1.f;
                g_colorCorrHighlights  = 0.f; g_colorCorrShadows     = 0.f;
                for (CcCurve& c : g_ccCurve) CcCurve_Reset(c);
                g_colorCorrR = g_colorCorrG = g_colorCorrB = 1.f;
                g_colorCorrHue = 0.f; g_colorCorrSat = 1.f;
                g_colorCorrContrast = 1.f; g_colorCorrTemp = 0.f;
            }
            } // end Settings
        }
        ImGui::Spacing();
    } // end s_tab == 3

    // ---- Tab 4: Crosshair --------------------------------------
    if (s_tab == 4)
    {
        // up to two "swatch + label" pairs on one row
        auto ColorColumns = [](const char* labelA, float* colA, const char* labelB, float* colB) {
            if (!ImGui::BeginTable(labelA, 2)) return;
            ImGui::TableNextColumn();
            ImGui::PushID(labelA); ColorPickerPopup(colA, 4, "##c"); ImGui::PopID();
            ImGui::SameLine(); ImGui::TextDisabled("%s", labelA);
            if (colB) {
                ImGui::TableNextColumn();
                ImGui::PushID(labelB); ColorPickerPopup(colB, 4, "##c"); ImGui::PopID();
                ImGui::SameLine(); ImGui::TextDisabled("%s", labelB);
            }
            ImGui::EndTable();
        };
        // four side toggles in a 2x2 grid; sides[] is indexed L, R, B, T
        auto SideGrid = [](const char* id, bool* sides) {
            if (!ImGui::BeginTable(id, 2)) return;
            ImGui::PushID(id);
            ImGui::TableNextColumn(); MenuCheckbox("Left",   &sides[0]);
            ImGui::TableNextColumn(); MenuCheckbox("Right",  &sides[1]);
            ImGui::TableNextColumn(); MenuCheckbox("Top",    &sides[3]);
            ImGui::TableNextColumn(); MenuCheckbox("Bottom", &sides[2]);
            ImGui::PopID();
            ImGui::EndTable();
        };

        MenuSectionLabel("CROSSHAIR");
        MenuCheckbox("Enable##chenable", &g_enableCrosshair);
        if (g_enableCrosshair)
        {
            MenuCheckbox("Antialiased##ch",   &g_chAntialias);
            MenuCheckbox("Dot##ch",           &g_chDot);
            MenuCheckbox("Outline##ch",       &g_chOutline);
            MenuCheckbox("Only some sides##chsides", &g_chSidesOnly);
            if (g_chSidesOnly) SideGrid("##chsidesgrid", g_chSides);

            MenuSectionLabel("BLEND");
            if (Crosshair_BlendCombo("##chblend"))
                ImGui::SetTooltip("How the arms + dot mix with the frame under them.\n"
                                  "Additive adds (dest ONE); the rest are Krita's blending modes.\n"
                                  "Alpha is the opacity. Krita modes leave the outline normal.");

            MenuSectionLabel("SHAPE");
            MenuSliderF("Thickness##chthick",      &g_chThickness, 0.5f,  5.f,   "%.1f");
            MenuSliderF("Length##chlen",           &g_chLength,    0.5f,  20.f,  "%.1f");
            MenuSliderF("Gap##chgap",              &g_chGap,      -10.f,  10.f,  "%.1f");
            MenuSliderF("Aspect##chaspect",        &g_chAspect,    0.1f,  10.f,  "%.2f");
            MenuSliderF("Gap aspect##chgapaspect", &g_chGapAspect, 0.1f,  5.f,   "%.2f");
            MenuSliderF("Rotation##chrot",         &g_chRotation,  0.f,   360.f, "%.0f deg");
            MenuCheckbox("Taper##chtaper", &g_chTaper);
            if (g_chTaper) {
                MenuSliderF("Near width##chtapernear", &g_chTaperNear, 0.f, 10.f, "%.2f");
                MenuSliderF("Far width##chtaperfar",   &g_chTaperFar,  0.f, 10.f, "%.2f");
            }

            MenuSectionLabel("COLOR");
            MenuCheckbox("Use sheen color##chsheen", &g_chUseSheenColor);
            if (g_chUseSheenColor)
                MenuSliderF("Alpha##chalpha", &g_chColor[3], 0.f, 1.f, "%.2f");
            else
                ColorColumns("Crosshair", g_chColor, nullptr, nullptr);
            MenuCheckbox("Gradient##chgrad", &g_chGradient);
            if (g_chGradient) {
                ColorColumns("Center", g_chGradColorCenter, "Outer", g_chGradColorOuter);
                MenuSliderF("Gradient start##chgradstart", &g_chGradStart, 0.f, 1.f, "%.2f");
                MenuSliderF("Gradient end##chgradend",     &g_chGradEnd,   0.f, 1.f, "%.2f");
                if (g_chGradEnd < g_chGradStart) g_chGradEnd = g_chGradStart;
            }
            if (g_chDot) {
                MenuCheckbox("Round dot##chdotcircle", &g_chDotCircle);
                MenuCheckbox("Custom dot color##chdotcustom", &g_chDotCustomColor);
                if (g_chDotCustomColor) ColorColumns("Dot color", g_chDotColor, nullptr, nullptr);
            }

            if (g_chOutline)
            {
                MenuSectionLabel("OUTLINE");
                if (ImGui::BeginTable("##chouton", 2)) {
                    ImGui::TableNextColumn(); MenuCheckbox("On lines##choutlonlines", &g_chOutlineOnLines);
                    ImGui::TableNextColumn(); MenuCheckbox("On dot##choutlondot",     &g_chOutlineOnDot);
                    ImGui::EndTable();
                }
                MenuSliderF("Thickness##chotthick", &g_chOutlineThick, 0.1f, 5.f, "%.1f");
                g_chOutlineThick = roundf(g_chOutlineThick * 10.f) / 10.f;
                MenuCheckbox("Use sheen color##choutsheencolor", &g_chOutlineUseSheenColor);
                if (!g_chOutlineUseSheenColor) ColorColumns("Outline color", g_chOutlineColor, nullptr, nullptr);
                MenuCheckbox("Soft (drop shadow)##choutblur", &g_chOutlineBlur);
                if (g_chOutlineBlur)
                    MenuSliderF("Softness##choutblurr", &g_chOutlineBlurRadius, 0.5f, 10.f, "%.1f");
                MenuCheckbox("Only some sides##choutlinesides", &g_chOutlineSidesOnly);
                if (g_chOutlineSidesOnly) SideGrid("##choutsidesgrid", g_chOutlineSides);
            }
        }

        // ---- CSGO crosshair ----------------------------------------
        MenuSectionLabel("CSGO CROSSHAIR");
        MenuCheckbox("Enable##chcsgo", &g_chCsgoEnable);
        if (g_chCsgoEnable)
        {
            // exact cl_crosshairstyle numbering: 0/1 = panorama reticle, 2/3/4 = vgui bars
            static const char* const k_csgoStyles[] = {
                "Default", "Default static", "Classic", "Classic dynamic", "Classic static" };
            ImGui::TextDisabled("Style");
            ImGui::SetNextItemWidth(-1.f);
            ImGui::Combo("##chcsgostyle", &g_chCsgoStyle, k_csgoStyles, IM_ARRAYSIZE(k_csgoStyles));
            ColorColumns("CSGO color", g_chCsgoColor, nullptr, nullptr);

            if (g_chCsgoStyle <= 1)
            {
                // csgo's gap is YRES-derived (768-height baseline) while the art stays 1:1 px
                MenuSliderF("Gap offset##chcsgogap",      &g_chCsgoGap,     -20.f, 20.f, "%.1f");
                MenuSliderF("Scale##chcsgoscale",         &g_chCsgoScale,    0.5f, 8.f,  "%.2f");
                MenuSliderF("Movement sensitivity##chcsgovs", &g_chCsgoVelSens, 0.f, 2.f,  "%.2f");
                MenuSliderF("Movement smoothing##chcsgosm",   &g_chCsgoSmooth,  0.f, 0.5f, "%.2f s");
                MenuCheckbox("Crisp scaling##chcsgocrisp",  &g_chCsgoCrisp);    // point filter when zoomed
                MenuCheckbox("Linear light##chcsgolin",     &g_chCsgoLinear);   // panorama's sRGB blending
                if (g_chCsgoStyle == 0)
                    MenuCheckbox("Spread lines##chcsgoarcs", &g_chCsgoArcs);  // the 4 animated bars
                MenuCheckbox("Arms##chcsgopips", &g_chCsgoPips);              // the 4 pips
            }
            else
            {
                MenuSliderF("Size##csxsize",    &g_csXSize,  0.f, 20.f, "%.1f");
                MenuSliderF("Thickness##csxth", &g_csXThick, 0.f, 6.f,  "%.1f");
                MenuSliderF("Gap##csxgap",      &g_csXGap,  -5.f, 20.f, "%.1f");
                MenuCheckbox("Use alpha##csxusea", &g_csXUseAlpha);   // off = additive bars
                if (g_csXUseAlpha)
                    MenuSliderI("Alpha##csxalpha", &g_csXAlpha, 0, 255);
                MenuCheckbox("Dot##csxdot",         &g_csXDot);
                MenuCheckbox("T style##csxt",       &g_csXT);
                MenuCheckbox("Outline##csxoutl",    &g_csXOutline);
                if (g_csXOutline)
                    MenuSliderF("Outline thickness##csxoutlth", &g_csXOutlineTh, 0.1f, 3.f, "%.1f");
                if (g_chCsgoStyle <= 3) {   // 2/3 are the dynamic ones; 4 never moves
                    MenuSliderF("Movement sensitivity##csxvs", &g_chCsgoVelSens, 0.f, 2.f,  "%.2f");
                    MenuSliderF("Movement smoothing##csxsm",   &g_chCsgoSmooth,  0.f, 0.5f, "%.2f s");
                }
                if (g_chCsgoStyle == 2) {
                    MenuSliderF("Split distance##csxsd",   &g_csXSplitDist,  0.f, 25.f, "%.1f");
                    MenuSliderF("Split ratio##csxsr",      &g_csXSplitRatio, 0.f, 1.f,  "%.2f");
                    MenuSliderF("Inner alpha##csxsai",     &g_csXInnerMod,   0.f, 1.f,  "%.2f");
                    MenuSliderF("Outer alpha##csxsao",     &g_csXOuterMod,   0.f, 1.f,  "%.2f");
                }
            }
        }

        // ---- CS:GO sniper scope ------------------------------------
        MenuSectionLabel("CSGO SCOPE");
        MenuCheckbox("Enable##sccsgo", &g_scCsgo);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("CS:GO's sniper scope 1:1 (cs_hud_scope.cpp + CS:GO's own art) replaces\n"
                              "TF's while zoomed, under the HUD. Every crosshair is hidden while it draws.");
        if (g_scCsgo)
        {
            static const char* const k_scWeapons[] = { "AWP", "SSG 08", "SCAR-20 / G3SG1" };
            ImGui::TextDisabled("Weapon");
            ImGui::SetNextItemWidth(-1.f);
            ImGui::Combo("##scweapon", &g_scCsgoWeapon, k_scWeapons, IM_ARRAYSIZE(k_scWeapons));
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Whose inaccuracy numbers drive the blur (items_game.txt)");
            static const char* const k_scZoom[] = { "Zoom level 1", "Zoom level 2" };
            ImGui::SetNextItemWidth(-1.f);
            ImGui::Combo("##sczoom", &g_scCsgoZoom, k_scZoom, IM_ARRAYSIZE(k_scZoom));
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("FOV the blur is measured at: 40, or the 2nd zoom (clamped to 25)");
            MenuSliderI("Line width##scwidth", &g_scCsgoWidth, 1, 10);   // cl_crosshair_sniper_width
            MenuCheckbox("Show normal inaccuracy##scnormal", &g_scCsgoNormalInacc);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("cl_crosshair_sniper_show_normal_inaccuracy: count the standing\n"
                                  "inaccuracy too, so the lines never go fully sharp");
            MenuSliderF("Opacity##scopacity", &g_scCsgoOpacity, 0.f, 1.f, "%.2f");
            MenuSliderF("Scale##scscale", &g_scCsgoScale, 0.25f, 2.f, "%.2f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Size of the scope circle + lens. 1 = CS:GO");
            MenuCheckbox("Dust lens##sclens", &g_scCsgoLens);
            MenuCheckbox("Hide your dot##scnodot", &g_scCsgoNoDot);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Your own glowing sniper dot is not drawn (others still see it)");
            MenuSliderF("Move blur##scmove", &g_scCsgoMoveSens, 0.f, 2.f, "%.2f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("How much running blurs the lines and closes the scope.\n"
                                  "1 = CS:GO, 0 = scope stays sharp while moving");
            MenuSliderF("Air blur##scair", &g_scCsgoAirSens, 0.f, 2.f, "%.2f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Jump, fall and landing inaccuracy. 1 = CS:GO, 0 = none");
            MenuCheckbox("Scope-in blur##scin", &g_scCsgoScopeIn);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("CS:GO carries the unscoped penalty into the zoom, so the lines\n"
                                  "start blurred and snap in. Off = sharp the moment you zoom");
            MenuCheckbox("Sway##scbob", &g_scCsgoBob);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Whole scope rides the CS:GO walk bob (own amounts, not the viewmodel bob)");
            if (g_scCsgoBob)
            {
                MenuSliderF("Sway scale##scbobscale", &g_scCsgoBobScale, 0.f, 10.f, "%.2f");
                MenuSliderF("Sway sideways##scboblat", &g_scCsgoBobLat, 0.f, 5.f, "%.2f");
                MenuSliderF("Sway up/down##scbobvert", &g_scCsgoBobVert, 0.f, 5.f, "%.2f");
            }
        }
        MenuCheckbox("Hide scope charge meter##scchargehide", &g_scChargeHide);
    } // end s_tab == 4

    // ---- Tab 5: Misc -------------------------------------------
    if (s_tab == 5)
    {
        MenuSectionLabel("MISC");
        MenuCheckbox("Minesweeper##msopen", &g_minesweeperOpen);
        if (g_minesweeperOpen)
            MenuCheckbox("Night theme##ms", &g_msDarkMode);

        MenuCheckbox("Net graph colors##ngcol", &g_ngColorEnable);
        if (g_ngColorEnable)
            MenuColorGrid("##ngcolors", {
                { "Text", g_ngTextColor, 4 }, { "Orange", g_ngOrangeColor, 4 },
                { "Red",  g_ngRedColor,  4 }, { "Yellow", g_ngYellowColor, 4 } });

        // ---- Web on surface ------------------------------------------
        MenuSectionLabel("WEB ON SURFACE");
        MenuCheckbox("Enable##webenable", &g_webEnable);
        if (g_webEnable && MenuDropdown("Settings##webdrop"))
        {
            const float gap   = ImGui::GetStyle().ItemSpacing.x;
            const float halfW = (ImGui::GetContentRegionAvail().x - gap) * 0.5f;

            // A rebind button: "Place: F", or the capture prompt while it listens.
            auto bindBtn = [&](const char* id, const char* name, int* key, int mode, float w) {
                char lbl[96];
                if (g_webKeyCapture == mode) snprintf(lbl, sizeof(lbl), "Press a key...%s", id);
                else snprintf(lbl, sizeof(lbl), "%s: %s%s", name, (*key ? VkToName(*key) : std::string("none")).c_str(), id);
                if (ImGui::Button(lbl, ImVec2(w, 0))) g_webKeyCapture = (g_webKeyCapture == mode) ? 0 : mode;
            };

            MenuSectionLabel("PAGE");
            static char s_webUrlBuf[512] = {};
            static bool s_webUrlInit = false;
            if (!s_webUrlInit) { strncpy_s(s_webUrlBuf, g_webUrl.c_str(), _TRUNCATE); s_webUrlInit = true; }
            const float goW = 46.f;
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - goW - gap);
            const bool urlEnter = ImGui::InputTextWithHint("##weburlcfg", "Address or search",
                s_webUrlBuf, sizeof(s_webUrlBuf), ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::SameLine();
            if (ImGui::Button("Go##weburlgo", ImVec2(goW, 0)) || urlEnter) {
                g_webUrl = s_webUrlBuf;
                Web_Navigate(g_webUrl.c_str());
            }
            if (ImGui::Button("Clear cache##webclearcfg", ImVec2(halfW, 0))) Web_ClearCache();
            ImGui::SameLine();
            if (ImGui::Button("Clear memory##webclrmemcfg", ImVec2(halfW, 0))) Web_ClearMemory();
            ImGui::TextDisabled("Cache + cookies wiped every time the browser starts");

            static const char* const k_webRes[] = { "480p", "720p", "1080p" };
            int resSel = g_webResH >= 1080 ? 2 : (g_webResH >= 720 ? 1 : 0);
            ImGui::TextDisabled("Resolution");
            if (MenuSegment("##webres", &resSel, k_webRes, 3)) {
                const int h[3] = { 480, 720, 1080 };
                g_webResH = h[resSel];
            }
            static const char* k_webAspects[k_wsAspectCount] = {};
            if (!k_webAspects[0]) for (int i = 0; i < k_wsAspectCount; ++i) k_webAspects[i] = k_wsAspects[i].name;
            if (g_webAspect < 0 || g_webAspect >= k_wsAspectCount) g_webAspect = 0;
            ImGui::TextDisabled("Shape");
            MenuSegment("##webaspect", &g_webAspect, k_webAspects, k_wsAspectCount);
            MenuCheckbox("Smooth video (auto quality)##websmooth", &g_webSmooth);
            MenuCheckbox("Block ads##webadblock", &g_webAdblock);

            MenuSectionLabel("WINDOW");
            MenuCheckbox("Show browser window##webpanel", &g_webPanel);
            if (g_webPanel) {
                MenuCheckbox("Keep with menu closed##webalways", &g_webPanelAlways);
                MenuSliderF("Window opacity##webpanelop", &g_webPanelOpacity, 0.05f, 1.f, "%.2f");
            }

            MenuSectionLabel("SCREEN IN THE WORLD");
            if (ImGui::Button("Place at crosshair##webplacenow", ImVec2(halfW, 0))) Web_PlaceAtCrosshair();
            ImGui::SameLine();
            if (ImGui::Button("Remove##webremovenow", ImVec2(halfW, 0))) Web_Unplace();
            MenuSliderF("Size##webscale", &g_webScale, 0.1f, 8.f, "%.2fx");
            MenuSliderF("Corner rounding##webround", &g_webRound, 0.f, 128.f, "%.0f");
            MenuSliderF("Opacity##webworldop", &g_webWorldOpacity, 0.f, 1.f, "%.2f");
            static const char* const k_webDepth[] = { "Decal", "Under HUD", "On top" };
            ImGui::TextDisabled("Draw");
            MenuSegment("##webdepth", &g_webDepthMode, k_webDepth, 3);
            static const char* const k_webBack[] = { "Mirror", "Hidden", "Readable" };
            ImGui::TextDisabled("Back side");
            MenuSegment("##webback", &g_webBackMode, k_webBack, 3);

            MenuSectionLabel("PLAYBACK");
            MenuCheckbox("Play when placed##webautoplay", &g_webAutoPlay);
            if (g_webAutoPlay) MenuCheckbox("From the beginning##webfromstart", &g_webPlayFromStart);
            MenuCheckbox("Remove when video ends##webautoremove", &g_webAutoRemove);
            MenuSliderF("Volume##webvol", &g_webVolume, 0.f, 1.f, "%.2f");
            MenuCheckbox("Fade with distance##webvoldist", &g_webVolDistance);
            if (g_webVolDistance) {
                MenuSliderF("Full volume within##webvolnear", &g_webVolNear, 0.f, 2048.f, "%.0f");
                MenuSliderF("Silent past##webvolfar", &g_webVolFar, 16.f, 4096.f, "%.0f");
            }

            MenuSectionLabel("CONTROLS");
            MenuCheckbox("Crosshair is the mouse##webptr", &g_webCrosshairPointer);
            if (g_webCrosshairPointer) {
                MenuCheckbox("Hover only on click##webclickonly", &g_webClickOnly);
                MenuCheckbox("Don't shoot at the screen##webblockfire", &g_webBlockFire);
            }
            MenuCheckbox("Wheel / arrows scroll aimed screen##webscroll", &g_webScrollInput);
            bindBtn("##webplacekey",  "Place",  &g_webPlaceKey,  1, halfW);
            ImGui::SameLine();
            bindBtn("##webremovekey", "Remove", &g_webRemoveKey, 3, halfW);
            bindBtn("##webclickkey",  "Click",  &g_webClickKey,  2, halfW);
            ImGui::SameLine();
            bindBtn("##webrclickkey", "Right",  &g_webRClickKey, 4, halfW);
        }

        // ---- Media player --------------------------------------------
        MenuSectionLabel("MEDIA PLAYER");
        MenuCheckbox("Enable##mpenable", &g_enableMediaPlayer);
        if (g_enableMediaPlayer && MenuDropdown("Settings##mpdrop"))
        {
            static const char* const k_mpModes[] = { "Card", "Text strip" };
            MenuSegment("##mpmode", &g_mpMode, k_mpModes, 2);
            MenuSliderF("Size##mpscale", &g_mpScale, 0.3f, 3.f, "%.2f");
            MenuCheckbox("Static width##mpstaticw", &g_mpStaticWidth);
            MenuSliderF("Max width##mpmaxw", &g_mpMaxWidth, 0.f, 600.f, g_mpMaxWidth < 1.f ? "off" : "%.0f px");
            MenuCheckbox("Copy style from watermark##mpcopy", &g_mpCopyWmStyle);

            if (g_mpMode == 0)
            {
                MenuSectionLabel("CARD");
                MenuCheckbox("Album cover##mpthumb", &g_mpShowThumb);
                if (g_mpShowThumb) {
                    MenuSliderF("Cover size##mpthumbsize", &g_mpThumbSize, 16.f, 96.f, "%.0f px");
                    MenuCheckbox("Keep aspect ratio##mpasp", &g_mpThumbPreserveAspect);
                    MenuCheckbox("Round cover##mpcirc", &g_mpThumbCircle);
                    if (!g_mpThumbCircle)
                        MenuSliderF("Cover rounding##mpthumbround", &g_mpThumbRounding, 0.f, 48.f, "%.0f");
                    MenuSliderF("Cover X offset##mpAlbumX", &g_mpAlbumXOff, -400.f, 400.f, "%.0f px");
                    MenuSliderF("Cover Y offset##mpAlbumY", &g_mpAlbumYOff, -400.f, 400.f, "%.0f px");
                    MenuCheckbox("Cover outline##mpoutcb", &g_mpThumbOutline);
                    if (g_mpThumbOutline) {
                        MenuSliderF("Outline thickness##mpoutthick", &g_mpThumbOutlineThick, 0.5f, 8.f, "%.1f");
                        MenuColorGrid("##mpoutcol", { { "Outline color", g_mpThumbOutlineColor, 4 } });
                    }
                    MenuCheckbox("Custom cover image##mpCustomThumb", &g_mpCustomThumb);
                    if (g_mpCustomThumb) {
                        static char        s_mpCustomPath[512] = {};
                        static std::string s_mpCustomPathLast;
                        if (g_mpCustomThumbPath != s_mpCustomPathLast) {
                            strncpy_s(s_mpCustomPath, g_mpCustomThumbPath.c_str(), sizeof(s_mpCustomPath) - 1);
                            s_mpCustomPathLast = g_mpCustomThumbPath;
                        }
                        const float btnW = 60.f, gap = ImGui::GetStyle().ItemSpacing.x;
                        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - btnW - gap);
                        if (ImGui::InputTextWithHint("##mpcustompath", ".png .jpg .bmp .gif", s_mpCustomPath, sizeof(s_mpCustomPath))) {
                            g_mpCustomThumbPath = s_mpCustomPath;
                            s_mpCustomPathLast  = s_mpCustomPath;
                        }
                        ImGui::SameLine(0, gap);
                        if (ImGui::Button("Browse##mpcustombrowse", ImVec2(btnW, 0.f)))
                            FilePick_Start(FILEPICK_MEDIA_THUMB);
                    }
                }
                MenuCheckbox("Progress bar##mpprog", &g_mpShowProgress);
                if (g_mpShowProgress) {
                    MenuSliderF("Bar X offset##mpprogx", &g_mpProgXOff,   -400.f, 400.f, "%.0f px");
                    MenuSliderF("Bar Y offset##mpprogy", &g_mpProgYOff,   -400.f, 400.f, "%.0f px");
                    MenuSliderF("Bar width##mpprogw",    &g_mpProgBarW,   -200.f, 400.f, "%.0f px");
                    MenuSliderF("Bar height##mpprogh",   &g_mpProgHeight,    0.5f, 16.f, "%.1f px");
                }
                MenuCheckbox("Duration##mpshowdur", &g_mpShowDuration);
            }

            MenuSectionLabel("POSITION");
            MenuAnchor6("##mppos", &g_mpPos);
            MenuSliderF("Y offset##mpyoff", &g_mpYOffset,    0.f,   1.f, "%.2f");
            MenuSliderF("X offset##mpxoff", &g_mpXOffset, -800.f, 800.f, "%.0f px");
            MenuCheckbox("Avoid watermark##mpavoid", &g_mpAvoidWatermark);

            MenuSectionLabel("TEXT");
            {
                static const char* const k_align[] = { "Left", "Center", "Right" };
                ImGui::TextDisabled("Track");
                MenuSegment("##mptitlealign", &g_mpTitleAlign, k_align, 3);
                MenuSliderF("Track X offset##mptitlexoff",  &g_mpTitleXOff, -800.f, 800.f,  "%.0f px");
                MenuSliderF("Track Y offset##mptitleyoff",  &g_mpTitleYOff, -400.f, 400.f,  "%.0f px");
                MenuSliderF("Track max width##mptitlemaxw", &g_mpTitleMaxW,    0.f, 2000.f, "%.0f px");
                MenuCheckbox("Ellipsis when cut##mptitleell", &g_mpTitleEllipsis);
                ImGui::TextDisabled("Artist");
                MenuSegment("##mpartistalign", &g_mpArtistAlign, k_align, 3);
                MenuSliderF("Artist X offset##mpartistxoff",  &g_mpArtistXOff, -800.f, 800.f,  "%.0f px");
                MenuSliderF("Artist Y offset##mpartistyoff",  &g_mpArtistYOff, -400.f, 400.f,  "%.0f px");
                MenuSliderF("Artist max width##mpartistmaxw", &g_mpArtistMaxW,    0.f, 2000.f, "%.0f px");
                MenuCheckbox("Ellipsis when cut##mpartistell", &g_mpArtistEllipsis);
                ImGui::TextDisabled("Lyrics");
                MenuSegment("##mplyricsalign", &g_mpLyricsAlign, k_align, 3);
            }

            MenuCheckbox("Waveform##mpwf", &g_mpWaveform);
            if (g_mpWaveform) {
                static const char* const k_wfPos[] = { "Before title", "After title", "Free" };
                MenuSegment("##wfpos", &g_mpWaveformPos, k_wfPos, 3);
                MenuSliderF("Amplitude##wfsen", &g_wfSensitivity, 1.f, 12.f, "%.1fx");
                if (g_mpWaveformPos == 2) {
                    MenuSliderF("X##wffreex", &g_mpWfFreeX, 0.f, 3840.f, "%.0f px");
                    MenuSliderF("Y##wffreey", &g_mpWfFreeY, 0.f, 2160.f, "%.0f px");
                }
            }

            MenuCheckbox("Lyrics##mplyrics", &g_mpShowLyrics);
            if (g_mpShowLyrics) {
                MenuCheckbox("Above player##mplyrabove", &g_mpLyricsAbove);
                MenuCheckbox("Beside player##mplyrsid", &g_mpLyricsSide);
                MenuCheckbox("Lyrics only (hide player)##mplyronly", &g_mpLyricsOnly);
                MenuSliderF("X offset##mplyrxoff", &g_mpLyricsXOff, -500.f, 500.f, "%.0f px");
                MenuSliderF("Y offset##mplyryoff", &g_mpLyricsYOff, -500.f, 500.f, "%.0f px");
                MenuSliderF("Scale##mplyrscale",   &g_mpLyricsScale,  0.5f, 2.f,   "%.2f");
                MenuSliderI("Rows##mplyrrows",     &g_mpLyricsRows,   1,    9);
                MenuSliderF("Timing offset##mplyroff", &g_mpLyricsOffset, -5.f, 5.f, "%+.2f s");
                MenuCheckbox("Background##mplyrbgdraw",   &g_mpLyricsBgDraw);
                MenuCheckbox("Gradient blob##mplyrblob",  &g_mpLyricsBlob);
                MenuCheckbox("Precipitation##mplyrprecip", &g_mpLyricsPrecip);
                MenuCheckbox("Limit width##mplyrclamp", &g_mpLyricsClampWidth);
                if (g_mpLyricsClampWidth) {
                    MenuSliderF("Max width##mplyrclampw", &g_mpLyricsClampW, 80.f, 600.f, "%.0f px");
                    MenuCheckbox("Truncate instead of wrap##mplyrellipsis", &g_mpLyricsEllipsis);
                }
                MenuCheckbox("Avoid watermark##mplyravoidwm", &g_mpLyricsAvoidWm);
                MenuCheckbox("Push player and watermark##mplyrshift", &g_mpLyricsShiftBoxes);
                MenuColorGrid("##mplyrcol", { { "Lyrics color", g_mpLyricsColor, 4 } });
            }

            if (!g_mpCopyWmStyle)
            {
                MenuSectionLabel("STYLE");
                if (MenuDropdown("Font##mpfont")) {
                    MenuFontPicker("##mpfontpick", g_mpFontIndex, g_mpFontFile, g_mpFontFlags, 6);
                    ImGui::Spacing();
                }
                MenuColorGrid("##mpcolors", {
                    { "Background", g_mpBgColor, 4 }, { "Title", g_mpTitleColor, 3 },
                    { "Artist", g_mpArtistColor, 3 } });
                MenuCheckbox("Background##mpbgdraw", &g_mpBgDraw);
                MenuSliderF("Rounding##mprounding", &g_mpBgRounding, 0.f, 11.f, "%.1f");
                MenuCornerFlags("##mpcorners", g_mpBgCornerFlags);
                MenuCheckbox("Background blur##bgblur", &g_bgBlur);
                if (g_bgBlur) MenuSliderF("Blur strength##bgblursigma", &g_bgBlurSigma, 0.5f, 40.f, "%.1f");
                MenuOutlineLayers("##mpoutline", g_mpOutlineLayers, g_mpOutlineLayerCount);
                MenuBlob("##mpblob", g_mpBlobEnable, &g_mpBlobUseSheen, g_mpBlobColor, g_mpBlobAlpha, g_mpBlobSize, g_mpBlobSpeed);
                MenuPrecip("##mpprecip", g_mpPrecipEnable, g_mpPrecipMode, g_mpPrecipCount, g_mpPrecipSpeed,
                           g_mpPrecipSize, g_mpPrecipWind, g_mpPrecipColor);
                MenuBoxShadow("##mpshadow", g_mpBoxShadow, g_mpBoxShadowOffX, g_mpBoxShadowOffY, g_mpBoxShadowBlur, g_mpBoxShadowColor);
                MenuSideGradients("##mpgrad", g_mpBgGrad);
                MenuInsets("##mpinset", g_mpBgInsetL, g_mpBgInsetR, g_mpBgInsetT, g_mpBgInsetB);
            }
            ImGui::Spacing();
        }

        // ---- Weapon HUD ----------------------------------------------
        MenuSectionLabel("WEAPON HUD");
        MenuCheckbox("Enable##wpnhenable", &g_enableWeaponHud);
        if (g_enableWeaponHud && MenuDropdown("Settings##wpndrop"))
        {
            MenuSliderF("Icon size##wpnhsz",         &g_wpnHudIconSz,      16.f,  80.f,  "%.0f px", false);
            MenuSliderF("Active icon scale##wpnhas", &g_wpnHudActiveScale, 0.5f,  3.f,   "%.2fx",   false);
            MenuCheckbox("Smooth scaling##wpnhss", &g_wpnHudActiveScaleSmooth);
            if (g_wpnHudActiveScaleSmooth)
                MenuSliderF("Scale speed##wpnhsspd", &g_wpnHudActiveScaleSpeed, 1.f, 30.f, "%.1f", false);
            MenuSliderF("Slot gap##wpnhgap",         &g_wpnHudSlotGap,     0.f,   24.f,  "%.0f px", false);
            MenuSliderF("Icon rotation##wpnhrot",    &g_wpnHudIconRotation, -180.f, 180.f, "%.0f deg", false);
            MenuCheckbox("Flip icons##wpnhflip", &g_wpnHudFlipIcons);
            MenuColorGrid("##wpnhcolors", {
                { "Active", g_wpnHudActiveColor, 4 }, { "Inactive", g_wpnHudInactiveColor, 4 } });

            MenuSectionLabel("POSITION");
            {
                static const char* const k_originH[] = { "Right", "Left", "Center" };
                static const char* const k_originV[] = { "Center", "Top", "Bottom" };
                MenuSegment("##wpnhoH", &g_wpnHudOriginH, k_originH, 3);
                MenuSegment("##wpnhoV", &g_wpnHudOriginV, k_originV, 3);
            }
            if (g_wpnHudOriginH == 2)
                MenuSliderF("X offset##wpnhx", &g_wpnHudX, -400.f, 400.f, "%.0f px", false);
            else
                MenuSliderF("Edge offset X##wpnhx", &g_wpnHudX, 0.f, 300.f, "%.0f px", false);
            if (g_wpnHudOriginV == 0)
                MenuSliderF("Y offset##wpnhy", &g_wpnHudY, -400.f, 400.f, "%.0f px", false);
            else
                MenuSliderF("Edge offset Y##wpnhy", &g_wpnHudY, 0.f, 800.f, "%.0f px", false);

            MenuSectionLabel("SLOT NUMBERS");
            MenuCheckbox("Show##wpnhsnum", &g_wpnHudShowNum);
            if (g_wpnHudShowNum) {
                static const char* const k_numColor[] = { "Icon", "Glow", "Custom" };
                static const char* const k_numPos[]   = { "Top", "Center", "Bottom" };
                MenuSliderF("Scale##wpnhns", &g_wpnHudNumScale, 0.2f, 2.f, "%.2f", false);
                ImGui::TextDisabled("Color");
                MenuSegment("##wpnncc", &g_wpnHudNumColorMode, k_numColor, 3);
                if (g_wpnHudNumColorMode == 2)
                    MenuColorGrid("##wpnncust", { { "Number color", g_wpnHudNumCustomColor, 4 } });
                ImGui::TextDisabled("Position");
                MenuSegment("##wpnnp", &g_wpnHudNumPos, k_numPos, 3);
                MenuSliderF("Offset X##wpnnox", &g_wpnHudNumOffX, -100.f, 100.f, "%.0f px", false);
                MenuSliderF("Offset Y##wpnnoy", &g_wpnHudNumOffY, -100.f, 100.f, "%.0f px", false);
                if (MenuDropdown("Font##wpnhnnf")) {
                    MenuFontPicker("##wpnnumfont", g_wpnHudNumFontIndex, g_wpnHudNumFontFile, g_wpnHudNumFontFlags, 4);
                    ImGui::Spacing();
                }
                MenuCheckbox("Drop shadow##wpnnds", &g_wpnHudNumShadowEnabled);
                if (g_wpnHudNumShadowEnabled) {
                    MenuSliderF("Shadow X##wpnndsx", &g_wpnHudNumShadowOffX, -10.f, 10.f, "%.1f", false);
                    MenuSliderF("Shadow Y##wpnndsy", &g_wpnHudNumShadowOffY, -10.f, 10.f, "%.1f", false);
                    MenuColorGrid("##wpnndscol", { { "Shadow color", g_wpnHudNumShadowColor, 4 } });
                }
                MenuCheckbox("Outline##wpnnout", &g_wpnHudNumOutlineEnabled);
                if (g_wpnHudNumOutlineEnabled) {
                    MenuSliderF("Outline thickness##wpnnoutthk", &g_wpnHudNumOutlineThickness, 0.5f, 4.f, "%.1f", false);
                    MenuColorGrid("##wpnnoutcol", { { "Outline color", g_wpnHudNumOutlineColor, 4 } });
                }
            }

            MenuSectionLabel("WEAPON NAME");
            MenuCheckbox("Show##wpnhn", &g_enableWpnHudName);
            if (g_enableWpnHudName) {
                static const char* const k_namePos[]   = { "Above icon", "Below icon" };
                static const char* const k_nameColor[] = { "Glow", "Icon", "Custom" };
                MenuSegment("##wpnhnpos", &g_wpnHudNamePos, k_namePos, 2);
                ImGui::TextDisabled("Color");
                MenuSegment("##wpnnmcc", &g_wpnHudNameColorMode, k_nameColor, 3);
                if (g_wpnHudNameColorMode == 2)
                    MenuColorGrid("##wpnnmcust", { { "Name color", g_wpnHudNameCustomColor, 4 } });
                MenuSliderF("Offset X##wpnnmox", &g_wpnHudNameOffX, -200.f, 200.f, "%.0f px", false);
                MenuSliderF("Offset Y##wpnnmoy", &g_wpnHudNameOffY, -200.f, 200.f, "%.0f px", false);
                MenuSliderF("Scale##wpnhnsz",    &g_wpnHudNameScale, 0.4f, 2.f, "%.2f", false);
                MenuSliderI("Max length##wpnhnamemx", &g_wpnHudNameMaxChars, 0, 80,
                            g_wpnHudNameMaxChars == 0 ? "no limit" : "%d chars", false);
                if (MenuDropdown("Font##wpnhnamef")) {
                    MenuFontPicker("##wpnnamefont", g_wpnHudNameFontIndex, g_wpnHudNameFontFile, g_wpnHudNameFontFlags, 4);
                    ImGui::Spacing();
                }
                MenuCheckbox("Drop shadow##wpnnameds", &g_wpnHudNameShadowEnabled);
                if (g_wpnHudNameShadowEnabled) {
                    MenuSliderF("Shadow X##wpnnamedsx", &g_wpnHudNameShadowOffX, -10.f, 10.f, "%.1f", false);
                    MenuSliderF("Shadow Y##wpnnamedsy", &g_wpnHudNameShadowOffY, -10.f, 10.f, "%.1f", false);
                    MenuColorGrid("##wpnnamedscol", { { "Shadow color", g_wpnHudNameShadowColor, 4 } });
                }
                MenuCheckbox("Outline##wpnnameout", &g_wpnHudNameOutlineEnabled);
                if (g_wpnHudNameOutlineEnabled) {
                    MenuSliderF("Outline thickness##wpnnameoutthk", &g_wpnHudNameOutlineThickness, 0.5f, 4.f, "%.1f", false);
                    MenuColorGrid("##wpnnameoutcol", { { "Outline color", g_wpnHudNameOutlineColor, 4 } });
                }
            }

            MenuSectionLabel("EFFECTS");
            MenuCheckbox("Glow##wpnhglen", &g_wpnHudGlowEnabled);
            if (g_wpnHudGlowEnabled) {
                MenuSliderF("Glow radius##wpnhgr",    &g_wpnHudGlowRadius, 2.f, 32.f, "%.0f px", false);
                MenuSliderF("Glow bolden##wpnhgb",    &g_wpnHudGlowBolden, 0.f, 20.f, "%.0f px", false);
                MenuSliderF("Glow intensity##wpnhga", &g_wpnHudGlowAlpha,  0.f, 1.f,  "%.2f",    false);
                MenuColorGrid("##wpnhglowcols", {
                    { "Slot 1", g_wpnHudGlowColors[0], 4 }, { "Slot 2", g_wpnHudGlowColors[1], 4 },
                    { "Slot 3", g_wpnHudGlowColors[2], 4 }, { "Slot 4", g_wpnHudGlowColors[3], 4 },
                    { "Slot 5", g_wpnHudGlowColors[4], 4 } }, 3);
            }
            MenuCheckbox("Separators##wpnhsepon", &g_wpnHudSepEnabled);
            if (g_wpnHudSepEnabled) {
                MenuSliderF("Separator width##wpnhsepw",  &g_wpnHudSepWidth,  0.5f, 10.f,  "%.1f px", false);
                MenuSliderF("Separator height##wpnhseph", &g_wpnHudSepHeight, 0.1f, 1.5f,  "%.2f",    false);
                MenuColorGrid("##wpnhsepcol", { { "Separator color", g_wpnHudSepColor, 4 } });
            }
            MenuCheckbox("Custom slot names##wpnhcn", &g_wpnHudCustomNames);
            if (g_wpnHudCustomNames) {
                for (int i = 0; i < 5; ++i) {
                    char hint[16];
                    snprintf(hint, sizeof(hint), "Slot %d", i + 1);
                    ImGui::PushID(i);
                    ImGui::SetNextItemWidth(-1.f);
                    ImGui::InputTextWithHint("##wpnhcn", hint, g_wpnHudCustomName[i], 64);
                    ImGui::PopID();
                }
            }
            MenuCheckbox("Fade out##wpnhfade", &g_wpnHudFade);
            if (g_wpnHudFade) {
                MenuSliderF("Fade delay##wpnhfadedly", &g_wpnHudFadeDelay, 0.f,  5.f,  "%.1f s", false);
                MenuSliderF("Fade speed##wpnhfadespd", &g_wpnHudFadeSpeed, 0.1f, 10.f, "%.1f",   false);
            }
            MenuCheckbox("Background gradient##wpnhbgen", &g_wpnHudBgEnabled);
            if (g_wpnHudBgEnabled) {
                MenuSliderF("Width##wpnhbgw",                &g_wpnHudBgWidth,   20.f,  600.f, "%.0f px", false);
                MenuSliderF("Height (horizontal)##wpnhbghh", &g_wpnHudBgHeightH,  4.f,  300.f, "%.0f px", false);
                MenuSliderF("Extra height (vertical)##wpnhbgeh", &g_wpnHudBgExtraH, -30.f, 60.f, "%.0f px", false);
                MenuSliderF("X offset##wpnhbgox",            &g_wpnHudBgOffX,  -200.f, 200.f,  "%.0f px", false);
                MenuSliderF("Y offset##wpnhbgoy",            &g_wpnHudBgOffY,  -200.f, 200.f,  "%.0f px", false);
                MenuCheckbox("From screen edge##wpnhbgse", &g_wpnHudBgScreenEdge);
                MenuSliderF("Far fade start##wpnhbgfs", &g_wpnHudBgFadeStart, 0.f, 1.f, "%.2f", false);
                MenuCheckbox("Fade near side##wpnhbgfn", &g_wpnHudBgFadeNear);
                if (g_wpnHudBgFadeNear)
                    MenuSliderF("Near fade start##wpnhbgfns", &g_wpnHudBgFadeNearStart, 0.f, 1.f, "%.2f", false);
                MenuColorGrid("##wpnhbgcols", {
                    { "Near", g_wpnHudBgColorNear, 4 }, { "Far", g_wpnHudBgColorFar, 4 } });
                MenuCheckbox("Border strips##wpnhbgbord", &g_wpnHudBgBorderEnabled);
                if (g_wpnHudBgBorderEnabled) {
                    MenuSliderF("Border height##wpnhbgbordh", &g_wpnHudBgBorderHeight, 1.f, 20.f, "%.1f px", false);
                    MenuColorGrid("##wpnhbgbcols", {
                        { "Border near", g_wpnHudBgBorderColorNear, 4 }, { "Border far", g_wpnHudBgBorderColorFar, 4 } });
                }
            }
            ImGui::Spacing();
        }
    } // end s_tab == 5

    // ---- Tab 6: Skins ------------------------------------------
    if (s_tab == 6)
    {
        MenuSectionLabel("SKINS");
        MenuCheckbox("Enable##skinsenable", &g_enableSkins);
        if (g_enableSkins)
        {
            static const char* const k_sheenNames[] = {
                "None", "Team Shine", "Deadly Daffodil", "Manndarin",
                "Mean Green", "Agonizing Emerald", "Villainous Violet", "Hot Rod" };
            static const char* const k_ksEffectNames[] = {
                "None", "Fire Horns", "Cerebral Discharge", "Tornado",
                "Flames", "Singularity", "Incinerator", "Hypno-Beam" };
            static const char* const k_unusualNames[] = {
                "None", "Hot (701)", "Isotope (702)", "Cool (703)", "Energy Orb (704)" };
            static const int k_unusualIds[] = { 0, 701, 702, 703, 704 };
            static const char* const k_ksTierNames[] = { "None", "Basic", "Specialized", "Professional" };

            // weapons sorted by name, built once
            static int  s_wpnOrder[k_wpnCount];
            static bool s_wpnOrderInit = false;
            if (!s_wpnOrderInit) {
                for (int i = 0; i < k_wpnCount; i++) s_wpnOrder[i] = i;
                std::sort(s_wpnOrder, s_wpnOrder + k_wpnCount, [](int a, int b) {
                    return _stricmp(k_wpnList[a].name, k_wpnList[b].name) < 0;
                });
                s_wpnOrderInit = true;
            }

            ImGui::TextDisabled("Weapon");
            ImGui::SetNextItemWidth(-1.f);
            if (ImGui::BeginCombo("##skin_wpn", k_wpnList[s_skinWpnSel].name)) {
                for (int oi = 0; oi < k_wpnCount; oi++) {
                    const int i = s_wpnOrder[oi];
                    if (ImGui::Selectable(k_wpnList[i].name, s_skinWpnSel == i)) s_skinWpnSel = i;
                }
                ImGui::EndCombo();
            }

            const int selRedir = k_wpnList[s_skinWpnSel].redir;
            SkinCfg&  cfg      = g_skinCfgs[selRedir];
            const bool hasPaint = k_wpnList[s_skinWpnSel].canPaint ||
                                  (k_wpnList[s_skinWpnSel].stock != k_wpnList[s_skinWpnSel].redir);

            // point the warpaint combo at whatever this weapon's config holds
            auto SyncWarpaintSel = [&]() {
                s_skinWarpSel = 0;
                if (cfg.weaponSkinDef > 0) {
                    int pkForDef = -1;
                    for (int j = 0; j < k_skinDefCount; j++)
                        if (k_skinDefs[j].skinDef == cfg.weaponSkinDef) { pkForDef = k_skinDefs[j].pkId; break; }
                    if (pkForDef >= 0)
                        for (int i = 1; i < k_warpaintCount; i++)
                            if (k_warpaints[i].defindex == pkForDef) { s_skinWarpSel = i; break; }
                } else if (cfg.paintKitId > 0) {
                    for (int i = 0; i < k_warpaintCount; i++)
                        if (k_warpaints[i].defindex == cfg.paintKitId) { s_skinWarpSel = i; break; }
                }
            };
            if (s_skinLastWpn != s_skinWpnSel) {
                s_skinLastWpn = s_skinWpnSel;
                SyncWarpaintSel();
                s_skinWpFilter[0] = '\0';
            }

            MenuCheckbox("Enable for this weapon##skinwpnen", &cfg.enabled);

            MenuSectionLabel("WARPAINT");
            ImGui::BeginDisabled(!hasPaint);
            ImGui::SetNextItemWidth(-1.f);
            if (ImGui::BeginCombo("##skin_warp", k_warpaints[s_skinWarpSel].name))
            {
                ImGui::SetNextItemWidth(-1.f);
                ImGui::InputTextWithHint("##wpfilter", "Search", s_skinWpFilter, sizeof(s_skinWpFilter));
                for (int i = 0; i < k_warpaintCount; i++)
                {
                    const int pk = k_warpaints[i].defindex;
                    // 0 = "(none)"; pk 1-86 = weapon-specific skin (needs a 15xxx def for this
                    // weapon); 87+ = generic warpaint (needs a paintable weapon)
                    if (i != 0) {
                        if (pk > 0 && pk <= 86) { if (Skin_FindSpecificDef(selRedir, pk) == 0) continue; }
                        else if (!hasPaint) continue;
                    }
                    if (s_skinWpFilter[0]) {
                        std::string hay = k_warpaints[i].name, needle = s_skinWpFilter;
                        for (char& ch : hay)    ch = (char)tolower((unsigned char)ch);
                        for (char& ch : needle) ch = (char)tolower((unsigned char)ch);
                        if (hay.find(needle) == std::string::npos) continue;
                    }
                    if (ImGui::Selectable(k_warpaints[i].name, s_skinWarpSel == i)) {
                        s_skinWarpSel = i;
                        if (i == 0)                   { cfg.paintKitId = 0;  cfg.weaponSkinDef = 0; }
                        else if (pk > 0 && pk <= 86)  { cfg.paintKitId = 0;  cfg.weaponSkinDef = Skin_FindSpecificDef(selRedir, pk); }
                        else                          { cfg.paintKitId = pk; cfg.weaponSkinDef = 0; }
                        g_skinNeedsUpdate = true;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::TextDisabled("Paint kit ID");
            ImGui::SetNextItemWidth(-1.f);
            if (ImGui::InputInt("##skin_kit", &cfg.paintKitId, 1, 10)) {
                if (cfg.paintKitId < 0) cfg.paintKitId = 0;
                // a weapon-specific id (1-86) becomes that weapon's skin def
                if (cfg.paintKitId >= 1 && cfg.paintKitId <= 86) {
                    const int sd = Skin_FindSpecificDef(selRedir, cfg.paintKitId);
                    if (sd > 0) { cfg.weaponSkinDef = sd; cfg.paintKitId = 0; }
                } else {
                    cfg.weaponSkinDef = 0;
                }
                SyncWarpaintSel();
            }
            {
                const char* wearBand = cfg.wear < 0.07f ? "Factory New" :
                                       cfg.wear < 0.15f ? "Minimal Wear" :
                                       cfg.wear < 0.38f ? "Field-Tested" :
                                       cfg.wear < 0.45f ? "Well-Worn" : "Battle Scarred";
                char wearFmt[48];
                snprintf(wearFmt, sizeof(wearFmt), "%%.2f %s", wearBand);
                MenuSliderF("Wear##skin_wear", &cfg.wear, 0.f, 1.f, wearFmt);
            }
            ImGui::TextDisabled("Pattern seed");
            ImGui::SetNextItemWidth(-1.f);
            static const uint64_t k_step = 1, k_stepFast = 1000;
            ImGui::InputScalar("##skin_pat", ImGuiDataType_U64, &cfg.pattern, &k_step, &k_stepFast);
            ImGui::EndDisabled();
            if (!hasPaint) ImGui::TextDisabled("This weapon takes no warpaint");

            MenuSectionLabel("KILLSTREAK");
            ImGui::SetNextItemWidth(-1.f);
            ImGui::Combo("##skin_kstier", &cfg.ksTier, k_ksTierNames, IM_ARRAYSIZE(k_ksTierNames));
            if (cfg.ksTier >= 2) {
                ImGui::TextDisabled("Sheen");
                ImGui::SetNextItemWidth(-1.f);
                ImGui::Combo("##skin_sheen", &cfg.ksSheen, k_sheenNames, IM_ARRAYSIZE(k_sheenNames));
                // rewrites this weapon's row in the killstreak color table: sheen tint and eye glow
                const float sw = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;
                MenuCheckbox("Custom sheen color##skin_kscolor", &cfg.ksCustomColor,
                             cfg.ksCustomColor ? ImGui::GetContentRegionAvail().x - sw : 0.f);
                if (cfg.ksCustomColor) { ImGui::SameLine(); ColorPickerPopup(cfg.ksColor, 3, "##skin_kscolorpick"); }
            }
            if (cfg.ksTier == 3) {
                ImGui::TextDisabled("Kill effect");
                ImGui::SetNextItemWidth(-1.f);
                ImGui::Combo("##skin_kseff", &cfg.ksEffect, k_ksEffectNames, IM_ARRAYSIZE(k_ksEffectNames));
                MenuCheckbox("Eye glow uses sheen color##kseyesheen", &cfg.ksUseSheenColor);
            }

            MenuSectionLabel("UNUSUAL");
            int unusualSel = 0;
            for (int i = 0; i < IM_ARRAYSIZE(k_unusualIds); i++)
                if (k_unusualIds[i] == cfg.unusual) { unusualSel = i; break; }
            ImGui::SetNextItemWidth(-1.f);
            if (ImGui::Combo("##skin_unusual", &unusualSel, k_unusualNames, IM_ARRAYSIZE(k_unusualNames)))
                cfg.unusual = k_unusualIds[unusualSel];
        }
    } // end s_tab == 6

    // ---- Tab 7: Images -----------------------------------------
    if (s_tab == 7)
    {
        MenuSectionLabel("IMAGES");
        MenuCheckbox("Enable##imgovrl", &g_enableImageOverlay);
        if (g_enableImageOverlay)
        {
            static const char* const k_slotLabels[k_imgMaxSlots] = { "1", "2", "3", "4", "5", "6", "7", "8" };
            MenuSegment("##imgslot", &g_imgSelectedSlot, k_slotLabels, k_imgMaxSlots);
            g_imgSelectedSlot = std::clamp(g_imgSelectedSlot, 0, k_imgMaxSlots - 1);
            OverlayImageSlot& sl = g_imgSlots[g_imgSelectedSlot];

            MenuCheckbox("Show this image##imen", &sl.enabled);
            {
                const float btnW = 60.f;
                const float gap  = ImGui::GetStyle().ItemSpacing.x;
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - btnW - gap);
                ImGui::InputText("##imgpath", sl.path, MAX_PATH, ImGuiInputTextFlags_ReadOnly);
                ImGui::SameLine(0, gap);
                if (ImGui::Button("Browse##imgbr", ImVec2(btnW, 0.f)))
                    FilePick_Start(g_imgSelectedSlot);
            }
            if (sl.path[0] && ImGui::Button("Reload##imgrld")) sl.needsLoad = true;
            if (sl.loaded) {
                ImGui::SameLine();
                if (ImGui::Button("Clear##imgclr")) sl.ReleaseTextures();
                const int fc = (int)sl.frames.size();
                ImGui::TextDisabled("%dx%d, %d frame%s", sl.texW, sl.texH, fc, fc == 1 ? "" : "s");
            } else if (sl.path[0]) {
                ImGui::TextDisabled("Not loaded");
            }

            MenuSliderF("Scale##imgscale", &sl.scale, 0.05f, 5.f, "%.2f");
            float rotDeg = sl.rotation * (180.f / 3.14159265f);
            if (MenuSliderF("Rotation##imgrot", &rotDeg, -180.f, 180.f, "%.0f deg"))
                sl.rotation = rotDeg * (3.14159265f / 180.f);
            MenuSliderF("Opacity##imgalpha", &sl.alpha, 0.f, 1.f, "%.2f");
            MenuCheckbox("Pixel art (no filtering)##imgpix", &sl.pixelArt);

            static const char* const k_anchors[] = { "Free", "Next to watermark" };
            MenuSegment("##imganchor", &sl.anchor, k_anchors, 2);
            if (sl.anchor == 0) {
                MenuSliderF("X##imgposX", &sl.posX, -4000.f, 4000.f, "%.0f px");
                MenuSliderF("Y##imgposY", &sl.posY, -4000.f, 4000.f, "%.0f px");
            } else {
                MenuSliderF("Offset X##imgaox", &sl.anchorOffX, -500.f, 500.f, "%.0f px");
                MenuSliderF("Offset Y##imgaoy", &sl.anchorOffY, -500.f, 500.f, "%.0f px");
            }
        }
    } // end s_tab == 7

    // ---- Tab 8: Config -----------------------------------------
    if (s_tab == 8)
    {
        MenuSectionLabel("CONFIGS");

        // the folder listing, refreshed once a second or after we touched a file -- not every frame
        static std::vector<std::string> s_cfgs;
        static ULONGLONG s_cfgScanAt = 0;
        static bool      s_cfgDirty  = true;
        if (s_cfgDirty || GetTickCount64() - s_cfgScanAt > 1000) {
            s_cfgs      = ListConfigs();
            s_cfgScanAt = GetTickCount64();
            s_cfgDirty  = false;
        }

        static char s_newCfgName[64] = {};
        const float gap  = ImGui::GetStyle().ItemSpacing.x;
        const float addW = 70.f;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - addW - gap);
        ImGui::InputTextWithHint("##newcfgname", "new config name", s_newCfgName, sizeof(s_newCfgName));
        ImGui::SameLine(0, gap);
        if (ImGui::Button("Create##cfgcreate", ImVec2(addW, 0)))
        {
            std::string name = s_newCfgName;
            name.erase(0, name.find_first_not_of(' '));
            name.erase(name.find_last_not_of(' ') + 1);
            if (!name.empty()) {
                if (name.size() < 4 || name.compare(name.size() - 4, 4, ".cfg") != 0) name += ".cfg";
                SaveConfig(ConfigFullPath(name));
                s_newCfgName[0] = '\0';
                s_cfgDirty = true;
            }
        }

        static std::string s_cfgSelected = "default.cfg";
        static bool        s_confirmDelete = false;
        if (s_cfgs.empty())
            ImGui::TextDisabled("No saved configs");
        for (const std::string& c : s_cfgs)
            if (ImGui::Selectable(c.c_str(), s_cfgSelected == c)) { s_cfgSelected = c; s_confirmDelete = false; }

        const bool hasSelection = std::find(s_cfgs.begin(), s_cfgs.end(), s_cfgSelected) != s_cfgs.end();
        ImGui::BeginDisabled(!hasSelection);
        const float bw3 = (ImGui::GetContentRegionAvail().x - gap * 2.f) / 3.f;
        if (ImGui::Button("Load##cfgload", ImVec2(bw3, 0)))   LoadConfig(ConfigFullPath(s_cfgSelected));
        ImGui::SameLine(0, gap);
        if (ImGui::Button("Save##cfgsave", ImVec2(bw3, 0)))   SaveConfig(ConfigFullPath(s_cfgSelected));
        ImGui::SameLine(0, gap);
        if (ImGui::Button("Delete##cfgdelete", ImVec2(bw3, 0))) s_confirmDelete = true;
        ImGui::EndDisabled();

        if (s_confirmDelete && hasSelection)
            ImGui::OpenPopup("##confirmdelpopup");
        if (ImGui::BeginPopup("##confirmdelpopup"))
        {
            ImGui::Text("Delete %s?", s_cfgSelected.c_str());
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.10f, 0.10f, 1.f));
            if (ImGui::Button("Delete##cfgconfirm", ImVec2(110.f, 0))) {
                DeleteConfig(s_cfgSelected);
                s_cfgDirty = true;
                s_confirmDelete = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopStyleColor();
            ImGui::SameLine();
            if (ImGui::Button("Cancel##cfgcancel", ImVec2(110.f, 0))) {
                s_confirmDelete = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        else s_confirmDelete = false;

        MenuSectionLabel("MENU");
        {
            const std::string keyLbl = g_menuKeyCapture ? std::string("Press a key (Esc cancels)")
                                                        : std::string("Menu key: ") + VkToName(g_menuKey);
            char keyBtnId[96];
            snprintf(keyBtnId, sizeof(keyBtnId), "%s##menukeybind", keyLbl.c_str());
            if (ImGui::Button(keyBtnId, ImVec2(-1.f, 0)))
                g_menuKeyCapture = !g_menuKeyCapture;
        }
        if (MenuDropdown("Menu font##menufont")) {
            MenuFontPicker("##menufontpick", g_menuFontIndex, g_menuFontFile, g_menuFontFlags, 6);
            MenuSliderF("Font scale##menufontscale", &g_menuFontScale, 0.5f, 2.f, "%.2f");
            ImGui::Spacing();
        }
    } // end s_tab == 8

    ImGui::EndChild();       // ##left
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    // ---- Right quick-settings column ---------------------------
    ImGui::SetCursorPos(ImVec2(sidebarW + col1W + 1.f, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.f, 14.f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::BeginChild("##right", ImVec2(col2W - 1.f, menuH), ImGuiChildFlags_AlwaysUseWindowPadding);

    // Tab 0: Watermark — thresholds, position, colors, background
    if (s_tab == 0) {
        MenuSectionLabel("THRESHOLDS");
        MenuSliderI("FPS red below##rthfps",   &g_threshFPS,  10, 240);
        MenuSliderI("Ping red above##rthping", &g_threshPing, 10, 500, "%d ms");
        MenuSliderI("Loss red above##rthloss", &g_threshLoss,  0,  50, "%d %%");

        MenuSectionLabel("POSITION");
        MenuAnchor6("##wmpos", &g_watermarkPos);
        MenuSliderF("Y offset##ryoff", &g_watermarkYOffset,   -1.f,   1.f, "%.2f");
        MenuSliderF("X offset##rxoff", &g_watermarkXOffset, -800.f, 800.f, "%.0f px");

        MenuSectionLabel("COLORS");
        MenuColorGrid("##wmcolors", {
            { "Background", g_wmBgColor,    4 }, { "Labels", g_wmDimColor,   3 },
            { "Values",     g_wmTextColor,  3 }, { "Alert",  g_wmAlertColor, 3 },
            { "Title",      g_wmTitleColor, 3 } });

        MenuSectionLabel("BACKGROUND");
        MenuSliderF("Rounding##rwmrnd", &g_wmBgRounding, 0.f, 11.f, "%.1f");
        MenuCornerFlags("##wmcorners", g_wmBgCornerFlags);
        MenuCheckbox("Background blur##rbgblur", &g_bgBlur);
        if (g_bgBlur) MenuSliderF("Blur strength##rbgblursigma", &g_bgBlurSigma, 0.5f, 40.f, "%.1f");
        MenuOutlineLayers("##wmoutline", g_wmOutlineLayers, g_wmOutlineLayerCount);
        MenuBlob("##wmblob", g_wmBlobEnable, &g_wmBlobUseSheen, g_wmBlobColor, g_wmBlobAlpha, g_wmBlobSize, g_wmBlobSpeed);
        MenuPrecip("##wmprecip", g_wmPrecipEnable, g_wmPrecipMode, g_wmPrecipCount, g_wmPrecipSpeed,
                   g_wmPrecipSize, g_wmPrecipWind, g_wmPrecipColor);
        MenuBoxShadow("##wmshadow", g_wmBoxShadow, g_wmBoxShadowOffX, g_wmBoxShadowOffY, g_wmBoxShadowBlur, g_wmBoxShadowColor);
        MenuCheckbox("Kill glow##rwmkillglow", &g_wmKillGlowEnable);
        if (g_wmKillGlowEnable) {
            MenuColorGrid("##wmkgcol", { { "Glow color", g_wmKillGlowColor, 4 } });
            MenuSliderF("Duration##rwmkgdur", &g_wmKillGlowDuration, 0.2f, 5.f,  "%.1f s");
            MenuSliderF("Spread##rwmkgspr",   &g_wmKillGlowSpread,   1.f,  60.f, "%.0f px");
        }
        MenuSideGradients("##wmgrad", g_wmBgGrad);
        MenuInsets("##wminset", g_wmBgInsetL, g_wmBgInsetR, g_wmBgInsetT, g_wmBgInsetB);
    }
    // Tab 1: Names — alias list
    if (s_tab == 1) {
        AliasLockGuard aliasLk;   // pure ImGui below, no engine calls under the lock
        MenuSectionLabel("GROUPS");
        const float gap = ImGui::GetStyle().ItemSpacing.x;
        const float sw  = ImGui::GetFrameHeight();
        for (int gi = 0; gi < (int)g_playerGroups.size(); ++gi) {
            ImGui::PushID(gi);
            char gbuf[32]{};
            strncpy_s(gbuf, g_playerGroups[gi].c_str(), _TRUNCATE);
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - sw - gap);
            if (ImGui::InputText("##grpname", gbuf, sizeof(gbuf)))
                g_playerGroups[gi] = gbuf;
            ImGui::SameLine(0, gap);
            const bool remove = ImGui::Button("X##grprmv", ImVec2(sw, sw));
            ImGui::PopID();
            if (remove) {
                // drop bit gi from every mask and shift the bits above it down
                for (auto& [k, ae] : g_aliases) {
                    const uint32_t lo = ae.groupMask & ((1u << gi) - 1u);
                    const uint32_t hi = uint32_t((uint64_t(ae.groupMask) >> (gi + 1)) << gi);
                    ae.groupMask = lo | hi;
                }
                g_playerGroups.erase(g_playerGroups.begin() + gi);
                break;
            }
        }
        {
            static char s_newGrpBuf[32] = {};
            const float addW = 70.f;
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - addW - gap);
            ImGui::InputTextWithHint("##newgrp", "new group", s_newGrpBuf, sizeof(s_newGrpBuf));
            ImGui::SameLine(0, gap);
            // groupMask is 32 bits; config load caps at 32 too
            if (ImGui::Button("+ Add##addgrp", ImVec2(addW, 0.f)) && s_newGrpBuf[0] && g_playerGroups.size() < 32) {
                g_playerGroups.push_back(s_newGrpBuf);
                s_newGrpBuf[0] = '\0';
            }
        }

        MenuSectionLabel("ALIASES");
        if (g_aliases.empty())
            ImGui::TextDisabled("Right-click a player in the scoreboard to add one.");

        // sorted for a stable order
        std::vector<uint64_t> aliasKeys;
        aliasKeys.reserve(g_aliases.size());
        for (auto& [k, _] : g_aliases) aliasKeys.push_back(k);
        std::sort(aliasKeys.begin(), aliasKeys.end());

        ImGui::BeginChild("##aliasscroll", ImVec2(-1.f, ImGui::GetContentRegionAvail().y - 4.f));
        for (int ai = 0; ai < (int)aliasKeys.size(); ++ai) {
            const uint64_t k = aliasKeys[ai];
            auto it = g_aliases.find(k);
            if (it == g_aliases.end()) continue;
            AliasEntry& ae = it->second;

            ImGui::PushID(ai);
            char sid8[12];
            snprintf(sid8, sizeof(sid8), "%.8llu", (unsigned long long)(k % 100000000ULL));
            ImGui::TextDisabled("%s", sid8);
            ImGui::SetNextItemWidth(-1.f);
            ImGui::InputText("##aname", ae.displayName, sizeof(ae.displayName));

            char grpPreview[128] = "no group";
            bool first = true;
            for (int gi = 0; gi < (int)g_playerGroups.size() && gi < 32; ++gi) {
                if (!(ae.groupMask & (1u << gi))) continue;
                if (first) { grpPreview[0] = '\0'; first = false; }
                else strncat_s(grpPreview, ", ", _TRUNCATE);
                strncat_s(grpPreview, g_playerGroups[gi].c_str(), _TRUNCATE);
            }
            ImGui::SetNextItemWidth(-1.f);
            if (ImGui::BeginCombo("##agrp", grpPreview)) {
                for (int gi = 0; gi < (int)g_playerGroups.size() && gi < 32; ++gi) {
                    bool sel = (ae.groupMask & (1u << gi)) != 0;
                    ImGui::PushID(gi);
                    if (ImGui::Selectable(g_playerGroups[gi].c_str(), sel, ImGuiSelectableFlags_DontClosePopups))
                        ae.groupMask ^= (1u << gi);
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            MenuCheckbox("Use alias in scoreboard##acn", &ae.changeNameInScoreboard);
            MenuCheckbox("Notify when on server##anot", &ae.notifyOnServer);
            const bool remove = ImGui::Button("Remove alias", ImVec2(-1.f, 0.f));
            ImGui::Spacing();
            ImGui::PopID();
            if (remove) g_aliases.erase(it);
        }
        ImGui::EndChild();
    }
    // Tab 3: Visuals — viewmodel
    if (s_tab == 3) {
        MenuSectionLabel("VIEWMODEL");
        MenuCheckbox("Custom sway##rvmsway", &g_enableViewmodelSway);
        if (g_enableViewmodelSway) {
            MenuSliderF("Sway scale##rvmswsc",  &g_vmSwayScale,  -10.f, 10.f, "%.2f");
            MenuSliderF("Sway interp##rvmswip", &g_vmSwayInterp,   0.f, 10.f, "%.2f");
            if (ImGui::Button("CS:GO sway##rvmswcs")) { g_vmSwayScale = 1.6f; g_vmSwayInterp = 0.1f; }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("CS:GO's sway values: scale 1.6, interp 0.1\n(TF runs the same sway code)");
        }
        MenuCheckbox("Remove bob##rvmbob", &g_removeViewmodelBob);

        // CS:GO bob -- sliders use CS:GO's own cvar ranges
        ImGui::BeginDisabled(g_removeViewmodelBob);
        MenuCheckbox("CS:GO bob##rvmcsbob", &g_vmCsBob);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("CS:GO's weapon bob instead of TF's: gun lowers the faster you run,\n"
                              "less bob in the air, bob speed follows your class speed");
        if (g_vmCsBob) {
            MenuSliderF("Lower amount##rvmcslow", &g_vmCsBobLower, 5.f, 30.f, "%.1f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("cl_bob_lower_amt: how far the gun drops at full run speed");
            MenuSliderF("Bob up/down##rvmcsvert", &g_vmCsBobVert, 0.1f, 2.f, "%.2f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("cl_bobamt_vert");
            MenuSliderF("Bob side##rvmcslat", &g_vmCsBobLat, 0.1f, 2.f, "%.2f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("cl_bobamt_lat");
            MenuSliderF("Bob cycle##rvmcscyc", &g_vmCsBobCycle, 0.1f, 2.f, "%.2f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("cl_bobcycle: higher = slower steps");
            MenuSliderF("Bob up time##rvmcsup", &g_vmCsBobUp, 0.05f, 0.95f, "%.2f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("cl_bobup: share of each step spent rising");
            if (ImGui::Button("CS:GO defaults##rvmcsdef")) {
                g_vmCsBobLower = 21.f; g_vmCsBobVert = 0.25f; g_vmCsBobLat = 0.4f;
                g_vmCsBobCycle = 0.98f; g_vmCsBobUp = 0.5f;
            }
        }
        ImGui::EndDisabled();
        MenuCheckbox("CS:GO jump lower##rvmcsgl", &g_vmCsGunLower);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("CS:GO: gun drops a little while you're in the air");
        if (g_vmCsGunLower) {
            MenuSliderF("Lower angle##rvmcsgla", &g_vmCsGunLowerAngle, 0.f, 20.f, "%.1f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("cl_gunlowerangle (CS:GO 2)");
            MenuSliderF("Lower speed##rvmcsgls", &g_vmCsGunLowerSpeed, 0.01f, 2.f, "%.2f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("cl_gunlowerspeed (CS:GO 0.1, per frame like CS:GO)");
        }
        MenuCheckbox("CS:GO landing dip##rvmcsld", &g_vmCsLandDip);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("CS:GO: gun dips when you land (viewmodel only, camera untouched)");
        if (g_vmCsLandDip) {
            MenuSliderF("Dip##rvmcslda", &g_vmCsLandDipAmt, 0.f, 20.f, "%.0f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("cl_headbob_land_dip_amt (CS:GO 4, whole units)");
            MenuSliderF("Dip time##rvmcsldt", &g_vmCsLandDipTime, 0.03f, 0.5f, "%.2fs");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("seconds to the bottom of the dip (CS:GO 0.25)\nlower = faster, whole dip lasts 1.5x this");
        }
        MenuCheckbox("Offset##rvmoff", &g_enableViewmodelOffset);
        if (g_enableViewmodelOffset) {
            MenuSliderF("X##rvmox", &g_vmOffsetX, -50.f, 50.f, "%.2f");
            MenuSliderF("Y##rvmoy", &g_vmOffsetY, -50.f, 50.f, "%.2f");
            MenuSliderF("Z##rvmoz", &g_vmOffsetZ, -50.f, 50.f, "%.2f");
            MenuCheckbox("Separate v_model offset##rvmosep", &g_vmOffsetSeparate);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("own offset for offhand v_models\n(spy invis watch, dead ringer, cloak and dagger)");
            if (g_vmOffsetSeparate) {
                MenuSliderF("X##rvmo2x", &g_vmOffset2X, -50.f, 50.f, "%.2f");
                MenuSliderF("Y##rvmo2y", &g_vmOffset2Y, -50.f, 50.f, "%.2f");
                MenuSliderF("Z##rvmo2z", &g_vmOffset2Z, -50.f, 50.f, "%.2f");
            }
        }

        {
            const bool was = g_vmAlphaEnable;
            MenuCheckbox("Transparent##rvmalpha", &g_vmAlphaEnable);
            if (g_vmAlphaEnable && !was && g_vmAlpha >= 0.999f) g_vmAlpha = 0.5f;   // 1 = no-op, looks broken
            if (g_vmAlphaEnable) {
                MenuSliderF("Alpha##rvmalphav", &g_vmAlpha, 0.f, 1.f, "%.2f");
                MenuCheckbox("Flat alpha##rvmflat", &g_vmFlatAlpha);
            }
        }

        MenuSectionLabel("CHAMS");
        MenuChamLayers("Weapon chams##rvmcw", g_enableVmWeaponChams, g_vmWeaponChamCount, g_vmWeaponChamLayers, true);
        MenuChamLayers("Arms chams##rvmca",   g_enableVmArmsChams,   g_vmArmsChamCount,   g_vmArmsChamLayers,   true);

        MenuSectionLabel("GLOW");
        auto GlowSection = [](const char* label, bool& enable, float* col, float& sigma, int& stencil, bool& useSheen) {
            ImGui::PushID(label);
            MenuCheckbox(label, &enable);
            if (enable) {
                const float sw = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;
                MenuCheckbox("Use sheen color##sheen", &useSheen,
                             useSheen ? 0.f : ImGui::GetContentRegionAvail().x - sw);
                if (!useSheen) { ImGui::SameLine(); ColorPickerPopup(col, 4, "##col"); }
                MenuSliderF("Blur##sigma", &sigma, 0.f, 30.f, "%.0f px");
                MenuSliderI("Outline##stencil", &stencil, 0, 10, stencil == 0 ? "off" : "%d px");
            }
            ImGui::PopID();
        };
        GlowSection("Weapon glow", g_enableVmWeaponGlow, g_vmWeaponGlowColor, g_vmWeaponGlowSigma,
                    g_vmWeaponGlowStencil, g_vmWeaponGlowUseSheen);
        GlowSection("Arms glow",   g_enableVmArmsGlow,   g_vmArmsGlowColor,   g_vmArmsGlowSigma,
                    g_vmArmsGlowStencil,   g_vmArmsGlowUseSheen);
    }
    // Tab 4: Crosshair — dynamic spread + live preview
    if (s_tab == 4) {
        static bool   s_chTestRunning  = false;
        static float  s_chTestSpeed    = 0.f;
        static double s_chTestT0       = 0.0;
        static bool   s_chPreviewDynBg = false;

        MenuSectionLabel("DYNAMIC SPREAD");
        MenuCheckbox("Enable##rchdyn", &g_chDynamic);
        if (g_chDynamic) {
            MenuSliderF("Min speed##rchdynmin",  &g_chDynamicMin, 0.f, 900.f,  "%.0f u/s");
            MenuSliderF("Max spread##rchdynmax", &g_chDynamicMax, 0.f, 20.f,   "%.1f");
            MenuSliderF("Full at##rchdynref",    &g_chDynamicRef, 1.f, 1000.f, "%.0f u/s");
            if (g_chDynamicMin >= g_chDynamicRef) g_chDynamicMin = std::max(0.f, g_chDynamicRef - 1.f);

            if (ImGui::Button(s_chTestRunning ? "Stop test##rchtestbtn" : "Test##rchtestbtn", ImVec2(-1.f, 0.f))) {
                s_chTestRunning = !s_chTestRunning;
                s_chTestSpeed   = 0.f;
                s_chTestT0      = ImGui::GetTime();
            }
        } else {
            s_chTestRunning = false;
        }

        // test loop: 0 -> "full at" speed over 3 s, then restart
        if (s_chTestRunning) {
            const float elapsed = (float)(ImGui::GetTime() - s_chTestT0);
            s_chTestSpeed = elapsed / 3.f * g_chDynamicRef;
            if (s_chTestSpeed >= g_chDynamicRef) { s_chTestSpeed = 0.f; s_chTestT0 = ImGui::GetTime(); }
        } else {
            s_chTestSpeed = 0.f;
        }

        MenuSectionLabel("PREVIEW");
        MenuCheckbox("Moving background##rchpreviewbg", &s_chPreviewDynBg);

        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const float  side  = std::min(avail.x, avail.y - ImGui::GetStyle().ItemSpacing.y);
        if (side > 4.f)
        {
            ImDrawList*  pdl = ImGui::GetWindowDrawList();
            const ImVec2 p0  = ImGui::GetCursorScreenPos();
            const ImVec2 p1(p0.x + side, p0.y + side);
            pdl->AddRectFilled(p0, p1, IM_COL32(20, 20, 20, 220), 4.f);

            if (s_chPreviewDynBg)
                DrawCrosshairPreviewScene(pdl, p0, side);

            // the real renderer, so the preview can never drift from what the game shows
            if (g_enableCrosshair || g_chCsgoEnable)
                Crosshair_DrawPreview(pdl, p0, p1, s_chTestRunning ? s_chTestSpeed : -1.f);
            else {
                const char* off = "No crosshair enabled";
                const ImVec2 ts = ImGui::CalcTextSize(off);
                pdl->AddText(ImVec2(p0.x + (side - ts.x) * 0.5f, p0.y + (side - ts.y) * 0.5f),
                             IM_COL32(120, 120, 120, 255), off);
            }

            if (s_chTestRunning) {
                char velBuf[32];
                snprintf(velBuf, sizeof(velBuf), "%.0f u/s", s_chTestSpeed);
                const ImVec2 ts = ImGui::CalcTextSize(velBuf);
                pdl->AddText(ImVec2(p0.x + (side - ts.x) * 0.5f, p1.y - ts.y - 4.f),
                             IM_COL32(200, 200, 200, 210), velBuf);
            }
            ImGui::Dummy(ImVec2(side, side));
        }
    }
    // Tab 5: Misc — game colors, killfeed, speedometer, system
    if (s_tab == 5) {
        // feature toggle + "use sheen color" + swatch, the shape every game recolor here takes
        auto SheenColor = [](const char* label, bool& enable, bool& useSheen, float* color3) {
            ImGui::PushID(label);
            MenuCheckbox(label, &enable);
            if (enable) {
                const float sw = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;
                MenuCheckbox("Use sheen color##sheen", &useSheen,
                             useSheen ? 0.f : ImGui::GetContentRegionAvail().x - sw);
                if (!useSheen) { ImGui::SameLine(); ColorPickerPopup(color3, 3, "##col"); }
            }
            ImGui::PopID();
        };

        MenuSectionLabel("GAME");
        SheenColor("Crit particle color", g_enableCritColor, g_critUseSheenColor, g_critColor);
        SheenColor("DR meter color",      g_enableDRColor,   g_drUseSheenColor,   g_drColor);
        SheenColor("Halloween footsteps", g_hwFootsteps,     g_hwFootUseSheenColor, g_hwFootColor);
        if (g_hwFootsteps)
            MenuCheckbox("Hide when cloaked##rmiscfooths", &g_hwFootHideStealth);
        MenuCheckbox("Force Halloween##rmischl", &g_hwForceHoliday);
        {
            const bool prev = g_enableEquipRegionUnlock;
            MenuCheckbox("Equip region unlock##rmisceq", &g_enableEquipRegionUnlock);
            if (g_enableEquipRegionUnlock && !prev) InstallEquipRegionHook();
        }
        MenuCheckbox("Static Ambassador crosshair##rmiscamby", &g_staticAmbyCrosshair);

        // ---- Killfeed --------------------------------------------------
        MenuSectionLabel("KILLFEED");
        MenuSliderF("Notice duration##rmisckfdur", &g_deathNoticeTime, 1.f, 60.f, "%.1f s");
        MenuCheckbox("Only your kills##rmiscselfkf", &g_selfKillfeedOnly);
        if (g_selfKillfeedOnly)
            MenuCheckbox("Custom killfeed##rmisccustomkf", &g_customKillfeedEnable);
        if (g_selfKillfeedOnly && g_customKillfeedEnable && MenuDropdown("Settings##kfdrop"))
        {
            static const char* const k_origin[] = { "Left", "Right", "Center" };
            MenuSegment("##rkforigin", &g_kfOrigin, k_origin, 3);
            MenuSliderF("Offset X##rkfoffx",     &g_kfOffsetX,  -500.f, 500.f, "%.0f px");
            MenuSliderF("Offset Y##rkfoffy",     &g_kfOffsetY,     0.f, 500.f, "%.0f px");
            MenuSliderI("Max entries##rkfmaxe",  &g_kfMaxEntries,  1,   8);
            MenuSliderF("Row gap##rkfpady",      &g_kfEntryPadY,   0.f, 20.f,  "%.0f px");
            MenuSliderF("Side padding##rkfpadx", &g_kfPadX,        0.f, 40.f,  "%.0f px");
            MenuSliderF("Row height##rkfminh",   &g_kfMinHeight,   0.f, 80.f,  "%.0f px");
            MenuCheckbox("Oldest on top##rkfgrowdn", &g_kfGrowDown);

            MenuSectionLabel("STYLE");
            MenuColorGrid("##rkfcolors", {
                { "Background", g_kfBgColor, 4 }, { "Text", g_kfTextColor, 4 },
                { "Icon tint",  g_kfIconColor, 4 }, { "RED team", g_kfRedColor, 4 },
                { "BLU team",   g_kfBluColor, 4 } });
            MenuSliderF("Rounding##rkfround", &g_kfBgRounding, 0.f, 20.f, "%.1f");
            if (g_kfBgRounding > 0.f) MenuCornerFlags("##rkfcorners", g_kfBgCornerFlags);
            MenuOutlineLayers("##rkfoutline", g_kfOutlineLayers, g_kfOutlineLayerCount);
            MenuBoxShadow("##rkfshadow", g_kfBoxShadow, g_kfBoxShadowOffX, g_kfBoxShadowOffY, g_kfBoxShadowBlur, g_kfBoxShadowColor);
            MenuBlob("##rkfblob", g_kfBlobEnable, nullptr, g_kfBlobColor, g_kfBlobAlpha, g_kfBlobSize, g_kfBlobSpeed);
            MenuPrecip("##rkfprecip", g_kfPrecipEnable, g_kfPrecipMode, g_kfPrecipCount, g_kfPrecipSpeed,
                       g_kfPrecipSize, g_kfPrecipWind, g_kfPrecipColor);
            MenuCheckbox("Kill glow##rkfkillglow", &g_kfKillGlowEnable);
            if (g_kfKillGlowEnable) {
                MenuColorGrid("##rkfkgcol", { { "Glow color", g_kfKillGlowColor, 4 } });
                MenuSliderF("Glow duration##rkfkgdur", &g_kfKillGlowDuration, 0.2f, 5.f,  "%.1f s");
                MenuSliderF("Glow spread##rkfkgspr",   &g_kfKillGlowSpread,   1.f,  60.f, "%.0f px");
            }

            MenuSectionLabel("TEXT");
            if (MenuDropdown("Font##rkffont")) {
                MenuFontPicker("##rkffontpick", g_kfFontIndex, g_kfFontFile, g_kfFontFlags, 4);
                ImGui::Spacing();
            }
            MenuSliderF("Text scale##rkffscale", &g_kfFontScale, 0.3f, 5.f, "%.2f");
            MenuCheckbox("Drop shadow##rkftds", &g_kfTextDropShadow);
            if (g_kfTextDropShadow) {
                MenuSliderF("Shadow X##rkftdsx", &g_kfTextDropShadowOffX, -20.f, 20.f, "%.1f px");
                MenuSliderF("Shadow Y##rkftdsy", &g_kfTextDropShadowOffY, -20.f, 20.f, "%.1f px");
                MenuCheckbox("Soft shadow##rkftdsblur", &g_kfTextDropShadowBlur);
                MenuColorGrid("##rkftdscol", { { "Shadow color", g_kfTextDropShadowColor, 4 } });
            }
            MenuCheckbox("Text outline##rkftout", &g_kfTextOutline);
            if (g_kfTextOutline) {
                MenuSliderF("Outline thickness##rkftoutthk", &g_kfTextOutlineThickness, 0.5f, 4.f, "%.1f px");
                MenuColorGrid("##rkftoutcol", { { "Outline color", g_kfTextOutlineColor, 4 } });
            }

            MenuSectionLabel("ICONS");
            MenuCheckbox("Kill icons##rkfuseicons", &g_kfUseIcons);
            if (g_kfUseIcons) {
                MenuSliderF("Icon scale##rkfiscale", &g_kfIconScale, 0.25f, 4.f, "%.2f");
                MenuCheckbox("Consistent kill icons##rkfconsistent", &g_kfConsistentIcons);
            }
            MenuCheckbox("Plain weapon icons##rkfplainicons", &g_kfPlainWeaponIcons);

            MenuSectionLabel("ANIMATION");
            MenuCheckbox("Slide in##rkfslide", &g_kfSlideIn);
            if (g_kfSlideIn) {
                static const char* const k_slideIn[] = { "Auto", "From left", "From right" };
                MenuSegment("##rkfslidedir", &g_kfSlideDir, k_slideIn, 3);
                MenuSliderF("Slide in speed##rkfslidespeed", &g_kfSlideSpeed, 1.f, 50.f, "%.0f");
            }
            MenuCheckbox("Slide out##rkfslideout", &g_kfSlideOut);
            if (g_kfSlideOut) {
                static const char* const k_slideOut[] = { "Auto", "To left", "To right" };
                MenuSegment("##rkfslideoutdir", &g_kfSlideOutDir, k_slideOut, 3);
                MenuSliderF("Slide out speed##rkfslideoutspeed", &g_kfSlideOutSpeed, 1.f, 50.f, "%.0f");
            }
            MenuCheckbox("Fade in##rkffadein", &g_kfFadeIn);
            if (g_kfFadeIn)
                MenuSliderF("Fade in speed##rkffadespeed", &g_kfFadeSpeed, 0.5f, 20.f, "%.1f");
            MenuCheckbox("Fade out##rkffadeout", &g_kfFadeOut);
            if (g_kfFadeOut)
                MenuSliderF("Fade out speed##rkffadeoutspeed", &g_kfFadeOutSpeed, 0.5f, 20.f, "%.1f");
            MenuCheckbox("Smooth shift##rkfsmoothshift", &g_kfSmoothShift);
            if (g_kfSmoothShift)
                MenuSliderF("Shift speed##rkfsmoothshiftspeed", &g_kfSmoothShiftSpeed, 1.f, 50.f, "%.0f");
            ImGui::Spacing();
        }

        // ---- Speedometer -----------------------------------------------
        MenuSectionLabel("SPEEDOMETER");
        MenuCheckbox("Enable##rmispspeedo", &g_speedoEnable);
        if (g_speedoEnable && MenuDropdown("Settings##rspdrop"))
        {
            MenuSliderF("Scale##rspscale", &g_speedoScale, 0.3f, 5.f, "%.2f");
            MenuSliderF("X##rspposX", &g_speedoPosX, 0.f, 1.f, "%.3f");
            MenuSliderF("Y##rspposY", &g_speedoPosY, 0.f, 1.f, "%.3f");
            if (MenuDropdown("Font##rspfont")) {
                MenuFontPicker("##rspfontpick", g_speedoFontIndex, g_speedoFontFile, g_speedoFontFlags, 6);
                ImGui::Spacing();
            }
            MenuCheckbox("Color by speed##rspcbspeed", &g_speedoColorBySpeed);
            if (g_speedoColorBySpeed) {
                MenuColorGrid("##rspcols", {
                    { "Slow", g_speedoColorLow, 4 }, { "Mid", g_speedoColorMid, 4 }, { "Fast", g_speedoColorHigh, 4 } }, 3);
                MenuSliderF("Slow below##rspcsl", &g_speedoSpeedLow,  0.f, 800.f, "%.0f u/s");
                MenuSliderF("Fast above##rspcfh", &g_speedoSpeedHigh, 0.f, 800.f, "%.0f u/s");
            } else {
                MenuColorGrid("##rspflat", { { "Text color", g_speedoColorLow, 4 } });
            }
            MenuCheckbox("Drop shadow##rspshen", &g_speedoShadow);
            if (g_speedoShadow) {
                MenuSliderF("Shadow X##rspshadx", &g_speedoShadowOffX, -20.f, 20.f, "%.1f px");
                MenuSliderF("Shadow Y##rspshady", &g_speedoShadowOffY, -20.f, 20.f, "%.1f px");
                MenuCheckbox("Soft shadow##rspshadblur", &g_speedoShadowBlur);
                MenuColorGrid("##rspshadcol", { { "Shadow color", g_speedoShadowColor, 4 } });
            }
            MenuCheckbox("Outline##rspouten", &g_speedoOutline);
            if (g_speedoOutline) {
                MenuSliderF("Outline thickness##rspoutthk", &g_speedoOutlineThick, 0.5f, 6.f, "%.1f px");
                MenuColorGrid("##rspoutcol", { { "Outline color", g_speedoOutlineColor, 4 } });
            }
            MenuCheckbox("Fade in and out##rspfade", &g_speedoFadeIn);
            if (g_speedoFadeIn)
                MenuSliderF("Show above##rspfadethresh", &g_speedoFadeThreshold, 0.f, 600.f, "%.0f u/s");
            MenuCheckbox("Pre-jump speed##rsppre", &g_speedoShowPre);
            if (g_speedoShowPre) {
                MenuSliderF("Hold time##rspprehold", &g_speedoPreHoldTime, 0.5f, 10.f, "%.1f s");
                MenuCheckbox("Expand from center##rsppreac", &g_speedoPreAnimCenter);
                MenuCheckbox("Doctrine brackets##rsppredoc", &g_speedoPreDoctrineParens);
            }
            ImGui::Spacing();
        }
        MenuCheckbox("Velocity graph##rspgraph", &g_speedoGraph);
        if (g_speedoGraph && MenuDropdown("Settings##rspgdrop"))
        {
            MenuSliderF("X##rspgposX",       &g_speedoGraphPosX,     0.f,   1.f,    "%.3f");
            MenuSliderF("Y##rspgposY",       &g_speedoGraphPosY,     0.f,   1.f,    "%.3f");
            MenuSliderF("Width##rspgw",      &g_speedoGraphW,        20.f,  1000.f, "%.0f px");
            MenuSliderF("Height##rspgh",     &g_speedoGraphH,        10.f,  400.f,  "%.0f px");
            MenuSliderF("Max speed##rspgms", &g_speedoGraphMaxSpeed, 10.f,  2000.f, "%.0f u/s");
            MenuSliderF("Line height##rspglh", &g_speedoGraphLineH,  0.5f,  10.f,   "%.1f px");
            MenuColorGrid("##rspgcol", { { "Graph color", g_speedoGraphColor, 4 } });
            static const char* const k_fade[] = { "None", "Both ends", "By speed", "End only", "Start only" };
            ImGui::TextDisabled("Fade");
            ImGui::SetNextItemWidth(-1.f);
            ImGui::Combo("##rspgfade", &g_speedoGraphFade, k_fade, IM_ARRAYSIZE(k_fade));
            ImGui::Spacing();
        }

        MenuSectionLabel("SYSTEM");
        MenuCheckbox("sv_pure bypass##rmiscpure",      &g_enablePureBypass);
        MenuCheckbox("sv_cheats bypass##rmisccheats",  &g_enableCheatsbypass);
    }
    // Tab 6: Skins — australium, reskin, stat clock, spells
    if (s_tab == 6) {
        if (g_enableSkins) {
            const WpnEntry& we  = k_wpnList[s_skinWpnSel];
            SkinCfg&        cfg = g_skinCfgs[we.redir];

            // a toggle the selected weapon may not support
            auto Optional = [](const char* label, const char* na, bool supported, bool* v) {
                ImGui::BeginDisabled(!supported);
                MenuCheckbox(supported ? label : na, v);
                ImGui::EndDisabled();
            };

            MenuSectionLabel("EXTRAS");
            Optional("Australium##rsk", "Australium (n/a)##rsk", we.canAustralia,   &cfg.australium);
            Optional("Festivized##rsk", "Festivized (n/a)##rsk", we.canFestivize,   &cfg.festivized);
            Optional("Festive##rsk",    "Festive (n/a)##rsk",    we.festiveIdx > 0, &cfg.festive);

            MenuSectionLabel("RESKIN");
            {
                const ReskinGroup* rg = Skin_FindReskinGroup(we.redir);
                const char* preview = rg ? "(none)" : "(n/a for this weapon)";
                if (rg && cfg.reskinDef > 0)
                    for (int i = 0; i < rg->count; i++)
                        if (rg->variants[i].defindex == cfg.reskinDef && rg->variants[i].style == cfg.reskinStyle)
                            { preview = rg->variants[i].name; break; }
                ImGui::BeginDisabled(!rg);
                ImGui::SetNextItemWidth(-1.f);
                if (ImGui::BeginCombo("##rsk_reskin", preview)) {
                    if (ImGui::Selectable("(none)", cfg.reskinDef == 0)) { cfg.reskinDef = 0; cfg.reskinStyle = 0; g_skinNeedsUpdate = true; }
                    for (int i = 0; rg && i < rg->count; i++) {
                        const bool sel = cfg.reskinDef == rg->variants[i].defindex && cfg.reskinStyle == rg->variants[i].style;
                        if (ImGui::Selectable(rg->variants[i].name, sel)) {
                            cfg.reskinDef   = rg->variants[i].defindex;
                            cfg.reskinStyle = rg->variants[i].style;
                            g_skinNeedsUpdate = true;
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::EndDisabled();
            }

            MenuSectionLabel("STAT CLOCK");
            MenuCheckbox("Enable##rskstat", &cfg.statClock);
            if (cfg.statClock) {
                ImGui::TextDisabled("Kill count");
                ImGui::SetNextItemWidth(-1.f);
                ImGui::InputInt("##rskstatk", &cfg.statKills, 1, 100);
                if (cfg.statKills < 0) cfg.statKills = 0;
            }

            MenuSectionLabel("HALLOWEEN SPELLS");
            {
                auto Spell = [&](const char* label, const char* na, bool supported, uint8_t flag) {
                    bool on = (cfg.spellFlags & flag) != 0;
                    ImGui::BeginDisabled(!supported);
                    if (MenuCheckbox(supported ? label : na, &on))
                        cfg.spellFlags = (uint8_t)((cfg.spellFlags & ~flag) | (on ? flag : 0));
                    ImGui::EndDisabled();
                };
                Spell("Exorcism##rskspell",       "Exorcism##rskspell",               true,                               kCfgSpell_Exorcism);
                Spell("Pumpkin bombs##rskspellp", "Pumpkin bombs (n/a)##rskspellp",   (we.spells & kSpell_Pumpkin) != 0, kCfgSpell_Pumpkin);
                Spell("Halloween fire##rskspellf","Halloween fire (n/a)##rskspellf",  (we.spells & kSpell_Fire)    != 0, kCfgSpell_Fire);
            }
            ImGui::Spacing();
            if (ImGui::Button("Apply##rskapp", ImVec2(-1.f, 0.f))) g_skinNeedsUpdate = true;
        }
    }
    // Tab 7: Images — decorative image fills the right column
    if (s_tab == 7) {
        if (g_decorImageTex) {
            // TF2's DX9 wrapper forces SRGBTEXTURE=1 on every SetTexture, which gamma-decodes
            // the texture and makes it look brighter. Off for this draw, then reset.
            ImGui::GetWindowDrawList()->AddCallback(
                [](const ImDrawList*, const ImDrawCmd*) { g_d3dDev->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, 0); }, nullptr);
            const ImVec2 p0 = ImGui::GetCursorScreenPos();
            const ImVec2 sz = ImGui::GetContentRegionAvail();
            ImGui::Dummy(sz);
            ImGui::GetWindowDrawList()->AddImageRounded((ImTextureID)g_decorImageTex,
                p0, ImVec2(p0.x + sz.x, p0.y + sz.y), ImVec2(0, 0), ImVec2(1, 1),
                IM_COL32_WHITE, 6.f);
            ImGui::GetWindowDrawList()->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
        }
    }
    // Tab 8: Config — appearance
    if (s_tab == 8) {
        MenuSectionLabel("APPEARANCE");
        MenuColorGrid("##rcfcolors", {
            { "Accent", g_accentColor, 3 }, { "Text", g_menuTextColor, 3 }, { "Background", g_menuBgColor, 3 } });
        MenuCheckbox("Tab icons##rcftabicons",     &g_menuShowTabIcons);
        MenuCheckbox("Snow##rcfsnow",              &g_menuSnow);
        MenuCheckbox("Background blur##rcfbgblur", &g_menuBgBlur);
        if (g_menuBgBlur) MenuSliderF("Blur strength##rcfblursigma", &g_menuBgBlurSigma, 0.5f, 40.f, "%.1f");

        // credit, pinned to the bottom-right of this column
        const char*  credit = "made by dumufa";
        const ImVec2 ts     = ImGui::CalcTextSize(credit);
        const ImVec2 pad    = ImGui::GetStyle().WindowPadding;
        const float  bY     = ImGui::GetWindowHeight() - pad.y - ts.y;
        if (bY > ImGui::GetCursorPosY()) ImGui::SetCursorPosY(bY);
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - pad.x - ts.x);
        ImGui::TextDisabled("%s", credit);
    }

    ImGui::EndChild();       // ##right
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    ImGui::End();

    if (g_menuFont && g_menuFont->IsLoaded()) ImGui::PopFont();
}

