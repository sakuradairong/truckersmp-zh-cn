#pragma once
#include "renderer.h"
#include "telemetry.h"
#include "freight.h"
#include <array>

namespace overlay {
class Runtime {
    TruckersMP::Session& session_;
    Players players_;
    Renderer renderer_;
    std::shared_ptr<Service> service_;
    std::shared_ptr<const ServiceView> data_;
    companion::Settings settings_,draft_;
    companion::Own own_;
    JobTelemetry job_;
    FreightController freight_;
    FreightTarget freightTarget_;
    int freightMode_=0;
    std::array<ImGuiTextFilter,7> freightFilters_{};
    bool stopped_=false,ready_=false,shown_=true,editing_=false,cursor_=false,mouseLock_=false,keyLock_=false;
    bool requestMode_=false,allowRequests_=false,dirty_=false;
    bool rendererWarning_=false;
    bool freightNotice_=false;
    bool rightToggleHeld_=false,suppressKeysUntilRelease_=false;
    int recordingKey_=0;
    std::string recordingError_,settingsMessage_;
    std::array<bool,256> keys_{};
    std::array<bool,3> captured_{};
    std::uint64_t nextRead_=0,nextSave_=0,nextOffer_=0,saveSerial_=0,epoch_=0,rendererGeneration_=0;
    float lastWidth_=0,lastHeight_=0;
    ImVec2 boundsMin_{},boundsMax_{};
    int page_=0,filter_=0,sort_=0,selected_=-1;
    std::uint64_t selectedAccount_=0;
    char search_[192]{},accountText_[24]{};
    std::string message_;
    bool Matches(const companion::Hotkey& key,unsigned pressed) const;
    unsigned Modifiers() const;
    void BeginKeyCapture(int target);
    void EndKeyCapture();
    void CaptureKey(unsigned key,bool edge);
    void Key(TruckersMP::InputKeyEvent& event);
    void Character(TruckersMP::InputCharEvent& event);
    void MouseMove(TruckersMP::InputMouseMoveEvent& event);
    void MouseButton(TruckersMP::InputMouseButtonEvent& event);
    void MouseWheel(TruckersMP::InputMouseWheelEvent& event);
    bool EnterEdit();
    void LeaveEdit();
    void MarkDirty();
    void Frame();
    void Draw();
    void Nearby();
    void Truck();
    void Freight();
    void FreightNotice();
    void FreightEditor();
    void FreightCopy();
    void ProfilePage();
    void SettingsPage();
    void Draft();
    void Selected();
    std::uint64_t Account() const;
    const Profile* ProfileFor(std::uint64_t id) const;
    void Request(std::uint64_t id,bool priority=false);
    bool WorldLive() const;
public:
    explicit Runtime(TruckersMP::Session& session):session_(session),players_(session){}
    ~Runtime(){Shutdown();}
    bool Start();
    void Shutdown() noexcept;
};
}
