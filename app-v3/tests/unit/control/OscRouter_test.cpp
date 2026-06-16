// Tests for the Phase 4 OscRouter — OSC message -> parameter-update intents.
// Headless: pure juce::OSC data types, no sockets.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "control/OscRouter.h"

#include <juce_osc/juce_osc.h>

using namespace pedalboard3::control;
using Catch::Approx;

namespace
{

OscMapping addrToParam(const juce::String& address, pedalboard3::session::NodeId node, int paramIndex)
{
    OscMapping m;
    m.address = address;
    m.argumentIndex = 0;
    m.target.node = node;
    m.target.parameterIndex = paramIndex;
    return m;
}

} // namespace

TEST_CASE("OscRouter: a matching address drives a parameter", "[control][oscrouter]")
{
    OscRouter r;
    r.setMappings({addrToParam("/pb3/gain", 5, 2)});

    auto out = r.process(juce::OSCMessage("/pb3/gain", 1.0f));
    REQUIRE(out.size() == 1);
    REQUIRE(out[0].target.node == 5);
    REQUIRE(out[0].target.parameterIndex == 2);
    REQUIRE(out[0].value == Approx(1.0f));

    // A non-matching address yields nothing.
    REQUIRE(r.process(juce::OSCMessage("/pb3/other", 1.0f)).empty());
}

TEST_CASE("OscRouter: int argument is promoted; range + invert applied", "[control][oscrouter]")
{
    OscRouter r;
    auto m = addrToParam("/pb3/gain", 1, 0);
    m.rangeMin = 0.2f;
    m.rangeMax = 0.8f;
    r.setMappings({m});

    REQUIRE(r.process(juce::OSCMessage("/pb3/gain", 0.5f))[0].value == Approx(0.5f));
    REQUIRE(r.process(juce::OSCMessage("/pb3/gain", 0.0f))[0].value == Approx(0.2f));
    REQUIRE(r.process(juce::OSCMessage("/pb3/gain", 1))[0].value == Approx(0.8f)); // int 1 -> 1.0

    m.inverted = true;
    r.setMappings({m});
    REQUIRE(r.process(juce::OSCMessage("/pb3/gain", 1.0f))[0].value == Approx(0.2f)); // inverted high
}

TEST_CASE("OscRouter: argumentIndex selects which argument supplies the value", "[control][oscrouter]")
{
    OscRouter r;
    auto m = addrToParam("/pb3/xy", 3, 0);
    m.argumentIndex = 1;
    r.setMappings({m});

    auto out = r.process(juce::OSCMessage("/pb3/xy", 0.1f, 0.9f));
    REQUIRE(out.size() == 1);
    REQUIRE(out[0].value == Approx(0.9f));
}

TEST_CASE("OscRouter: a sender wildcard pattern matches a concrete mapping", "[control][oscrouter]")
{
    OscRouter r;
    r.setMappings({addrToParam("/pb3/gain", 5, 0)});

    // The incoming message's address pattern carries the wildcard.
    auto out = r.process(juce::OSCMessage("/pb3/*", 1.0f));
    REQUIRE(out.size() == 1);
    REQUIRE(out[0].target.node == 5);
}

TEST_CASE("OscRouter: one address fans out to several mappings", "[control][oscrouter]")
{
    OscRouter r;
    r.setMappings({addrToParam("/pb3/gain", 5, 0), addrToParam("/pb3/gain", 6, 1)});

    auto out = r.process(juce::OSCMessage("/pb3/gain", 0.75f));
    REQUIRE(out.size() == 2);
    REQUIRE(out[0].target.node == 5);
    REQUIRE(out[1].target.node == 6);
    REQUIRE(out[0].value == Approx(0.75f));
}

TEST_CASE("OscRouter: an invalid mapping address is skipped, not crashed", "[control][oscrouter]")
{
    OscRouter r;
    r.setMappings({addrToParam("bad address", 5, 0), addrToParam("/pb3/gain", 6, 0)});

    auto out = r.process(juce::OSCMessage("/pb3/gain", 1.0f));
    REQUIRE(out.size() == 1);
    REQUIRE(out[0].target.node == 6);
}

TEST_CASE("OscRouter: a missing argument produces no update", "[control][oscrouter]")
{
    OscRouter r;
    auto m = addrToParam("/pb3/gain", 5, 0);
    m.argumentIndex = 2; // message will only have one arg
    r.setMappings({m});

    REQUIRE(r.process(juce::OSCMessage("/pb3/gain", 1.0f)).empty());
}
