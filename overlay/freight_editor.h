#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace overlay {
enum class FreightOperation {CopyNormal,CopyOnline,Generate};
enum class EditorActionKind {Refresh,Select,Toggle,Generate};
struct FreightChoice {
    std::uintptr_t token=0; // Identity only. Re-enumerated before any native call.
    std::string id,label;
    bool enabled=false;
};
struct FreightMenu {
    bool enabled=false;
    std::uintptr_t selected=0;
    std::vector<FreightChoice> choices;
};
struct FreightToggle {bool enabled=false,checked=false;};
struct FreightEditorView {
    bool ready=false,canGenerate=false,copyTrailer=false;
    std::uintptr_t dialog=0;
    std::uint64_t tick=0,epoch=0,generation=0,revision=0;
    // Source city/company, destination city/company, cargo, trailer, urgency.
    std::array<FreightMenu,7> menus;
    // Compatible cargo, own trailer, country-compatible trailer, take, navigate.
    std::array<FreightToggle,5> toggles;
    std::string message="载入原版选项后，可在这里选择货物。";
};
struct FreightEditorAction {
    EditorActionKind kind=EditorActionKind::Refresh;
    unsigned field=0;
    FreightChoice choice;
    bool checked=false;
    FreightEditorView before;
};
}
