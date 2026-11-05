#pragma once
#include <Windows.h>
#include <cstdint>
#include <string>
#include <string_view>
#include "freight_editor.h"

namespace overlay {
// Last reached call boundary, not a server acknowledgement or a UI receipt.
// Literal strings and integers keep this usable across the SEH boundary.
struct NativeCallTrace {const char* stage="not-called";unsigned exception=0;};
// An implementation detail of the optional local build, not a TMP SDK API.
// No native object survives a Submit call and no client code is patched.
class NativeFreight {
    HMODULE module_=nullptr;
    std::uintptr_t begin_=0,end_=0,playersGlobal_=0,managerGlobal_=0,uiGlobal_=0;
    std::uintptr_t findPlayer_=0,copyRequest_=0,copyEvent_=0,managerTable_=0;
    std::uintptr_t onlineGlobal_=0,generate_=0,resetEditor_=0,selectAuto_=0,selectCombo_=0,checkSet_=0,menuChildren_=0,textGetter_=0;
    std::uintptr_t refreshOwned_=0,refreshRoute_=0,refreshTake_=0,refreshGenerate_=0;
    std::uintptr_t menuItemType_=0;
    bool editorAvailable_=false,onlineAvailable_=false;
    bool attempted_=false,available_=false,faulted_=false;
    std::string reason_="等待客户端识别";
    std::string diagnostic_,editorReason_,onlineReason_;
    NativeCallTrace trace_;
public:
    enum class Result { CallReturned,Edited,Unavailable,PlayerMissing,StateChanged,Fault };
    const NativeCallTrace& Trace() const {return trace_;}
    // Returns true only when a resolution attempt completes, for one-shot logs.
    bool Prepare(std::string_view clientVersion);
    void SessionChanged();
    bool Available() const {return available_&&!faulted_;}
    const std::string& Reason() const {return reason_;}
    const std::string& Diagnostic() const {return diagnostic_;}
    const std::string& EditorReason() const {return editorReason_;}
    const std::string& OnlineReason() const {return onlineReason_;}
    Result SubmitCopy(int playerId,bool online);
    bool CanCopyOnline() const;
    bool EditorAvailable() const {return Available()&&editorAvailable_;}
    bool ReadEditor(FreightEditorView& view,bool refresh);
    Result Edit(const FreightEditorAction& action,FreightEditorView& after);
    static bool Included();
};
}
