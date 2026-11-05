#include "freight.h"

namespace overlay {
namespace {
constexpr std::uint64_t RequestTimeout=120000,RequestCooldown=15000;
bool Live(const companion::World& w){return w.active&&w.connected&&w.server&&companion::Fresh(GetTickCount64(),w.tick);}
bool Live(const JobTelemetry& j){return j.active&&j.known&&j.generation&&companion::Fresh(GetTickCount64(),j.tick,1500);}
bool SameJob(const JobTelemetry& a,const JobTelemetry& b){
    return a.generation==b.generation&&a.revision==b.revision&&a.hasJob&&b.hasJob&&
        !std::strcmp(a.cargoId,b.cargoId)&&!std::strcmp(a.sourceCityId,b.sourceCityId)&&!std::strcmp(a.sourceCompanyId,b.sourceCompanyId)&&
        !std::strcmp(a.destinationCityId,b.destinationCityId)&&!std::strcmp(a.destinationCompanyId,b.destinationCompanyId)&&!std::strcmp(a.market,b.market);
}
const char* Kind(FreightOperation op){return op==FreightOperation::Generate?"generate":op==FreightOperation::CopyOnline?"wot-copy":"normal-copy";}
}
void FreightController::Log(TruckersMP::Session& session,const char* state){
    session.Core().LogMessage(TruckersMP::LogLevel::Info,"freight/request id="+std::to_string(serial_)+" kind="+Kind(operation_)+" state="+state+
        " player="+std::to_string(target_.player)+" account="+std::to_string(target_.account)+" server="+std::to_string(target_.server)+
        " "+Diagnostics()+" message="+message_);
}
const char* FreightController::PhaseText()const{
    switch(phase_){
        case CopyPhase::Idle:return "尚无请求";
        case CopyPhase::Queued:return "排队，尚未调用";
        case CopyPhase::AwaitingObservation:return "调用已返回，结果待核对";
        case CopyPhase::NewJob:return "观察到任务，待核对来源";
        case CopyPhase::Confirmed:return "用户已确认";
        case CopyPhase::Unknown:return "结果未知";
        case CopyPhase::Stopped:return "已停止跟踪";
        case CopyPhase::Failed:return "调用前检查未通过";
        case CopyPhase::Closed:return "本地记录已结束";
    }
    return "未知状态";
}
std::string FreightController::Diagnostics()const{
    const auto now=GetTickCount64();
    return "native_stage="+std::string(callTrace_.stage)+" exception="+std::to_string(callTrace_.exception)+
        " input_released="+std::to_string(inputReleased_)+" elapsed_ms="+std::to_string(submitted_?now-submitted_:0)+
        " epoch="+std::to_string(target_.epoch)+" baseline_generation="+std::to_string(baseline_.generation)+
        " baseline_revision="+std::to_string(baseline_.revision)+" observed_revision="+std::to_string(observed_.revision)+
        " telemetry_generation="+std::to_string(latest_.generation)+" telemetry_revision="+std::to_string(latest_.revision)+
        " telemetry_active="+std::to_string(latest_.active)+" telemetry_known="+std::to_string(latest_.known)+
        " telemetry_fresh="+std::to_string(Live(latest_))+" has_job="+std::to_string(latest_.hasJob)+
        " paused="+std::to_string(latest_.paused)+" server_ack=unavailable";
}
void FreightController::SessionChanged(TruckersMP::Session& session){
    native_.SessionChanged();epoch_=server_=0;const bool unresolved=hold_!=Hold::None;hold_=Hold::None;cooldown_=0;
    editorAction_.reset();editor_={};editorLoaded_=false;nextEditor_=0;
    if(pending_||unresolved){pending_=false;phase_=CopyPhase::Stopped;message_="会话已变化，旧请求不再跟踪；不会重新提交。";Log(session,"session-changed");}
}
void FreightController::PublishEditor(FreightEditorView view,const companion::World& world,const JobTelemetry& job){
    view.epoch=world.epoch;view.generation=job.generation;view.revision=++editorRevision_;
    editor_=std::move(view);nextEditor_=GetTickCount64()+400;
}
void FreightController::Update(TruckersMP::Session& session,const companion::World& w,const JobTelemetry& job,bool showEditor){
    latest_=job;
    if(epoch_!=w.epoch||server_!=w.server){SessionChanged(session);epoch_=w.epoch;server_=w.server;}
    if(Live(w)&&native_.Prepare(w.clientVersion))session.Core().LogMessage(TruckersMP::LogLevel::Info,"freight/compat "+native_.Diagnostic());
    if(editorLoaded_&&job.active&&editor_.generation!=job.generation){
        editor_={};editorAction_.reset();editorLoaded_=false;editor_.message="游戏会话已重建，请重新载入货运选项。";
    }
    if(showEditor&&editorLoaded_&&!pending_&&hold_==Hold::None&&!editorAction_&&Live(w)&&Live(job)&&!job.paused&&!job.hasJob&&
       job.speedKnown&&job.kmh<=.05f&&job.thread==GetCurrentThreadId()&&GetTickCount64()>=nextEditor_){
        FreightEditorView view;native_.ReadEditor(view,true);PublishEditor(std::move(view),w,job);
    }
    // A stopped/timed-out request may still produce a job. Observe existing
    // telemetry without resending it or treating a lack of data as cancellation.
    if(!pending_&&!CanReviewUnresolved())return;
    if(!Live(w))return;
    if(w.epoch!=target_.epoch||w.server!=target_.server){pending_=false;phase_=CopyPhase::Stopped;hold_=Hold::None;editorAction_.reset();message_="已离开原会话，旧请求不再跟踪。";Log(session,"disconnected");return;}
    if(job.active&&job.generation!=baseline_.generation){pending_=false;phase_=CopyPhase::Stopped;hold_=Hold::TelemetryReset;editorAction_.reset();message_="游戏状态已重建，结果无法关联；请重新连接服务器后再操作货运。";Log(session,"telemetry-reset");return;}
    if(phase_==CopyPhase::Queued)return;
    if(Live(job)&&job.revision>baseline_.revision&&job.configurationTick>=submitted_&&job.hasJob){
        if(phase_!=CopyPhase::NewJob){observed_=job;phase_=CopyPhase::NewJob;message_=pending_?"检测到新任务。请核对货物、路线和市场，再确认结果。":"停止等待后检测到运输任务。请核对来源，不会自动归因于上次请求。";Log(session,"new-job-observed");}
        else if(!SameJob(observed_,job)){observed_=job;message_="当前任务又发生了变化，请重新核对；不会自动归因于本次请求。";Log(session,"observed-job-changed");}
    }else if(phase_==CopyPhase::NewJob&&Live(job)&&!job.hasJob){
        pending_=false;hold_=Hold::Unresolved;phase_=CopyPhase::Stopped;message_="检测到的运输任务已结束或取消，但尚未确认来源。请在下方核对并结束本次记录。";Log(session,"job-ended");
    }
    if(pending_&&phase_==CopyPhase::AwaitingObservation&&GetTickCount64()-submitted_>=15000&&
       (!noObservationReported_||feedbackTelemetryLive_!=Live(job))){
        const bool first=!noObservationReported_;
        noObservationReported_=true;
        feedbackTelemetryLive_=Live(job);
        message_=feedbackTelemetryLive_?"调用已返回，但尚未观察到新任务。没有提示不代表成功或失败；请核对原版窗口，勿重复提交。":"调用已返回，当前任务遥测不可用；无法判断结果，请核对原版窗口，勿重复提交。";
        if(first)Log(session,"no-job-observed-15s"); // Do not log transient read-lock misses every frame.
    }
    if(pending_&&phase_!=CopyPhase::NewJob&&GetTickCount64()-submitted_>=RequestTimeout){
        pending_=false;phase_=CopyPhase::Unknown;hold_=Hold::Unresolved;message_="两分钟内未确认结果，已停止等待。请在原版核对提示和当前任务，再在下方结束记录；不会自动重发。";Log(session,"timeout-unknown");
    }
}
std::string FreightController::CommonUnavailable(TruckersMP::Session& session,const companion::World& w,const JobTelemetry& job)const{
    if(pending_)return "已有货运请求正在跟踪。";
    if(hold_==Hold::Unresolved)return "上一请求结果未明确，请在下方核对并结束记录。";
    if(hold_==Hold::TelemetryReset)return "游戏状态已重建，请重新连接服务器后再操作货运。";
    if(hold_==Hold::NativeFault)return native_.Reason();
    const auto now=GetTickCount64();
    if(now<cooldown_)return "货运操作冷却中，还需 "+std::to_string((cooldown_-now+999)/1000)+" 秒。";
    if(!Live(w)||!session.Network().IsConnected().value_or(false))return "请先连接服务器。";
    if(!native_.Available())return native_.Reason();
    if(!Live(job))return "等待当前任务的完整遥测状态。";
    if(job.thread!=GetCurrentThreadId())return "当前回调不是已观察到的游戏遥测线程，货运操作已停用。";
    if(job.paused)return "请返回驾驶画面后操作。";
    if(!job.speedKnown||job.kmh>.05f)return "请停稳车辆后操作货运。";
    if(job.hasJob)return "已有运输任务，请先完成或在原版中取消。";
    return {};
}
std::string FreightController::Unavailable(TruckersMP::Session& session,const companion::World& w,const JobTelemetry& job,const FreightTarget& t,FreightOperation op)const{
    if(editorAction_)return "等待原版选项同步。";
    auto reason=CommonUnavailable(session,w,job);if(!reason.empty())return reason;
    if(op==FreightOperation::Generate)return "请使用自选做货表单。";
    if(op==FreightOperation::CopyOnline&&!native_.OnlineReason().empty())return native_.OnlineReason();
    if(op==FreightOperation::CopyOnline&&!native_.CanCopyOnline())return "当前原版 WoT 复制条件未就绪，请确认已登录 World of Trucks。";
    if(t.player<0||t.player>=5000||!t.account||t.epoch!=w.epoch||t.server!=w.server)return "请从当前附近列表重新选择支持的玩家。";
    if(t.account==w.account)return "请选择其他玩家。";
    auto selected=session.Player().GetPlayerByID(t.player);
    if(!selected||!selected->IsValid()||selected->GetPlayerID().value_or(-1)!=t.player||selected->GetAccountID().value_or(0)!=t.account)return "所选玩家已离开或编号已被复用，请重新选择。";
    return {};
}
bool FreightController::Queue(TruckersMP::Session& session,const companion::World& w,const JobTelemetry& job,const FreightTarget& t,FreightOperation op){
    const auto reason=Unavailable(session,w,job,t,op);if(!reason.empty()){message_=reason;return false;}
    latest_=job;callTrace_={};inputReleased_=-1;noObservationReported_=false;
    operation_=op;expectTaken_=true;target_=t;baseline_=job;observed_={};submitted_=0;++serial_;phase_=CopyPhase::Queued;pending_=true;message_="正在交还输入并复核目标玩家……";Log(session,"queued");return true;
}
std::string FreightController::EditorUnavailable(TruckersMP::Session& session,const companion::World& w,const JobTelemetry& job,bool refresh)const{
    if(editorAction_)return "等待原版选项同步。";
    auto reason=CommonUnavailable(session,w,job);if(!reason.empty())return reason;
    if(!native_.EditorAvailable())return native_.EditorReason();
    if(!refresh&&(!editor_.ready||editor_.epoch!=w.epoch||editor_.generation!=job.generation||!companion::Fresh(GetTickCount64(),editor_.tick,1500)))return "请重新载入当前会话的货运选项。";
    return {};
}
bool FreightController::QueueEditor(TruckersMP::Session& session,const companion::World& w,const JobTelemetry& job,FreightEditorAction action){
    const auto reason=EditorUnavailable(session,w,job,action.kind==EditorActionKind::Refresh);
    if(!reason.empty()){message_=reason;return false;}
    action.before=editor_;action.before.epoch=w.epoch;action.before.generation=job.generation;
    if(action.kind==EditorActionKind::Generate){
        if(!editor_.canGenerate){message_=editor_.message;return false;}
        latest_=job;callTrace_={};inputReleased_=-1;noObservationReported_=false;
        operation_=FreightOperation::Generate;expectTaken_=editor_.toggles[3].checked;
        target_={-1,0,w.epoch,w.server,"自选货运"};baseline_=job;observed_={};submitted_=0;++serial_;phase_=CopyPhase::Queued;pending_=true;
        message_="正在复核原版选项和接取资格……";Log(session,"queued");
    }
    editorAction_=std::move(action);return true;
}
void FreightController::ProcessEditor(TruckersMP::Session& session,const companion::World& w,const JobTelemetry& job,bool interactionAllowed){
    if(!editorAction_||editorAction_->kind==EditorActionKind::Generate)return;
    auto action=std::move(*editorAction_);editorAction_.reset();
    auto reason=CommonUnavailable(session,w,job);
    if(!interactionAllowed)reason="操作模式已退出，未同步选项。";
    if(action.before.epoch!=w.epoch||action.before.generation!=job.generation)reason="会话已经变化，请重新载入选项。";
    if(!reason.empty()){message_=reason;return;}
    FreightEditorView after;const auto result=native_.Edit(action,after);editorLoaded_=true;
    if(result==NativeFreight::Result::Edited){message_="选项已同步原版并读回。";PublishEditor(std::move(after),w,job);}
    else{
        editor_.ready=false;nextEditor_=0;
        message_=result==NativeFreight::Result::Fault?native_.Reason():"原版选项或资格已变化，未生成任务；请刷新并重新选择。";
        editor_.message=message_;
        const auto& trace=native_.Trace();
        session.Core().LogMessage(TruckersMP::LogLevel::Warning,"freight/editor stage="+std::string(trace.stage)+
            " exception="+std::to_string(trace.exception)+" message="+message_);
    }
}
void FreightController::Execute(TruckersMP::Session& session,const companion::World& w,const JobTelemetry& job,bool released){
    if(!Queued())return;
    latest_=job;inputReleased_=released?1:0;
    pending_=false;auto action=std::move(editorAction_);editorAction_.reset();
    auto reason=operation_==FreightOperation::Generate?CommonUnavailable(session,w,job):Unavailable(session,w,job,target_,operation_);
    if(!released)reason="输入仍被占用，未提交货运请求。";
    if(job.generation!=baseline_.generation||job.revision!=baseline_.revision||w.epoch!=target_.epoch||w.server!=target_.server)reason="提交前会话或任务状态已变化，未发送请求。";
    if(operation_==FreightOperation::Generate&&(!action||action->kind!=EditorActionKind::Generate))reason="生成草稿已失效，请重新确认。";
    if(!reason.empty()){phase_=CopyPhase::Failed;message_=reason;Log(session,"preflight-rejected");return;}
    submitted_=GetTickCount64();cooldown_=submitted_+RequestCooldown;
    callTrace_={"preflight-passed",0};Log(session,"native-call-entering");
    FreightEditorView after;
    const auto result=operation_==FreightOperation::Generate?native_.Edit(*action,after):native_.SubmitCopy(target_.player,operation_==FreightOperation::CopyOnline);
    callTrace_=native_.Trace();
    switch(result){
        case NativeFreight::Result::CallReturned:
            pending_=true;phase_=CopyPhase::AwaitingObservation;
            message_=operation_==FreightOperation::Generate?(expectTaken_?"生成/接取调用已返回，尚未确认结果。若出现原版提示，请处理后核对新任务。":"生成调用已返回，尚未确认货单存在。请在原版查看货单后核对。"):
                "复制调用已返回，尚未确认结果。若出现原版提示，请处理后核对任务；没有提示时请勿重复提交。";
            Log(session,"native-call-returned");break;
        case NativeFreight::Result::PlayerMissing:phase_=CopyPhase::Failed;message_="目标玩家不再可用，未提交请求。";Log(session,"target-missing");break;
        case NativeFreight::Result::StateChanged:phase_=CopyPhase::Failed;message_="原版选项或资格已变化，未提交生成/复制请求。请重新核对。";editor_.ready=false;Log(session,"native-state-changed");break;
        case NativeFreight::Result::Fault:phase_=CopyPhase::Unknown;hold_=Hold::NativeFault;message_=native_.Reason();Log(session,"native-fault-unknown");break;
        default:phase_=CopyPhase::Failed;message_="当前原版货运能力不可用，请检查版本和 WoT 状态。";Log(session,"unavailable");break;
    }
}
void FreightController::Stop(TruckersMP::Session& session){
    editorAction_.reset();if(!pending_)return;
    const bool sent=phase_!=CopyPhase::Queued;pending_=false;hold_=sent?Hold::Unresolved:Hold::None;
    if(phase_!=CopyPhase::NewJob)phase_=CopyPhase::Stopped;
    message_=sent?"已停止等待；原版请求不会撤销。请核对原版提示和当前任务，再确认结果或结束记录。":"已取消尚未提交的请求。";Log(session,"waiting-stopped");
}
void FreightController::Confirm(TruckersMP::Session& session,const JobTelemetry& job){
    latest_=job;
    if(ManualGenerationConfirmation()&&Live(job)&&job.generation==baseline_.generation&&!job.hasJob){
        pending_=false;hold_=Hold::None;phase_=CopyPhase::Confirmed;message_="你已在原版确认货单生成。本记录不表示任务已接取。";Log(session,"user-confirmed-generation");return;
    }
    if(!CanConfirmJob()||!Live(job)||!SameJob(observed_,job)){
        message_="任务状态已变化，请重新核对当前任务。";return;
    }
    if(operation_==FreightOperation::CopyOnline&&std::strcmp(job.market,"external_contracts")&&std::strcmp(job.market,"external_market")){
        message_="当前任务的市场资料尚不能确认是 WoT 任务，不能记作 WoT 复制完成。";return;
    }
    pending_=false;hold_=Hold::None;phase_=CopyPhase::Confirmed;message_="你已确认当前任务与本次操作一致。本次跟踪完成。";Log(session,"user-confirmed");
}
std::string FreightController::RecoveryUnavailable(TruckersMP::Session& session,const companion::World& w,const JobTelemetry& job)const{
    if(!CanReviewUnresolved()||pending_||!submitted_)return "当前没有可以手动结束的待核对记录。";
    if(!Live(w)||!session.Network().IsConnected().value_or(false)||w.epoch!=target_.epoch||w.server!=target_.server)return "请等待当前联机会话恢复。";
    if(!native_.Available())return native_.Reason();
    if(!Live(job)||job.generation!=baseline_.generation)return "等待同一游戏会话的完整任务状态。";
    if(job.thread!=GetCurrentThreadId()||job.paused)return "请回到驾驶画面核对当前任务。";
    if(!job.speedKnown||job.kmh>.05f)return "请停稳车辆后结束记录。";
    if(job.hasJob)return "当前已有运输任务，请先核对是否为刚才的结果；完成或取消任务后再结束记录。";
    const auto now=GetTickCount64(),deadline=submitted_+RequestTimeout;
    if(now<deadline)return "仍在等待可能迟到的响应，至少再等 "+std::to_string((deadline-now+999)/1000)+" 秒。";
    return {};
}
bool FreightController::CloseUnresolved(TruckersMP::Session& session,const companion::World& w,const JobTelemetry& job,std::uint64_t request){
    latest_=job;
    if(request!=serial_){message_="请求记录已变化，请重新核对。";return false;}
    auto reason=RecoveryUnavailable(session,w,job);if(!reason.empty()){message_=reason;return false;}
    // Local acknowledgement only: never cancel a server operation, call a
    // native function or enqueue another request from this recovery action.
    pending_=false;hold_=Hold::None;phase_=CopyPhase::Closed;cooldown_=GetTickCount64()+RequestCooldown;
    editorAction_.reset();editor_={};editorLoaded_=false;nextEditor_=0;
    message_="你已核对并结束本地记录，未确认复制或生成成功。旧请求不会重发；冷却后请重新选择玩家或载入货运选项。";
    Log(session,"user-closed-unresolved");return true;
}
}
