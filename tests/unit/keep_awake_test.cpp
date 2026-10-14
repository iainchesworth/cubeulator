#include <catch2/catch_test_macros.hpp>

#include "desktop/keep_awake.hpp"

#include <QObject>

#include <memory>
#include <utility>

using cube::app::KeepAwake;
using cube::app::KeepAwakeBackend;

namespace {

// A backend that records what it is asked and answers as told.
class FakeBackend final : public KeepAwakeBackend {
public:
    void acquire() override {
        ++acquires;
        if (grant) {
            report(true);
        } else {
            report(false);
        }
    }
    void release() override { ++releases; }

    int acquires = 0;
    int releases = 0;
    bool grant = true;
};

// Counts how often it is released, and nothing else.
class CountingBackend final : public KeepAwakeBackend {
public:
    explicit CountingBackend(int* releases) : releases_(releases) {}
    void acquire() override { report(true); }
    void release() override { ++*releases_; }

private:
    int* releases_;
};

struct Fixture {
    explicit Fixture(bool grant = true) {
        auto backend = std::make_unique<FakeBackend>();
        backend->grant = grant;
        fake = backend.get();
        keepAwake = std::make_unique<KeepAwake>(std::move(backend));
        QObject::connect(keepAwake.get(), &KeepAwake::activeChanged, [this] { ++activeSignals; });
        QObject::connect(keepAwake.get(), &KeepAwake::heldChanged, [this] { ++heldSignals; });
    }

    FakeBackend* fake = nullptr;
    std::unique_ptr<KeepAwake> keepAwake;
    int activeSignals = 0;
    int heldSignals = 0;
};

}  // namespace

TEST_CASE("KeepAwake starts off and asks the platform for nothing", "[desktop][keepawake]") {
    Fixture f;

    REQUIRE_FALSE(f.keepAwake->active());
    REQUIRE_FALSE(f.keepAwake->held());
    REQUIRE(f.fake->acquires == 0);
    REQUIRE(f.fake->releases == 0);
}

TEST_CASE("KeepAwake holds the display while active and lets go when not", "[desktop][keepawake]") {
    Fixture f;

    f.keepAwake->setActive(true);
    REQUIRE(f.keepAwake->active());
    REQUIRE(f.keepAwake->held());
    REQUIRE(f.fake->acquires == 1);
    REQUIRE(f.activeSignals == 1);
    REQUIRE(f.heldSignals == 1);

    f.keepAwake->setActive(false);
    REQUIRE_FALSE(f.keepAwake->active());
    REQUIRE_FALSE(f.keepAwake->held());
    REQUIRE(f.fake->releases == 1);
    REQUIRE(f.activeSignals == 2);
    REQUIRE(f.heldSignals == 2);
}

TEST_CASE("KeepAwake ignores a repeated request", "[desktop][keepawake]") {
    Fixture f;

    f.keepAwake->setActive(true);
    f.keepAwake->setActive(true);
    REQUIRE(f.fake->acquires == 1);
    REQUIRE(f.activeSignals == 1);

    f.keepAwake->setActive(false);
    f.keepAwake->setActive(false);
    REQUIRE(f.fake->releases == 1);
    REQUIRE(f.activeSignals == 2);
}

TEST_CASE("KeepAwake reports that it is not holding when the platform refuses",
          "[desktop][keepawake]") {
    Fixture f(/*grant=*/false);

    f.keepAwake->setActive(true);

    // The screen asked, so it is active; the system said no, so it is not held.
    REQUIRE(f.keepAwake->active());
    REQUIRE_FALSE(f.keepAwake->held());
    REQUIRE(f.heldSignals == 0);
}

TEST_CASE("KeepAwake hands the display back when it is destroyed while active",
          "[desktop][keepawake]") {
    int released = 0;
    {
        KeepAwake keepAwake(std::make_unique<CountingBackend>(&released));
        keepAwake.setActive(true);
        REQUIRE(released == 0);
    }
    REQUIRE(released == 1);
}

TEST_CASE("KeepAwake on this platform can be switched on and off",
          "[desktop][keepawake][platform]") {
    // The real backend, briefly. It must come back off and never hang. Whether
    // the request is granted depends on the platform (Linux needs a session
    // bus, which a CI container does not have), so only what holds on every
    // platform is checked here; tests/platform/<name>/desktop/ holds the
    // platforms that always grant it.
    KeepAwake keepAwake;
    REQUIRE_FALSE(keepAwake.held());

    keepAwake.setActive(true);
    REQUIRE(keepAwake.active());

    keepAwake.setActive(false);
    REQUIRE_FALSE(keepAwake.active());
    REQUIRE_FALSE(keepAwake.held());
}
