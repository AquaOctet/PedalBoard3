// Tests for the Pedalboard2 (.pdl v2) importer -- the pure XML -> v3 Patch
// transform (master plan §4.2.B.1 + Appendix D). Uses a self-contained inline
// v2 fixture mirroring a real Pedalboard2 file: internal Audio Input/Output
// endpoints, real plugin FILTERs with STATE, an out-of-order FILTER listing
// whose CONNECTIONs define the true signal chain, and a UserNames override.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "session/v2_import/PdlV2Importer.h"

#include <juce_core/juce_core.h>

using namespace pedalboard3;
using Catch::Approx;

namespace
{

// Input(1) -> EQ(8) -> Comp(9) -> Reverb(7) -> Output(3). Note the FILTERs are
// listed Reverb(7), EQ(8), Comp(9) -- deliberately NOT in signal order, so the
// importer must derive order from the connections.
constexpr auto kV2Xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<Pedalboard2PatchFile>
  <Patch tempo="120" name="My Board">
    <FILTERGRAPH>
      <FILTER uid="1" x="0" y="0">
        <PLUGIN name="Audio Input" format="Internal" manufacturer="Raw Material Software"
                uid="246006c0" isInstrument="0" numInputs="0" numOutputs="4"/>
        <STATE>0.</STATE>
      </FILTER>
      <FILTER uid="3" x="900" y="0">
        <PLUGIN name="Audio Output" format="Internal" manufacturer="Raw Material Software"
                uid="724248cb" isInstrument="0" numInputs="2" numOutputs="0"/>
        <STATE>0.</STATE>
      </FILTER>
      <FILTER uid="7" x="600" y="0">
        <PLUGIN name="Reverb" format="VST" category="Reverb" manufacturer="Acme"
                version="1.0" file="C:\VST\Reverb.dll" uid="11112222" isInstrument="0"
                numInputs="2" numOutputs="2"/>
        <STATE>128.abcdef</STATE>
      </FILTER>
      <FILTER uid="8" x="200" y="0">
        <PLUGIN name="EQ" format="VST" category="EQ" manufacturer="Acme" version="2.0"
                file="C:\VST\EQ.dll" uid="33334444" isInstrument="0" numInputs="2" numOutputs="2"/>
      </FILTER>
      <FILTER uid="9" x="400" y="0">
        <PLUGIN name="Comp" format="VST" category="Dynamics" manufacturer="Acme" version="1.5"
                file="C:\VST\Comp.dll" uid="55556666" isInstrument="0" numInputs="2" numOutputs="2"/>
      </FILTER>
      <CONNECTION srcFilter="1" srcChannel="0" dstFilter="8" dstChannel="0"/>
      <CONNECTION srcFilter="1" srcChannel="1" dstFilter="8" dstChannel="1"/>
      <CONNECTION srcFilter="8" srcChannel="0" dstFilter="9" dstChannel="0"/>
      <CONNECTION srcFilter="8" srcChannel="1" dstFilter="9" dstChannel="1"/>
      <CONNECTION srcFilter="9" srcChannel="0" dstFilter="7" dstChannel="0"/>
      <CONNECTION srcFilter="9" srcChannel="1" dstFilter="7" dstChannel="1"/>
      <CONNECTION srcFilter="7" srcChannel="0" dstFilter="3" dstChannel="0"/>
      <CONNECTION srcFilter="7" srcChannel="1" dstFilter="3" dstChannel="1"/>
    </FILTERGRAPH>
    <Mappings/>
    <UserNames>
      <Name id="8" val="My EQ"/>
    </UserNames>
  </Patch>
  <DEVICESETUP deviceType="ASIO" audioOutputDeviceName="Voicemeeter"/>
</Pedalboard2PatchFile>)";

std::unique_ptr<juce::XmlElement> parse(const char* xml)
{
    return juce::parseXML(juce::String(xml));
}

// kV2Xml already emits exactly one warning (the state-not-migrated note: the
// Reverb FILTER carries <STATE>). Mapping-drop warnings add to this baseline.
constexpr int kBaselineWarnings = 1;

// The kV2Xml fixture with its empty <Mappings/> replaced by a populated block.
std::unique_ptr<juce::XmlElement> parseWithMappings(const juce::String& inner)
{
    return juce::parseXML(juce::String(kV2Xml).replace("<Mappings/>", "<Mappings>" + inner + "</Mappings>"));
}

// The kV2Xml fixture with the <Mappings> element removed entirely (pre-mappings file).
std::unique_ptr<juce::XmlElement> parseWithoutMappings()
{
    return juce::parseXML(juce::String(kV2Xml).replace("<Mappings/>", ""));
}

} // namespace

TEST_CASE("v2 import: isPedalboard2Patch detects the root tag", "[session][v2import]")
{
    auto v2 = parse(kV2Xml);
    REQUIRE(v2 != nullptr);
    REQUIRE(session::v2::isPedalboard2Patch(*v2));

    juce::XmlElement v3("PEDALBOARD");
    REQUIRE_FALSE(session::v2::isPedalboard2Patch(v3));
}

TEST_CASE("v2 import: reconstructs the chain in signal order, internal IO -> graphIO", "[session][v2import]")
{
    auto v2 = parse(kV2Xml);
    REQUIRE(v2 != nullptr);

    auto res = session::v2::importPedalboard2Patch(*v2);
    REQUIRE(res.patch != nullptr);
    REQUIRE(res.error.isEmpty());
    REQUIRE(res.patch->name == "My Board");

    // The two Internal I/O filters become endpoints, leaving 3 plugin nodes,
    // ordered by the connection chain (EQ -> Comp -> Reverb), NOT file order.
    REQUIRE(res.patch->nodes.size() == 3);
    REQUIRE(res.patch->nodes[0].desc.name == "EQ");
    REQUIRE(res.patch->nodes[1].desc.name == "Comp");
    REQUIRE(res.patch->nodes[2].desc.name == "Reverb");

    // UserNames override carried over.
    REQUIRE(res.patch->nodes[0].customName == "My EQ");

    // Identity preserved from the v2 <PLUGIN>.
    REQUIRE(res.patch->nodes[0].desc.manufacturerName == "Acme");
    REQUIRE(res.patch->nodes[0].desc.pluginFormatName == "VST");
    REQUIRE_FALSE(res.patch->nodes[0].desc.isInstrument);

    // The translated graph is structurally valid (no dangling refs / cycles).
    REQUIRE(session::validatePatch(*res.patch) == session::PatchValidation::Ok);

    // Three FILTERs carry <STATE>, but the two internal-IO ones collapse to graphIO
    // before the STATE check, so exactly one "state not migrated" warning fires.
    REQUIRE(res.warnings.size() == kBaselineWarnings);
}

TEST_CASE("v2 import: rejects a non-v2 root", "[session][v2import]")
{
    juce::XmlElement v3("PEDALBOARD");
    auto res = session::v2::importPedalboard2Patch(v3);
    REQUIRE(res.patch == nullptr);
    REQUIRE(res.error.isNotEmpty());
}

// A faithful (but STATE-trimmed) copy of a real user patch: five VST2 effects
// whose FILTERs are listed out of signal order, with the true chain expressed
// only by the CONNECTIONs. Reconstruction must yield the audio-flow order
//   Pro-Q 3 -> Pro-C 2 -> Clear -> IVGI2 -> Pro-G
// (Input -> 8 -> 9 -> 11 -> 10 -> 7 -> Output). Guards #10 against the exact
// topology shape we ship for. The huge base64 <STATE> blobs are shortened to a
// token; their mere presence must still raise the "state not migrated" note.
constexpr auto kRealWorldV2Xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<Pedalboard2PatchFile>
  <Patch tempo="120" name="1 - &lt;untitled&gt;">
    <FILTERGRAPH>
      <FILTER uid="1" x="0" y="0">
        <PLUGIN name="Audio Input" format="Internal" category="I/O devices"
                manufacturer="Raw Material Software" uid="246006c0" isInstrument="0"
                numInputs="0" numOutputs="4"/>
        <STATE>0.</STATE>
      </FILTER>
      <FILTER uid="3" x="865" y="0">
        <PLUGIN name="Audio Output" format="Internal" category="I/O devices"
                manufacturer="Raw Material Software" uid="724248cb" isInstrument="0"
                numInputs="2" numOutputs="0"/>
        <STATE>0.</STATE>
      </FILTER>
      <FILTER uid="7" x="685" y="0" oscMIDIAddress="" MIDIChannel="0">
        <PLUGIN name="FabFilter Pro-G" format="VST" category="Mastering" manufacturer="FabFilter"
                version="V1.3.4.0" file="D:\main\MUSIC\!-VST\FX\FabFilter Pro-G.dll"
                uid="46504772" isInstrument="0" numInputs="4" numOutputs="2"/>
        <STATE>352.CMlaKA</STATE>
      </FILTER>
      <FILTER uid="8" x="95" y="0" oscMIDIAddress="" MIDIChannel="0">
        <PLUGIN name="FabFilter Pro-Q 3" format="VST" manufacturer="FabFilter" version="V3.2.7.0"
                file="D:\main\MUSIC\!-VST\FX\FabFilter Pro-Q 3.dll" uid="46513370"
                isInstrument="0" numInputs="4" numOutputs="2"/>
        <STATE>1664.CMlaKA</STATE>
      </FILTER>
      <FILTER uid="9" x="276" y="0" oscMIDIAddress="" MIDIChannel="0">
        <PLUGIN name="FabFilter Pro-C 2" format="VST" category="Mastering" manufacturer="FabFilter"
                version="V2.1.5.0" file="D:\main\MUSIC\!-VST\FX\FabFilter\FabFilter Pro-C 2.dll"
                uid="46433270" isInstrument="0" numInputs="4" numOutputs="2"/>
        <STATE>396.CMlaKA</STATE>
      </FILTER>
      <FILTER uid="10" x="568" y="0" oscMIDIAddress="" MIDIChannel="0">
        <PLUGIN name="IVGI2" format="VST" category="Effect" manufacturer="Klanghelm"
                version="V1.3.2.3.5.2" file="D:\main\MUSIC\!-VST\FX\IVGI2.dll"
                uid="49564732" isInstrument="0" numInputs="2" numOutputs="2"/>
        <STATE>431.CMlaKA</STATE>
      </FILTER>
      <FILTER uid="11" x="451" y="0" oscMIDIAddress="" MIDIChannel="0">
        <PLUGIN name="Clear" format="VST" category="Effect" manufacturer="Supertone"
                version="V6.5.7.9.3" file="D:\main\MUSIC\!-VST\FX\Clear.dll"
                uid="636c6572" isInstrument="0" numInputs="2" numOutputs="2"/>
        <STATE>818.CMlaKA</STATE>
      </FILTER>
      <CONNECTION srcFilter="1" srcChannel="0" dstFilter="8" dstChannel="0"/>
      <CONNECTION srcFilter="1" srcChannel="1" dstFilter="8" dstChannel="1"/>
      <CONNECTION srcFilter="7" srcChannel="0" dstFilter="3" dstChannel="0"/>
      <CONNECTION srcFilter="7" srcChannel="1" dstFilter="3" dstChannel="1"/>
      <CONNECTION srcFilter="8" srcChannel="0" dstFilter="9" dstChannel="0"/>
      <CONNECTION srcFilter="8" srcChannel="1" dstFilter="9" dstChannel="1"/>
      <CONNECTION srcFilter="9" srcChannel="0" dstFilter="11" dstChannel="0"/>
      <CONNECTION srcFilter="9" srcChannel="1" dstFilter="11" dstChannel="1"/>
      <CONNECTION srcFilter="10" srcChannel="0" dstFilter="7" dstChannel="0"/>
      <CONNECTION srcFilter="10" srcChannel="1" dstFilter="7" dstChannel="1"/>
      <CONNECTION srcFilter="11" srcChannel="0" dstFilter="10" dstChannel="0"/>
      <CONNECTION srcFilter="11" srcChannel="1" dstFilter="10" dstChannel="1"/>
    </FILTERGRAPH>
    <Mappings/>
    <UserNames/>
  </Patch>
  <DEVICESETUP deviceType="ASIO" audioOutputDeviceName="Voicemeeter Insert Virtual ASIO"/>
</Pedalboard2PatchFile>)";

TEST_CASE("v2 import: real-world FabFilter chain reconstructs in signal order", "[session][v2import]")
{
    auto v2 = parse(kRealWorldV2Xml);
    REQUIRE(v2 != nullptr);
    REQUIRE(session::v2::isPedalboard2Patch(*v2));

    auto res = session::v2::importPedalboard2Patch(*v2);
    REQUIRE(res.patch != nullptr);
    REQUIRE(res.error.isEmpty());

    // XML entity in the patch name is decoded.
    REQUIRE(res.patch->name == "1 - <untitled>");

    // Two Internal I/O filters collapse to graphIO; the five effects remain,
    // ordered by the connection chain rather than the file's FILTER order.
    REQUIRE(res.patch->nodes.size() == 5);
    REQUIRE(res.patch->nodes[0].desc.name == "FabFilter Pro-Q 3");
    REQUIRE(res.patch->nodes[1].desc.name == "FabFilter Pro-C 2");
    REQUIRE(res.patch->nodes[2].desc.name == "Clear");
    REQUIRE(res.patch->nodes[3].desc.name == "IVGI2");
    REQUIRE(res.patch->nodes[4].desc.name == "FabFilter Pro-G");

    // Identity carried through for matching against installed VST3 builds.
    REQUIRE(res.patch->nodes[0].desc.manufacturerName == "FabFilter");
    REQUIRE(res.patch->nodes[3].desc.manufacturerName == "Klanghelm");
    REQUIRE(res.patch->nodes[2].desc.manufacturerName == "Supertone");

    // Structurally valid (no dangling refs / cycles) and the state-drop note fired.
    REQUIRE(session::validatePatch(*res.patch) == session::PatchValidation::Ok);
    REQUIRE(res.warnings.size() >= 1);
}

// ---- v2 <Mappings> import (uid 8=EQ=nodes[0], 9=Comp=nodes[1], 7=Reverb=nodes[2]) ----

TEST_CASE("v2 import: a CC MidiMapping translates to a v3 MIDI mapping", "[session][v2import]")
{
    auto v2 = parseWithMappings(
        R"(<MidiMapping pluginId="8" parameter="3" cc="7" latch="0" channel="1" lowerBound="0.0" upperBound="1.0"/>)");
    REQUIRE(v2 != nullptr);

    auto res = session::v2::importPedalboard2Patch(*v2);
    REQUIRE(res.patch != nullptr);
    REQUIRE(res.error.isEmpty());
    REQUIRE(res.patch->midiMappings.size() == 1);
    REQUIRE(res.patch->oscMappings.empty());

    const auto& m = res.patch->midiMappings[0];
    REQUIRE(m.source.kind == control::MidiSourceKind::ControlChange);
    REQUIRE(m.source.channel == 1);
    REQUIRE(m.source.controller == 7);
    REQUIRE(m.target.kind == control::MidiTargetKind::PluginParameter);
    REQUIRE(m.target.parameterIndex == 3);
    REQUIRE(m.target.node == res.patch->nodes[0].id);  // EQ (uid 8)
    REQUIRE(res.warnings.size() == kBaselineWarnings); // no drop warning
}

TEST_CASE("v2 import: an OscMapping translates with address + argument index", "[session][v2import]")
{
    auto v2 =
        parseWithMappings(R"(<OscMapping pluginId="9" parameter="5" address="/pb/comp/thresh" parameterIndex="2"/>)");
    REQUIRE(v2 != nullptr);

    auto res = session::v2::importPedalboard2Patch(*v2);
    REQUIRE(res.patch != nullptr);
    REQUIRE(res.error.isEmpty());
    REQUIRE(res.patch->oscMappings.size() == 1);
    REQUIRE(res.patch->midiMappings.empty());

    const auto& o = res.patch->oscMappings[0];
    REQUIRE(o.address == "/pb/comp/thresh");
    REQUIRE(o.argumentIndex == 2); // the v2 OSC arg index
    REQUIRE(o.target.kind == control::MidiTargetKind::PluginParameter);
    REQUIRE(o.target.parameterIndex == 5);            // the plugin parameter (v2 "parameter")
    REQUIRE(o.target.node == res.patch->nodes[1].id); // Comp (uid 9)
}

TEST_CASE("v2 import: a parameter=-1 bypass toggle is dropped with a warning", "[session][v2import]")
{
    auto v2 = parseWithMappings(R"(<MidiMapping pluginId="8" parameter="-1" cc="64" latch="1"/>)");
    REQUIRE(v2 != nullptr);

    auto res = session::v2::importPedalboard2Patch(*v2);
    REQUIRE(res.patch != nullptr);
    REQUIRE(res.error.isEmpty());
    REQUIRE(res.patch->midiMappings.empty()); // never coerced to a real parameter index
    REQUIRE(res.patch->oscMappings.empty());
    REQUIRE(res.warnings.size() == kBaselineWarnings + 1);

    bool sawBypassWarning = false;
    for (const auto& w : res.warnings)
        if (w.containsIgnoreCase("bypass"))
            sawBypassWarning = true;
    REQUIRE(sawBypassWarning);
}

TEST_CASE("v2 import: mappings to unknown / graph-IO plugins are dropped; valid survives", "[session][v2import]")
{
    auto v2 = parseWithMappings(
        R"(<MidiMapping pluginId="8" parameter="0" cc="1" channel="1" lowerBound="0.0" upperBound="1.0"/>)"
        R"(<MidiMapping pluginId="999" parameter="0" cc="2" channel="1" lowerBound="0.0" upperBound="1.0"/>)"
        R"(<MidiMapping pluginId="1" parameter="0" cc="3" channel="1" lowerBound="0.0" upperBound="1.0"/>)");
    REQUIRE(v2 != nullptr);

    auto res = session::v2::importPedalboard2Patch(*v2);
    REQUIRE(res.patch != nullptr);
    REQUIRE(res.error.isEmpty());
    REQUIRE(res.patch->midiMappings.size() == 1); // only the uid-8 (EQ) mapping survives
    REQUIRE(res.patch->midiMappings[0].target.node == res.patch->nodes[0].id);
    REQUIRE(res.warnings.size() == kBaselineWarnings + 2); // unknown-plugin + graph-IO drops
}

TEST_CASE("v2 import: empty and missing <Mappings> are clean no-ops", "[session][v2import]")
{
    SECTION("empty <Mappings/>")
    {
        auto v2 = parse(kV2Xml);
        REQUIRE(v2 != nullptr);
        auto res = session::v2::importPedalboard2Patch(*v2);
        REQUIRE(res.patch != nullptr);
        REQUIRE(res.patch->midiMappings.empty());
        REQUIRE(res.patch->oscMappings.empty());
        REQUIRE(res.warnings.size() == kBaselineWarnings);
    }
    SECTION("no <Mappings> element at all")
    {
        auto v2 = parseWithoutMappings();
        REQUIRE(v2 != nullptr);
        auto res = session::v2::importPedalboard2Patch(*v2);
        REQUIRE(res.patch != nullptr);
        REQUIRE(res.patch->midiMappings.empty());
        REQUIRE(res.patch->oscMappings.empty());
        REQUIRE(res.warnings.size() == kBaselineWarnings);
    }
}

TEST_CASE("v2 import: channel + range carry across, including an inverted (lo>hi) range", "[session][v2import]")
{
    auto v2 = parseWithMappings(
        R"(<MidiMapping pluginId="9" parameter="1" cc="11" channel="10" lowerBound="0.2" upperBound="0.8"/>)"
        R"(<MidiMapping pluginId="7" parameter="2" cc="12" channel="0" lowerBound="0.9" upperBound="0.1"/>)");
    REQUIRE(v2 != nullptr);

    auto res = session::v2::importPedalboard2Patch(*v2);
    REQUIRE(res.patch != nullptr);
    REQUIRE(res.patch->midiMappings.size() == 2);

    const auto& a = res.patch->midiMappings[0]; // Comp (uid 9)
    REQUIRE(a.source.channel == 10);
    REQUIRE(a.rangeMin == Approx(0.2f));
    REQUIRE(a.rangeMax == Approx(0.8f));
    REQUIRE_FALSE(a.inverted);
    REQUIRE(a.target.node == res.patch->nodes[1].id);

    const auto& b = res.patch->midiMappings[1]; // Reverb (uid 7)
    REQUIRE(b.source.channel == 0);             // omni
    REQUIRE(b.rangeMin == Approx(0.9f));
    REQUIRE(b.rangeMax == Approx(0.1f)); // lo>hi carried verbatim, inversion rides the range
    REQUIRE_FALSE(b.inverted);
    REQUIRE(b.target.node == res.patch->nodes[2].id);
}
