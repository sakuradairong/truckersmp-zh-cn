#pragma once
#include "freight_native.h"
#include "telemetry.h"
#include <optional>

namespace overlay {
enum class CopyPhase {Idle,Queued,AwaitingObservation,NewJob,Confirmed,Unknown,Stopped,Failed,Closed};
struct FreightTarget {
    int player=-1;
    std::uint64_t account=0,epoch=0,server=0;
    std::string name;
};
class FreightController {
    enum class Hold {None,Unresolved,TelemetryReset,NativeFault};
    NativeFreight native_;
    CopyPhase phase_=CopyPhase::Idle;
    FreightTarget target_;
    FreightOperation operation_=FreightOperation::CopyNormal;
    FreightEditorView editor_;
    std::optional<FreightEditorAction> editorAction_;
    bool editorLoaded_=false,expectTaken_=true;
    std::uint64_t nextEditor_=0,editorRevision_=0;
    JobTelemetry baseline_,observed_;
    JobTelemetry latest_;
    NativeCallTrace callTrace_;
    bool noObservationReported_=false,feedbackTelemetryLive_=false;
    int inputReleased_=-1;
    std::uint64_t serial_=0,submitted_=0,cooldown_=0,epoch_=0,server_=0;
    bool pending_=false;
    Hold hold_=Hold::None;
    std::string message_="选择自选做货，或在附近列表中选择玩家复制任务。";
    void Log(TruckersMP::Session& session,const char* state);
    std::string CommonUnavailable(TruckersMP::Session& session,const companion::World& world,const JobTelemetry& job)const;
    void PublishEditor(FreightEditorView view,const companion::World& world,const JobTelemetry& job);
public:
    void Update(TruckersMP::Session& session,const companion::World& world,const JobTelemetry& job,bool showEditor=false);
    std::string Unavailable(TruckersMP::Session& session,const companion::World& world,const JobTelemetry& job,const FreightTarget& target,FreightOperation operation=FreightOperation::CopyNormal) const;
    bool Queue(TruckersMP::Session& session,const companion::World& world,const JobTelemetry& job,const FreightTarget& target,FreightOperation operation=FreightOperation::CopyNormal);
    std::string EditorUnavailable(TruckersMP::Session& session,const companion::World& world,const JobTelemetry& job,bool refresh=false) const;
    bool QueueEditor(TruckersMP::Session& session,const companion::World& world,const JobTelemetry& job,FreightEditorAction action);
    void ProcessEditor(TruckersMP::Session& session,const companion::World& world,const JobTelemetry& job,bool interactionAllowed);
    void Execute(TruckersMP::Session& session,const companion::World& world,const JobTelemetry& job,bool inputReleased);
    void Stop(TruckersMP::Session& session);
    void Confirm(TruckersMP::Session& session,const JobTelemetry& job);
    std::string RecoveryUnavailable(TruckersMP::Session& session,const companion::World& world,const JobTelemetry& job)const;
    bool CloseUnresolved(TruckersMP::Session& session,const companion::World& world,const JobTelemetry& job,std::uint64_t request);
    void SessionChanged(TruckersMP::Session& session);
    bool Queued()const{return phase_==CopyPhase::Queued;}
    bool Pending()const{return pending_;}
    bool CanReviewUnresolved()const{return hold_==Hold::Unresolved;}
    bool CanConfirmJob()const{return phase_==CopyPhase::NewJob&&(pending_||CanReviewUnresolved());}
    CopyPhase Phase()const{return phase_;}
    const std::string& Message()const{return message_;}
    const char* PhaseText() const;
    std::string Diagnostics() const;
    std::uint64_t WaitingSeconds() const {return submitted_?(GetTickCount64()-submitted_)/1000:0;}
    const std::string& Compatibility()const{return native_.Reason();}
    const FreightTarget& Target()const{return target_;}
    std::uint64_t Serial()const{return serial_;}
    const FreightEditorView& Editor()const{return editor_;}
    bool EditorQueued()const{return editorAction_.has_value();}
    bool ManualGenerationConfirmation()const{return operation_==FreightOperation::Generate&&!expectTaken_&&
        ((phase_==CopyPhase::AwaitingObservation&&pending_)||(CanReviewUnresolved()&&(phase_==CopyPhase::Stopped||phase_==CopyPhase::Unknown)));}
    FreightOperation Operation()const{return operation_;}
};
}
