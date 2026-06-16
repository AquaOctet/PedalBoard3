// Tests for the Pedalboard2 (.pdl v2) importer -- the pure XML -> v3 Patch
// transform (master plan §4.2.B.1 + Appendix D). Uses a self-contained inline
// v2 fixture mirroring a real Pedalboard2 file: internal Audio Input/Output
// endpoints, real plugin FILTERs with STATE, an out-of-order FILTER listing
// whose CONNECTIONs define the true signal chain, and a UserNames override.

#include <catch2/catch_test_macros.hpp>

#include "session/v2_import/PdlV2Importer.h"

#include <juce_core/juce_core.h>

using namespace pedalboard3;

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

    // Two FILTERs had <STATE> -> a single "state not migrated" warning.
    REQUIRE(res.warnings.size() >= 1);
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
