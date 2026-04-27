#include "Session.h"

#include "engine/AudioEngine.h"
#include "engine/AudioGraphRuntime.h"
#include "engine/PluginHost.h"
#include "PluginSlot.h"

#include <utility>

namespace pedalboard3::session
{

namespace
{

constexpr double kFallbackSampleRate = 48000.0;
constexpr int kFallbackBlockSize = 512;

struct DeviceConfig
{
    double sampleRate;
    int blockSize;
};

DeviceConfig deriveConfig(engine::AudioEngine& eng)
{
    if (auto* dm = eng.getDeviceManager())
    {
        if (auto* dev = dm->getCurrentAudioDevice())
        {
            const auto sr = dev->getCurrentSampleRate();
            const auto bs = dev->getCurrentBufferSizeSamples();
            return {sr > 0.0 ? sr : kFallbackSampleRate, bs > 0 ? bs : kFallbackBlockSize};
        }
    }
    return {kFallbackSampleRate, kFallbackBlockSize};
}

} // namespace

// -------- Worker thread ----------------------------------------------------

class Session::Worker : public juce::Thread
{
public:
    explicit Worker(Session& session) : juce::Thread("PB3 Patch Build"), session_(session) {}

    void run() override
    {
        while (!threadShouldExit())
        {
            // Sleep until notified, or until shutdown asks us to exit.
            // juce::Thread::wait(-1) returns when notify() or
            // signalThreadShouldExit() is called.
            wait(-1);
            if (threadShouldExit())
                return;

            // Snapshot the mailbox under lock and release it BEFORE the
            // slow build call. The mailbox lock must NEVER be held across
            // buildFn_ — that's the 1–4 second call that would otherwise
            // freeze the UI thread on its next queueLoad.
            std::optional<Request> req;
            {
                std::lock_guard<std::mutex> lk(session_.mailboxMutex_);
                req = std::exchange(session_.mailbox_, std::nullopt);
            }
            if (!req)
                continue;

            BuildResult br;
            if (req->kind == RequestKind::Unload)
            {
                br.isUnload = true;
            }
            else
            {
                const auto cfg = deriveConfig(session_.engine_);
                br.desc = req->desc;
                br.prepared = session_.buildFn_(req->desc, cfg.sampleRate, cfg.blockSize, br.errorOut);
            }

            {
                std::lock_guard<std::mutex> lk(session_.resultMutex_);
                session_.result_ = std::move(br);
            }

            // Drain only one request per signal: the message thread will
            // re-arm us if mailbox grew during the build.
            if (session_.resultUpdater_ != nullptr)
                session_.resultUpdater_->triggerAsyncUpdate();
        }
    }

private:
    Session& session_;
};

// -------- Result updater ---------------------------------------------------

class Session::ResultUpdater : public juce::AsyncUpdater
{
public:
    explicit ResultUpdater(Session& session) : session_(session) {}

    void handleAsyncUpdate() override { session_.onBuildResult(); }

private:
    Session& session_;
};

// -------- Session ----------------------------------------------------------

Session::Session(PluginSlot& slot, engine::AudioEngine& engine, engine::PluginHost& host)
    : Session(slot,
              engine,
              [&host](const juce::PluginDescription& d, double sr, int bs, juce::String& err)
              { return buildPreparedGraph(host, d, sr, bs, err); })
{
}

Session::Session(PluginSlot& slot, engine::AudioEngine& engine, BuildFn buildFn)
    : slot_(slot), engine_(engine), buildFn_(std::move(buildFn))
{
    jassert(buildFn_);
    resultUpdater_ = std::make_unique<ResultUpdater>(*this);
    worker_ = std::make_unique<Worker>(*this);
    worker_->startThread();
    idleEvent_.signal(); // we start Idle
}

Session::~Session()
{
    // Cancel any AsyncUpdater callback that's already been scheduled —
    // it would otherwise fire on a partially-destroyed Session after the
    // worker join (worker join alone cannot drain already-queued message-
    // thread items).
    if (resultUpdater_ != nullptr)
        resultUpdater_->cancelPendingUpdate();

    // Tell the worker to exit, then wake it so it observes the flag.
    if (worker_ != nullptr)
    {
        worker_->signalThreadShouldExit();
        worker_->notify();
        // Bounded by the slowest plugin's createInstance / prepareToPlay.
        worker_->stopThread(5000);
    }

    // Drop any orphaned BuildResult so the prepared graph (if any) is
    // destroyed on this (message) thread alongside us.
    {
        std::lock_guard<std::mutex> lk(resultMutex_);
        result_.reset();
    }
}

void Session::queueLoad(std::optional<juce::PluginDescription> desc)
{
    Request req;
    if (desc)
    {
        req.kind = RequestKind::Load;
        req.desc = *desc;
    }
    else
    {
        req.kind = RequestKind::Unload;
    }

    {
        std::lock_guard<std::mutex> lk(mailboxMutex_);
        mailbox_ = std::move(req);
    }

    bool wasIdle = false;
    {
        std::lock_guard<std::mutex> lk(statusMutex_);
        wasIdle = (state_.load(std::memory_order_relaxed) == State::Idle);
    }

    if (wasIdle)
        publishStateChange(State::Building);

    if (worker_ != nullptr)
        worker_->notify();
}

void Session::queueLoad(const juce::PluginDescription& desc)
{
    queueLoad(std::optional<juce::PluginDescription>{desc});
}

void Session::queueUnload()
{
    queueLoad(std::nullopt);
}

Session::Status Session::getStatus() const
{
    Status s;
    s.state = state_.load(std::memory_order_acquire);
    s.version = stateVersion_.load(std::memory_order_acquire);
    {
        std::lock_guard<std::mutex> lk(statusMutex_);
        s.lastError = lastError_;
        s.activeDescription = activeDescription_;
    }
    s.loadedName = slot_.getLoadedName();
    return s;
}

juce::String Session::getLoadedName() const
{
    return slot_.getLoadedName();
}

juce::String Session::getLastError() const
{
    std::lock_guard<std::mutex> lk(statusMutex_);
    return lastError_;
}

bool Session::isLoaded() const noexcept
{
    return slot_.isLoaded();
}

std::optional<juce::PluginDescription> Session::getActiveDescription() const
{
    std::lock_guard<std::mutex> lk(statusMutex_);
    return activeDescription_;
}

bool Session::waitForIdle(int timeoutMs)
{
    if (state_.load(std::memory_order_acquire) == State::Idle)
        return true;
    return idleEvent_.wait(timeoutMs);
}

void Session::publishStateChange(State newState)
{
    state_.store(newState, std::memory_order_release);
    stateVersion_.fetch_add(1, std::memory_order_release);

    if (newState == State::Idle)
        idleEvent_.signal();
    else
        idleEvent_.reset();

    sendChangeMessage();
}

void Session::rearmWorkerIfMailboxNonEmpty()
{
    bool hasMore = false;
    {
        std::lock_guard<std::mutex> lk(mailboxMutex_);
        hasMore = mailbox_.has_value();
    }
    if (hasMore)
    {
        publishStateChange(State::Building);
        if (worker_ != nullptr)
            worker_->notify();
    }
}

void Session::onBuildResult()
{
    std::optional<BuildResult> result;
    {
        std::lock_guard<std::mutex> lk(resultMutex_);
        result = std::exchange(result_, std::nullopt);
    }

    if (!result)
        return;

    // Coalescing check — if a newer request landed in the mailbox during
    // the build, drop this result and re-arm the worker for the new one.
    {
        std::lock_guard<std::mutex> lk(mailboxMutex_);
        if (mailbox_.has_value())
        {
            // Stay in Building; worker will pick up the new request.
            if (worker_ != nullptr)
                worker_->notify();
            return; // result.prepared is destroyed via RAII as we exit.
        }
    }

    if (result->isUnload)
    {
        slot_.unload();
        {
            std::lock_guard<std::mutex> lk(statusMutex_);
            activeDescription_ = std::nullopt;
            lastError_.clear();
        }
        publishStateChange(State::Idle);
    }
    else if (!result->prepared.isValid())
    {
        // Build failed — leave any previously-loaded plugin intact. Do
        // NOT clear activeDescription_; that would lose the user's
        // restore state when a missing plugin fails to load on launch.
        {
            std::lock_guard<std::mutex> lk(statusMutex_);
            lastError_ = result->errorOut.isNotEmpty() ? result->errorOut : juce::String("Plugin load failed");
        }
        publishStateChange(State::Idle);
    }
    else
    {
        publishStateChange(State::Swapping);

        // SR/block-change recovery: if the audio runtime has changed sample
        // rate or block size since the worker dispatched, re-prepareToPlay
        // on the message thread before swapping. Cheap (only inner
        // prepareToPlay re-runs), and avoids the prepared graph being
        // mismatched with the live runtime.
        if (auto* runtime = engine_.getRuntime(); runtime != nullptr && result->prepared.graph != nullptr)
        {
            const double rtSr = runtime->getSampleRate();
            const int rtBlock = runtime->getBlockSize();
            const double pgSr = result->prepared.graph->getSampleRate();
            const int pgBlock = result->prepared.graph->getBlockSize();
            if (rtSr > 0.0 && rtBlock > 0 && (rtSr != pgSr || rtBlock != pgBlock))
            {
                result->prepared.graph->setPlayConfigDetails(2, 2, rtSr, rtBlock);
                result->prepared.graph->prepareToPlay(rtSr, rtBlock);
            }
        }

        auto previous = engine_.loadGraph(std::move(result->prepared.graph));
        (void)previous;

        slot_.adoptPrepared(std::move(result->prepared));

        {
            std::lock_guard<std::mutex> lk(statusMutex_);
            activeDescription_ = result->desc;
            lastError_.clear();
        }
        publishStateChange(State::Idle);
    }

    // A queueLoad may have arrived between our coalescing check above and
    // the publishStateChange to Idle — re-check and re-arm.
    rearmWorkerIfMailboxNonEmpty();
}

} // namespace pedalboard3::session
