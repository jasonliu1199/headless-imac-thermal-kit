#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOTimerEventSource.h>
#include <i386/proc_reg.h>
#include <i386/cpuid.h>
#include <mach/kmod.h>
#include <mach/mach_types.h>
#include <pexpert/pexpert.h>

#define super IOService

#ifndef MSR_IA32_POWER_CTL
#define MSR_IA32_POWER_CTL 0x1FC
#endif
#ifndef MSR_IA32_MISC_ENABLE
#define MSR_IA32_MISC_ENABLE 0x1A0
#endif
#ifndef MSR_IA32_PERF_CTL
#define MSR_IA32_PERF_CTL 0x199
#endif
#ifndef MSR_TURBO_RATIO_LIMIT
#define MSR_TURBO_RATIO_LIMIT 0x1AD
#endif
#ifndef MSR_IA32_PM_ENABLE
#define MSR_IA32_PM_ENABLE 0x770
#endif
#ifndef MSR_IA32_HWP_CAPABILITIES
#define MSR_IA32_HWP_CAPABILITIES 0x771
#endif
#ifndef MSR_IA32_HWP_REQUEST
#define MSR_IA32_HWP_REQUEST 0x774
#endif

static const uint64_t kPowerCtlEnableProcHot = 1ULL;
static const uint64_t kMiscEnableSpeedStep = 1ULL << 16;
static const uint64_t kMiscDisableTurboBoost = 1ULL << 38;

extern "C" void mp_rendezvous_no_intrs(void (*func)(void *), void *arg);

struct UnlockConfig {
    uint32_t targetRatio;
    uint32_t minRatio;
    uint32_t epp;
    uint32_t cpuCount;
    uint32_t hwpWrites;
    uint32_t perfWrites;
    uint32_t prochotClears;
    uint64_t samplePowerCtl;
    uint64_t sampleMisc;
    uint64_t sampleHwpCaps;
    uint64_t sampleHwpRequest;
    uint64_t samplePerfCtl;
    uint64_t sampleTurboLimit;
};

static bool cpu_has_hwp()
{
    uint32_t regs[4] = {[eax] = 6, [ebx] = 0, [ecx] = 0, [edx] = 0};
    cpuid(regs);
    return regs[eax] & (1U << 7);
}

static uint32_t first_nonzero_turbo_ratio(uint64_t turbo)
{
    for (int shift = 24; shift >= 0; shift -= 8) {
        uint32_t ratio = (uint32_t)((turbo >> shift) & 0xff);
        if (ratio) return ratio;
    }
    return 42;
}

static void apply_unlock_on_cpu(void *data)
{
    auto *cfg = static_cast<UnlockConfig *>(data);
    uint32_t target = cfg->targetRatio;
    uint32_t minRatio = cfg->minRatio;

    uint64_t power = rdmsr64(MSR_IA32_POWER_CTL);
    uint64_t newPower = power & ~kPowerCtlEnableProcHot;
    if (newPower != power) {
        wrmsr64(MSR_IA32_POWER_CTL, newPower);
        cfg->prochotClears++;
    }

    uint64_t misc = rdmsr64(MSR_IA32_MISC_ENABLE);
    uint64_t newMisc = (misc | kMiscEnableSpeedStep) & ~kMiscDisableTurboBoost;
    if (newMisc != misc) {
        wrmsr64(MSR_IA32_MISC_ENABLE, newMisc);
    }

    uint64_t turbo = rdmsr64(MSR_TURBO_RATIO_LIMIT);
    if (target == 0) {
        target = first_nonzero_turbo_ratio(turbo);
    }
    if (target < 12) target = 42;
    if (target > 45) target = 45;
    if (minRatio < 8) minRatio = 8;
    if (minRatio > target) minRatio = target;

    if (cpu_has_hwp()) {
        uint64_t pmEnable = rdmsr64(MSR_IA32_PM_ENABLE);
        if ((pmEnable & 1ULL) == 0) {
            wrmsr64(MSR_IA32_PM_ENABLE, pmEnable | 1ULL);
        }

        uint64_t caps = rdmsr64(MSR_IA32_HWP_CAPABILITIES);
        uint32_t highest = (uint32_t)(caps & 0xff);
        uint32_t lowest = (uint32_t)((caps >> 24) & 0xff);
        if (highest && target > highest) target = highest;
        if (lowest && minRatio < lowest) minRatio = lowest;

        uint64_t oldReq = rdmsr64(MSR_IA32_HWP_REQUEST);
        uint64_t req =
            ((uint64_t)minRatio) |
            ((uint64_t)target << 8) |
            ((uint64_t)target << 16) |
            ((uint64_t)(cfg->epp & 0xff) << 24);
        uint64_t newReq = (oldReq & ~0xffffffffULL) | req;
        wrmsr64(MSR_IA32_HWP_REQUEST, newReq);
        cfg->hwpWrites++;

        cfg->sampleHwpCaps = caps;
        cfg->sampleHwpRequest = rdmsr64(MSR_IA32_HWP_REQUEST);
    }

    uint64_t perf = rdmsr64(MSR_IA32_PERF_CTL);
    uint64_t newPerf = (perf & ~0xff00ULL) | ((uint64_t)target << 8);
    wrmsr64(MSR_IA32_PERF_CTL, newPerf);
    cfg->perfWrites++;

    cfg->samplePowerCtl = rdmsr64(MSR_IA32_POWER_CTL);
    cfg->sampleMisc = rdmsr64(MSR_IA32_MISC_ENABLE);
    cfg->samplePerfCtl = rdmsr64(MSR_IA32_PERF_CTL);
    cfg->sampleTurboLimit = turbo;
    cfg->cpuCount++;
}

static void apply_unlock(UnlockConfig *cfg)
{
    mp_rendezvous_no_intrs(apply_unlock_on_cpu, cfg);
}

class FrequencyUnlocker : public IOService
{
    OSDeclareDefaultStructors(FrequencyUnlocker)

public:
    bool init(OSDictionary *dictionary = nullptr) override;
    void free(void) override;
    IOService *probe(IOService *provider, SInt32 *score) override;
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;

private:
    IOWorkLoop *workLoop_ = nullptr;
    IOTimerEventSource *timer_ = nullptr;
    uint32_t ratio_ = 42;
    uint32_t minRatio_ = 8;
    uint32_t epp_ = 0;
    uint32_t intervalMs_ = 2000;
    uint32_t tick_ = 0;

    static void timerFired(OSObject *owner, IOTimerEventSource *sender);
    void applyAndReschedule(bool first);
};

OSDefineMetaClassAndStructors(FrequencyUnlocker, IOService)

#ifdef XCODE_OFF
extern "C" {
static kern_return_t dummy(__unused kmod_info_t *ki, __unused void *data)
{
    return KERN_SUCCESS;
}

#ifndef KEXT_ID
#error KEXT_ID undefined
#endif
#ifndef KEXT_VERSION
#error KEXT_VERSION undefined
#endif

#define TO_STR(tokens) #tokens
extern kern_return_t _start(kmod_info_t *, void *);
extern kern_return_t _stop(kmod_info_t *, void *);
KMOD_EXPLICIT_DECL(KEXT_ID, TO_STR(KEXT_VERSION), _start, _stop)
__private_extern__ kmod_start_func_t *_realmain = dummy;
__private_extern__ kmod_stop_func_t *_antimain = dummy;
__private_extern__ int _kext_apple_cc = __APPLE_CC__;
}
#endif

bool FrequencyUnlocker::init(OSDictionary *dictionary)
{
    IOLog("[FrequencyUnlocker] init\n");
    return super::init(dictionary);
}

void FrequencyUnlocker::free(void)
{
    IOLog("[FrequencyUnlocker] free\n");
    super::free();
}

IOService *FrequencyUnlocker::probe(IOService *provider, SInt32 *score)
{
    IOLog("[FrequencyUnlocker] probe\n");
    return super::probe(provider, score);
}

bool FrequencyUnlocker::start(IOService *provider)
{
    if (!super::start(provider)) return false;

    PE_parse_boot_argn("FrequencyUnlockerRatio", &ratio_, sizeof(ratio_));
    PE_parse_boot_argn("FrequencyUnlockerMinRatio", &minRatio_, sizeof(minRatio_));
    PE_parse_boot_argn("FrequencyUnlockerEPP", &epp_, sizeof(epp_));
    PE_parse_boot_argn("FrequencyUnlockerIntervalMs", &intervalMs_, sizeof(intervalMs_));

    if (ratio_ == 0 || ratio_ > 45) ratio_ = 42;
    if (minRatio_ < 8) minRatio_ = 8;
    if (minRatio_ > ratio_) minRatio_ = ratio_;
    if (intervalMs_ < 250) intervalMs_ = 250;
    if (intervalMs_ > 60000) intervalMs_ = 60000;

    IOLog("[FrequencyUnlocker] start ratio=%u min=%u epp=%u intervalMs=%u\n",
          ratio_, minRatio_, epp_, intervalMs_);

    workLoop_ = IOWorkLoop::workLoop();
    if (!workLoop_) return false;

    timer_ = IOTimerEventSource::timerEventSource(this, FrequencyUnlocker::timerFired);
    if (!timer_) return false;

    if (workLoop_->addEventSource(timer_) != kIOReturnSuccess) return false;

    applyAndReschedule(true);
    registerService();
    return true;
}

void FrequencyUnlocker::stop(IOService *provider)
{
    IOLog("[FrequencyUnlocker] stop\n");
    if (timer_) {
        timer_->cancelTimeout();
        if (workLoop_) workLoop_->removeEventSource(timer_);
        timer_->release();
        timer_ = nullptr;
    }
    if (workLoop_) {
        workLoop_->release();
        workLoop_ = nullptr;
    }
    super::stop(provider);
}

void FrequencyUnlocker::timerFired(OSObject *owner, IOTimerEventSource *sender)
{
    auto *me = OSDynamicCast(FrequencyUnlocker, owner);
    if (me) me->applyAndReschedule(false);
}

void FrequencyUnlocker::applyAndReschedule(bool first)
{
    UnlockConfig cfg = {};
    cfg.targetRatio = ratio_;
    cfg.minRatio = minRatio_;
    cfg.epp = epp_;
    apply_unlock(&cfg);

    tick_++;
    if (first || tick_ <= 10 || (tick_ % 30) == 0) {
        IOLog("[FrequencyUnlocker] tick=%u cpus=%u target=%u hwpWrites=%u perfWrites=%u prochotClears=%u "
              "POWER_CTL=0x%llx MISC=0x%llx HWP_CAP=0x%llx HWP_REQ=0x%llx PERF_CTL=0x%llx TURBO=0x%llx\n",
              tick_, cfg.cpuCount, ratio_, cfg.hwpWrites, cfg.perfWrites, cfg.prochotClears,
              cfg.samplePowerCtl, cfg.sampleMisc, cfg.sampleHwpCaps, cfg.sampleHwpRequest,
              cfg.samplePerfCtl, cfg.sampleTurboLimit);
    }

    if (timer_) timer_->setTimeoutMS(intervalMs_);
}
