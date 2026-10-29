#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "UI/Controls.h"
#include "Parameters.h"

class RobKitProcessor;

// "Red Room" interface: 1180 x 700 design, scaled as a whole. Static artwork is cached; the
// kit stage, meters and status repaint only while something is moving.
namespace rr
{
namespace Col
{
    inline const juce::Colour bg { 0xff090e12 }, plateA { 0xff182328 }, plateB { 0xff10181d }, line { 0xff2d383b },
        border { 0xff354045 }, bone { 0xffe7eadf }, dim { 0xff8d9d9e }, label { 0xffbac6c0 }, lime { 0xffc4fb58 },
        cyan { 0xff68dfdd }, brass { 0xffdfb56c }, red { 0xffee6358 }, pink { 0xffed83bc };
}

struct Images
{
    static const Images& get();
    juce::Image kit, wordmark, halo, texture, screw, knob;
};

// Dial drawn from the supplied 65-frame knob strip with the section's accent on the pointer.
class Dial : public ui::Knob
{
public:
    Dial (RobKitProcessor& p, const char* id, juce::String label, float diameter, juce::Colour accent);
    void paint (juce::Graphics&) override;
private:
    float d;
};

// Mixer level fader: a thin track with an accent thumb and a dB readout.
class Fader : public ui::ParamControl
{
public:
    Fader (RobKitProcessor& p, const char* id, juce::Colour accent);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
private:
    juce::Rectangle<float> track() const { return { 0.0f, (float) getHeight() * 0.5f - 2.0f, 108.0f, 4.0f }; }
    bool dragging = false;
};

struct Part { juce::String id, label; int note; int group; juce::Rectangle<float> box; float cxF, cyF, ryF; juce::Point<float> labelAt; std::vector<int> notes; };

// The drum kit: aspect-fit scene, generous elliptical hit areas, incoming-hit glow, readouts.
class KitStage : public juce::Component
{
public:
    explicit KitStage (RobKitProcessor& p);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    bool tick();                                         // true while glowing
    int partAt (juce::Point<float> p) const;             // -1 for none
    juce::Rectangle<float> hitEllipse (int part) const;  // in component coordinates
    float velocityAt (int part, juce::Point<float> p) const;
    void setSelection (const juce::String& name, int note, int velocity, int group);
    const std::vector<Part>& parts() const { return partList; }
    std::function<void (int part, int velocity)> onHit;
private:
    juce::AffineTransform view() const;
    RobKitProcessor& proc;
    std::vector<Part> partList;
    std::vector<std::vector<int>> partArts;
    std::vector<uint32_t> seen;
    std::vector<float> glow;
    int hover = -1, selectedGroup = Kick;
    juce::String selName { "KICK" }, selNote { "C1 \xc2\xb7 36" }, selVel { "VEL 100" };
};

class StrikeButton : public juce::Component
{
public:
    StrikeButton (juce::String t, int n) : text (std::move (t)), note (n) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { if (onClick) onClick(); }
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
    juce::String text; int note; bool selected = false, hover = false;
    std::function<void()> onClick;
};

class TextButtonRR : public juce::Component, public juce::SettableTooltipClient
{
public:
    TextButtonRR (juce::String t, bool framed = true) : text (std::move (t)), frame (framed) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { if (onClick) onClick(); }
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
    juce::String text; bool frame, hover = false;
    std::function<void()> onClick;
};

class Meters : public juce::Component
{
public:
    explicit Meters (RobKitProcessor& p) : proc (p) {}
    bool tick();
    void paint (juce::Graphics&) override;
private:
    RobKitProcessor& proc;
    float level[2] { -60.0f, -60.0f }, peakDb = -100.0f, peakAge = 0.0f;
};

class Panel : public juce::Component
{
public:
    static constexpr int W = 1180, H = 700;
    explicit Panel (RobKitProcessor& p);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void tick();
    std::function<void (float)> onScale;
    int boundControlCount() const { return (int) controls.size(); }
    const std::vector<ui::ParamControl*>& boundControls() const { return controls; }
    KitStage& stage() { return kit; }
    void selectGroup (int group, int selectedNote = -1);
    void audition (int note, int velocity);
private:
    template <class T, class... A> T* own (juce::Rectangle<int> r, A&&... args);
    void showSetup();
    void showMap();
    void resetControls();

    RobKitProcessor& proc;
    juce::Image cache;
    std::vector<ui::ParamControl*> controls;
    std::vector<std::unique_ptr<juce::Component>> owned;
    KitStage kit { proc };
    Meters meters { proc };
    std::vector<StrikeButton*> strikes;
    int group = Kick;
    juce::String status { "READY TO HIT" }, lastStatus;
    std::array<uint32_t, 64> seenHits {};
    float statusAge = 0.0f;
};
} // namespace rr

class RobKitEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit RobKitEditor (RobKitProcessor&);
    ~RobKitEditor() override { stopTimer(); }
    void paint (juce::Graphics& g) override { g.fillAll (rr::Col::bg); }
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    rr::Panel& getPanel() { return panel; }
    void tickForTest() { panel.tick(); }
private:
    void timerCallback() override { panel.tick(); }
    RobKitProcessor& proc;
    rr::Panel panel;
    juce::TooltipWindow tips { this, 650 };
};
