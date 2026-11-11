// Picks the right protocol for each device kind at run time.
#include "battery_protocols.h"

#include <cstddef>

namespace {

// Tries every protocol of one kind; once a receiver answers, polls only that
// one until it disappears, so protocols without a receiver cost nothing extra.
class KindProvider : public BatteryProvider {
public:
    explicit KindProvider(DeviceKind kind) {
        for (const auto& p : Protocols()) {
            if (p.kind == kind) entries_.push_back({p.id, p.create()});
        }
    }

    BatteryReading Poll() override {
        size_t tried = kNone;
        if (active_ != kNone) {
            BatteryReading r = PollAt(active_);
            if (r.state != LinkState::NotFound) return r;
            tried = active_;
            active_ = kNone;
        }
        for (size_t i = 0; i < entries_.size(); ++i) {
            if (i == tried) continue;
            BatteryReading r = PollAt(i);
            if (r.state != LinkState::NotFound) {
                active_ = i;
                return r;
            }
        }
        return BatteryReading{};
    }

private:
    static constexpr size_t kNone = static_cast<size_t>(-1);

    struct Entry {
        const char* id;
        std::unique_ptr<BatteryProvider> provider;
    };

    BatteryReading PollAt(size_t i) {
        BatteryReading r = entries_[i].provider->Poll();
        r.protocol = entries_[i].id;
        return r;
    }

    std::vector<Entry> entries_;
    size_t active_ = kNone;
};

}  // namespace

std::unique_ptr<BatteryProvider> CreateProviderFor(DeviceKind kind) { return std::make_unique<KindProvider>(kind); }
