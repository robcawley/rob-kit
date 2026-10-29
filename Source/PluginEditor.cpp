#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "BinaryData.h"

using namespace juce;

namespace rr
{
namespace
{
    const Colour groupAccent[NumGroups] { Col::lime, Col::cyan, Col::cyan, Col::red, Col::brass, Col::pink };
    const char* groupName[NumGroups] { "KICK", "SNARE", "HATS", "TOMS", "CYMBALS", "PERC" };

    Font raj (float h, bool bold = true, float spacing = 0.0f)
    {
        auto f = bold ? ui::Fonts::get().heavy (h) : ui::Fonts::get().label (h);
        return spacing != 0.0f ? f.withExtraKerningFactor (spacing / h) : f;
    }
    Font mono (float h, float spacing = 0.0f)
    {
        auto f = ui::Fonts::get().mono (h);
        return spacing != 0.0f ? f.withExtraKerningFactor (spacing / h) : f;
    }
    void txt (Graphics& g, const String& s, Rectangle<float> r, const Font& f, Colour c, Justification j = Justification::centredLeft)
    {
        g.setFont (f); g.setColour (c); g.drawText (s, r, j, false);
    }
    String noteText (int note) { return MidiMessage::getMidiNoteName (note, true, true, 3) + String::fromUTF8 (" \xc2\xb7 ") + String (note); }

    // The articulation buttons for each group: the design's 18, plus the main hand percussion.
    struct Strike { const char* name; int note; int group; };
    const Strike kStrikes[] {
        { "KICK", 36, Kick }, { "KICK 2", 35, Kick },
        { "SNARE", 38, Snare }, { "EDGE", 34, Snare }, { "SIDESTICK", 37, Snare }, { "RIMSHOT", 40, Snare },
        { "CLOSED", 42, Hats }, { "PEDAL", 44, Hats }, { "OPEN", 46, Hats },
        { "HIGH TOM", 48, Toms }, { "MID TOM", 47, Toms }, { "LOW TOM", 43, Toms },
        { "CRASH L", 49, Cymbals }, { "CRASH R", 57, Cymbals }, { "SPLASH", 55, Cymbals }, { "RIDE", 51, Cymbals }, { "BELL", 53, Cymbals },
        { "CLAP", 39, Perc }, { "TAMBOURINE", 54, Perc }, { "SHAKER", 82, Perc }, { "COWBELL", 56, Perc },
        { "CONGA", 63, Perc }, { "BONGO", 60, Perc }, { "CLAVES", 75, Perc } };

    struct ResetAction : UndoableAction
    {
        std::vector<std::tuple<RangedAudioParameter*, float, float>> changes;
        void apply (bool forward)
        {
            for (auto& [p, from, to] : changes) { p->beginChangeGesture(); p->setValueNotifyingHost (forward ? to : from); p->endChangeGesture(); }
        }
        bool perform() override { apply (true); return true; }
        bool undo() override { apply (false); return true; }
        int getSizeInUnits() override { return (int) changes.size() * 8; }
    };
}

const Images& Images::get()
{
    static const Images im = []
    {
        Images i;
        i.kit      = ImageCache::getFromMemory (BinaryData::kit_scene_png, BinaryData::kit_scene_pngSize);
        i.wordmark = ImageCache::getFromMemory (BinaryData::wordmark_png, BinaryData::wordmark_pngSize);
        i.halo     = ImageCache::getFromMemory (BinaryData::hit_halo_png, BinaryData::hit_halo_pngSize);
        i.texture  = ImageCache::getFromMemory (BinaryData::panel_texture_png, BinaryData::panel_texture_pngSize);
        i.screw    = ImageCache::getFromMemory (BinaryData::screw_png, BinaryData::screw_pngSize);
        i.knob     = ImageCache::getFromMemory (BinaryData::knob_strip_png, BinaryData::knob_strip_pngSize);
        return i;
    }();
    return im;
}

//==============================================================================
Dial::Dial (RobKitProcessor& p, const char* id, String lbl, float diameter, Colour accent)
    : ui::Knob (p, id, std::move (lbl), ui::Knob::Size::Small, accent), d (diameter) {}

void Dial::paint (Graphics& g)
{
    const auto& im = Images::get();
    const auto r = Rectangle<float> (d, d).withCentre ({ (float) getWidth() * 0.5f, d * 0.5f + 1.0f });
    const int frame = jlimit (0, 64, roundToInt (norm() * 64.0f));
    g.setOpacity (1.0f);
    g.drawImage (im.knob.getClippedImage ({ 0, frame * 128, 128, 128 }), r);     // supplied strip, 128 px frames

    // accent over the strip's neutral pointer (same geometry: radius 8 to 15 of 64)
    const float ang = degreesToRadians (-135.0f + 270.0f * norm());
    const Point<float> v (std::sin (ang), -std::cos (ang));
    const float k = d / 64.0f;
    Path ptr; ptr.startNewSubPath (r.getCentre() + v * 8.0f * k); ptr.lineTo (r.getCentre() + v * 15.0f * k);
    g.setColour (colour.withAlpha (hovering ? 0.45f : 0.3f));
    g.strokePath (ptr, PathStrokeType (5.5f * k, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (colour);
    g.strokePath (ptr, PathStrokeType (2.4f * k, PathStrokeType::curved, PathStrokeType::rounded));
    if (modulated())
    {
        const float ea = degreesToRadians (-135.0f + 270.0f * effectiveNorm());
        g.setColour (colour.withAlpha (0.8f));
        g.fillEllipse (Rectangle<float> (3.0f, 3.0f).withCentre (r.getCentre() + Point<float> (std::sin (ea), -std::cos (ea)) * (d * 0.5f + 2.0f)));
    }

    const bool small = d < 32.0f;
    auto t = getLocalBounds().toFloat().withTrimmedTop (d + (small ? 5.0f : 5.0f));
    txt (g, label, t.removeFromTop (small ? 10.0f : 14.0f), raj (small ? 8.5f : 10.0f, true, 0.7f), Col::label, Justification::centred);
    const String vt = isLearning() ? String ("MOVE A KNOB") : valueText();
    txt (g, vt, t.removeFromTop (12.0f), mono (small ? 8.5f : 9.0f), isLearning() ? Col::pink : colour, Justification::centred);
    if (const int cc = learnedCC(); cc >= 0)
        txt (g, "CC" + String (cc), r.translated (r.getWidth() * 0.62f, -3.0f).withSize (30.0f, 10.0f), mono (8.0f), Col::brass);
}

//==============================================================================
Fader::Fader (RobKitProcessor& p, const char* id, Colour accent) : ParamControl (p, id, {}) { colour = accent; }

void Fader::paint (Graphics& g)
{
    const auto t = track();
    g.setColour (Colour (0xff050c10)); g.fillRect (t);
    g.setColour (Colour (0xff354246)); g.drawRect (t, 1.0f);
    const float x = norm() * (t.getWidth() - 7.0f);
    const auto thumb = Rectangle<float> (7.0f, 13.0f).withCentre ({ t.getX() + x + 3.5f, t.getCentreY() });
    g.setColour (colour.withAlpha (0.25f)); g.fillRoundedRectangle (thumb.expanded (2.0f), 2.0f);
    g.setColour (colour); g.fillRoundedRectangle (thumb, 1.0f);
    const String vt = isLearning() ? String ("LEARN") : valueText().replace ("-inf", String::fromUTF8 ("\xe2\x88\x92\xe2\x88\x9e"));
    txt (g, vt, getLocalBounds().toFloat().withTrimmedLeft (110.0f), mono (9.0f), isLearning() ? Col::pink : Colour (0xffb8c4bc), Justification::centredRight);
}

void Fader::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { showMenu(); return; }
    if (e.mods.isAltDown()) { typeValue(); return; }
    dragging = true;
    beginEdit();
    if (! e.mods.isShiftDown()) setNorm (jlimit (0.0f, 1.0f, (e.position.x - 3.5f) / (track().getWidth() - 7.0f)));
}

void Fader::mouseDrag (const MouseEvent& e)
{
    if (! dragging) return;
    if (e.mods.isShiftDown()) setNorm (norm() + (float) (e.position.x - e.mouseDownPosition.x) / 4000.0f);
    else setNorm (jlimit (0.0f, 1.0f, (e.position.x - 3.5f) / (track().getWidth() - 7.0f)));
}

void Fader::mouseUp (const MouseEvent&) { if (dragging) endEdit(); dragging = false; }
void Fader::mouseDoubleClick (const MouseEvent&) { setNormComplete (param.getDefaultValue()); }
void Fader::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
{
    const float step = e.mods.isShiftDown() ? 0.004f : 0.02f;
    const float dlt = (w.deltaY != 0.0f ? w.deltaY : w.deltaX) * (w.isReversed ? -1.0f : 1.0f);
    setNormComplete (norm() + (dlt > 0 ? step : -step));
}

//==============================================================================
KitStage::KitStage (RobKitProcessor& p) : proc (p)
{
    // geometry from the design pack's kit-layout.json (1000 x 590 view box) and its hit-zone rules
    struct Def { const char* id; const char* label; int note; int group; float x, y, w, h, lx, ly; std::vector<int> notes; };
    const Def defs[] {
        { "crash-l", "CRASH L", 49, Cymbals, 110, 75, 218, 103, 204, 57, { 49, 28 } },
        { "crash-r", "CRASH R", 57, Cymbals, 633, 65, 236, 106, 760, 42, { 57, 29 } },
        { "ride", "RIDE", 51, Cymbals, 701, 214, 230, 102, 883, 205, { 51, 59, 53, 52 } },
        { "splash", "SPLASH", 55, Cymbals, 439, 45, 130, 67, 505, 35, { 55 } },
        { "hats", "HATS", 42, Hats, 68, 242, 160, 83, 123, 234, { 42, 44, 33, 46, 31 } },
        { "tom-low", "LOW TOM", 43, Toms, 641, 338, 204, 157, 866, 467, { 43, 41 } },
        { "tom-mid", "MID TOM", 47, Toms, 609, 202, 175, 139, 661, 195, { 47, 45 } },
        { "tom-high", "HIGH TOM", 48, Toms, 323, 134, 176, 138, 410, 125, { 48, 50 } },
        { "snare", "SNARE", 38, Snare, 201, 316, 189, 121, 257, 461, { 38, 34, 37, 40, 32 } },
        { "kick", "KICK", 36, Kick, 372, 282, 277, 239, 505, 563, { 36, 35 } } };
    for (const auto& d : defs)
    {
        const bool kick = String (d.id) == "kick", cym = d.group == Cymbals || d.group == Hats;
        Part pt { d.id, d.label, d.note, d.group, { d.x, d.y, d.w, d.h },
                  kick ? 0.48f : 0.5f, kick ? 0.55f : (cym ? 0.4f : 0.34f), kick ? 0.36f : (cym ? 0.29f : 0.23f),
                  { d.lx, d.ly }, d.notes };
        partList.push_back (pt);
        std::vector<int> arts;
        for (int n : d.notes) if (const int a = p.kit().articulationForNote (n); a >= 0) arts.push_back (a);
        partArts.push_back (arts);
    }
    seen.assign (partList.size(), 0);
    glow.assign (partList.size(), 0.0f);
    for (size_t i = 0; i < partList.size(); ++i)
        for (int a : partArts[i]) seen[i] += p.telemetry().hits[(size_t) a].load();
    setMouseCursor (MouseCursor::PointingHandCursor);
    setTitle ("Drum kit");
    setDescription ("Click a drum or cymbal to audition it; higher on the head plays harder.");
}

AffineTransform KitStage::view() const
{
    const float s = std::min ((float) getWidth() / 1000.0f, (float) getHeight() / 590.0f);
    return AffineTransform::scale (s).translated (((float) getWidth() - 1000.0f * s) * 0.5f, ((float) getHeight() - 590.0f * s) * 0.5f);
}

Rectangle<float> KitStage::hitEllipse (int i) const
{
    const auto& p = partList[(size_t) i];
    const float cx = p.box.getX() + p.box.getWidth() * p.cxF, cy = p.box.getY() + p.box.getHeight() * p.cyF;
    const float rx = p.box.getWidth() * 0.43f, ry = p.box.getHeight() * p.ryF;
    return Rectangle<float> (cx - rx, cy - ry, rx * 2.0f, ry * 2.0f).transformedBy (view());
}

int KitStage::partAt (Point<float> pt) const
{
    for (int i = (int) partList.size() - 1; i >= 0; --i)     // later parts sit on top, as in the design
    {
        const auto e = hitEllipse (i);
        const float dx = (pt.x - e.getCentreX()) / (e.getWidth() * 0.5f), dy = (pt.y - e.getCentreY()) / (e.getHeight() * 0.5f);
        if (dx * dx + dy * dy <= 1.0f) return i;
    }
    return -1;
}

float KitStage::velocityAt (int i, Point<float> pt) const
{
    const auto e = hitEllipse (i);
    return 25.0f + 102.0f * (1.0f - jlimit (0.0f, 1.0f, (pt.y - e.getY()) / e.getHeight()));
}

void KitStage::setSelection (const String& name, int note, int velocity, int grp)
{
    selName = name; selNote = noteText (note); selVel = "VEL " + String (velocity); selectedGroup = grp;
    repaint();
}

void KitStage::mouseDown (const MouseEvent& e)
{
    const int i = partAt (e.position);
    if (i >= 0 && onHit) onHit (i, roundToInt (velocityAt (i, e.position)));
}

void KitStage::mouseMove (const MouseEvent& e)
{
    const int i = partAt (e.position);
    if (i != hover) { hover = i; repaint(); }
}

bool KitStage::tick()
{
    bool active = false;
    auto& t = proc.telemetry();
    for (size_t i = 0; i < partList.size(); ++i)
    {
        uint32_t total = 0; float vel = 0.0f;
        for (int a : partArts[i]) { total += t.hits[(size_t) a].load (std::memory_order_relaxed); vel = std::max (vel, t.lastVel[(size_t) a].load()); }
        if (total != seen[i]) { seen[i] = total; glow[i] = 0.55f + 0.45f * vel; active = true; }
        else if (glow[i] > 0.0f) { glow[i] = std::max (0.0f, glow[i] - 0.2f); active = true; }   // ~140 ms then gone
    }
    if (active) repaint();
    return active;
}

void KitStage::paint (Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.setColour (Colour (0xff0c1318)); g.fillRect (b);
    g.setGradientFill (ColourGradient (Colour (0x6629333a), b.getCentreX(), b.getHeight() * 0.7f,
                                       Colours::transparentBlack, b.getCentreX() + b.getWidth() * 0.45f, b.getHeight() * 0.7f, true));
    g.fillRect (b);
    g.setColour (Colour (0x0851615a));
    for (float y = 39.0f; y < b.getHeight(); y += 40.0f) g.drawHorizontalLine ((int) y, 0.0f, b.getWidth());
    txt (g, "RED ROOM", { 22.0f, 10.0f, 700.0f, 70.0f }, raj (57.0f, true, 13.0f), Colour (0x0e4e5e59), Justification::topLeft);

    const auto vt = view();
    const auto kitRect = Rectangle<float> (0, 0, 1000, 590).transformedBy (vt);
    g.setOpacity (1.0f);                                     // images take the current colour's alpha
    g.drawImage (Images::get().kit, kitRect);

    for (int i = 0; i < (int) partList.size(); ++i)
    {
        const auto e = hitEllipse (i);
        const auto& p = partList[(size_t) i];
        if (glow[(size_t) i] > 0.0f)
        {
            const float a = glow[(size_t) i];
            g.setOpacity (a);
            g.drawImage (Images::get().halo, e.withSizeKeepingCentre (e.getWidth() * 1.45f, e.getHeight() * 1.9f));
            g.setOpacity (1.0f);
            g.setColour (Col::lime.withAlpha (0.25f * a)); g.fillEllipse (e);
            g.setColour (Colour (0xffecffd6).withAlpha (a)); g.drawEllipse (e, 2.5f);
        }
        else if (i == hover)
        {
            g.setColour (Colour (0x0cd1ffe0)); g.fillEllipse (e);
            g.setColour (Colour (0x7ad2ffe0)); g.drawEllipse (e, 1.5f);
        }
        else if (p.group == selectedGroup)
        {
            g.setColour (Colour (0x77c4fb58)); g.drawEllipse (e, 1.5f);
        }
        // part labels (drawn larger than the design's for legibility at normal size)
        const auto lp = p.labelAt.transformedBy (vt);
        txt (g, p.label, Rectangle<float> (90.0f, 14.0f).withCentre (lp.translated (0.0f, -3.0f)), raj (10.0f, true, 1.6f),
              Colour (0xff9aaba8), Justification::centred);
    }

    // footer readouts
    const float y = b.getBottom() - 25.0f;
    txt (g, selName, { 15.0f, y, 110.0f, 18.0f }, raj (13.0f, true, 1.0f), Col::lime);
    g.setColour (Colour (0xff3a4b48)); g.drawVerticalLine (128, y + 2.0f, y + 16.0f);
    txt (g, selNote, { 138.0f, y, 140.0f, 18.0f }, mono (10.0f, 1.0f), Colour (0xffa9b8ae));
    txt (g, selVel, { b.getWidth() - 115.0f, y, 100.0f, 18.0f }, mono (9.0f, 1.0f), Colour (0xff82998e), Justification::centredRight);
}

//==============================================================================
void StrikeButton::paint (Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (selected ? Colour (0x0bbfff53) : Colour (0xff111a1f)); g.fillRoundedRectangle (r, 3.0f);
    g.setColour (selected ? Colour (0xff9fc44d) : (hover ? Col::lime : Colour (0xff354345))); g.drawRoundedRectangle (r, 3.0f, 1.0f);
    txt (g, text, r, raj (10.5f, true, 0.6f), selected || hover ? Col::lime : Colour (0xffb8c4bc), Justification::centred);
}

void TextButtonRR::paint (Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    if (frame)
    {
        g.setColour (Colour (0xff111a1f)); g.fillRoundedRectangle (r, 3.0f);
        g.setColour (hover ? Col::lime : Colour (0xff394447)); g.drawRoundedRectangle (r, 3.0f, 1.0f);
        txt (g, text, r, raj (11.5f, true, 1.0f), hover ? Col::lime : Col::bone, Justification::centred);
    }
    else txt (g, text, r, mono (9.0f, 0.5f), hover ? Col::lime : Colour (0xff9dada4), Justification::centredRight);
}

//==============================================================================
bool Meters::tick()
{
    auto& t = proc.telemetry();
    bool moved = false;
    float pk = 0.0f;
    for (int c = 0; c < 2; ++c)
    {
        const float p = t.peak[c].exchange (0.0f);
        pk = std::max (pk, p);
        const float db = Decibels::gainToDecibels (p, -60.0f);
        const float nl = std::max (db, level[c] - 1.8f);
        moved |= std::abs (nl - level[c]) > 0.05f;
        level[c] = nl;
    }
    const float pdb = Decibels::gainToDecibels (pk, -100.0f);
    if (pdb > peakDb || (peakAge += 1.0f) > 45.0f) { if (std::abs (pdb - peakDb) > 0.05f) moved = true; peakDb = pdb; peakAge = 0.0f; }
    if (moved) repaint();
    return moved;
}

void Meters::paint (Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    txt (g, "L  R", { 0.0f, 0.0f, b.getWidth(), 10.0f }, mono (8.0f), Colour (0xff91a29b), Justification::centred);
    const float h = 102.0f, top = 15.0f;
    for (int c = 0; c < 2; ++c)
    {
        const auto col = Rectangle<float> (b.getCentreX() - 9.0f + (float) c * 11.0f, top, 7.0f, h);
        g.setColour (Colour (0xff050b0f)); g.fillRect (col);
        g.setColour (Colour (0xff324044)); g.drawRect (col, 1.0f);
        const float frac = jlimit (0.0f, 1.0f, (level[c] + 48.0f) / 48.0f);
        auto fill = col.reduced (1.0f).withTrimmedTop ((h - 2.0f) * (1.0f - frac));
        ColourGradient cg (Col::lime, col.getX(), col.getBottom(), Col::pink, col.getX(), col.getY(), false);
        cg.addColour (0.88, Col::brass);
        g.setGradientFill (cg); g.fillRect (fill);
        g.setColour (Colour (0xff0c1518));
        for (float y = col.getBottom() - 5.0f; y > fill.getY(); y -= 6.0f) g.fillRect (col.getX(), y, col.getWidth(), 2.0f);
    }
    const String pk = peakDb <= -99.0f ? String::fromUTF8 ("\xe2\x88\x92\xe2\x88\x9e") : String (peakDb, 1);
    txt (g, pk, { 0.0f, top + h + 4.0f, b.getWidth(), 10.0f }, mono (8.0f), peakDb > -1.0f ? Col::pink : Colour (0xff91a29b), Justification::centred);
}

//==============================================================================
template <class T, class... A>
T* Panel::own (Rectangle<int> r, A&&... args)
{
    auto c = std::make_unique<T> (std::forward<A> (args)...);
    auto* raw = c.get();
    raw->setBounds (r);
    addAndMakeVisible (raw);
    if (auto* pc = dynamic_cast<ui::ParamControl*> (raw)) controls.push_back (pc);
    owned.push_back (std::move (c));
    return raw;
}

Panel::Panel (RobKitProcessor& p) : proc (p)
{
    setSize (W, H);
    kit.setBounds (21, 141, 800, 315); addAndMakeVisible (kit);
    kit.onHit = [this] (int part, int vel)
    {
        const auto& pt = kit.parts()[(size_t) part];
        selectGroup (pt.group, pt.note);
        audition (pt.note, vel);
        kit.setSelection (pt.label, pt.note, vel, pt.group);
    };

    // 02 FEEL: 4 x 2, last cell is the note
    struct D { const char* id; const char* label; };
    const D feel[] { { PID::dynamics, "DYNAMICS" }, { PID::human, "HUMAN" }, { PID::timing, "DRIFT" }, { PID::kickBlend, "KICK BLEND" },
                     { PID::sub, "SUB" }, { PID::wires, "WIRES" }, { PID::hatClose, "HAT CLOSE" } };
    for (int k = 0; k < 7; ++k)
    {
        const float cx = 849.0f + 74.5f * ((float) (k % 4) + 0.5f);
        own<Dial> (Rectangle<int> ((int) cx - 37, k < 4 ? 147 : 218, 74, 69), proc, feel[k].id, feel[k].label, 38.0f, k < 4 ? Col::cyan : Col::brass);
    }

    // 03 THE ROOM (+ impact): 4 x 2 and the meters
    const D bus[] { { PID::room, "ROOM" }, { PID::roomSize, "SIZE" }, { PID::glue, "GLUE" }, { PID::punch, "PUNCH" },
                    { PID::drive, "DRIVE" }, { PID::width, "WIDTH" }, { PID::output, "OUTPUT" }, { PID::smash, "SMASH" } };
    for (int k = 0; k < 8; ++k)
    {
        const float cx = 849.0f + 61.5f * ((float) (k % 4) + 0.5f);
        own<Dial> (Rectangle<int> ((int) cx - 31, k < 4 ? 352 : 423, 62, 69), proc, bus[k].id, bus[k].label, 38.0f, (k % 4) == 3 ? Col::pink : Col::brass);
    }
    meters.setBounds (1098, 350, 40, 134); addAndMakeVisible (meters);

    // MAKE IT YOURS: six channels
    const float cw = 1138.0f / 6.0f;
    for (int g = 0; g < NumGroups; ++g)
    {
        const float x = 21.0f + cw * (float) g;
        own<Fader> (Rectangle<int> ((int) x + 12, 569, 160, 23), proc, PID::level (g).toRawUTF8(), groupAccent[g]);
        const char* mini[] { "TUNE", "DECAY", "TONE" };
        const juce::String ids[] { PID::tune (g), PID::decay (g), PID::tone (g) };
        const float mw = (cw - 24.0f) / 3.0f;
        for (int k = 0; k < 3; ++k)
            own<Dial> (Rectangle<int> ((int) (x + 12.0f + mw * (float) k), 592, (int) mw, 64), proc, ids[k].toRawUTF8(), mini[k], 28.0f, groupAccent[g]);
    }

    auto* reset = own<TextButtonRR> (Rectangle<int> (1000, 518, 148, 22), String::fromUTF8 ("RESET CONTROLS \xe2\x86\xba"), false);
    reset->onClick = [this] { resetControls(); };
    reset->setTooltip ("Return every control to its default (undoable).");
    auto* map = own<TextButtonRR> (Rectangle<int> (1017, 28, 75, 30), "MIDI MAP");
    map->onClick = [this] { showMap(); };
    map->setTooltip ("Every note ROB KIT plays. Right-click any control for MIDI learn.");
    auto* setup = own<TextButtonRR> (Rectangle<int> (1098, 28, 62, 30), "SETUP");
    setup->onClick = [this] { showSetup(); };

    for (size_t a = 0; a < seenHits.size(); ++a) seenHits[a] = proc.telemetry().hits[a].load();
    selectGroup (Kick, 36);
}

void Panel::selectGroup (int g, int selectedNote)
{
    group = g;
    for (auto* s : strikes) { removeChildComponent (s); }
    owned.erase (std::remove_if (owned.begin(), owned.end(), [this] (const auto& c)
                 { return std::find (strikes.begin(), strikes.end(), dynamic_cast<StrikeButton*> (c.get())) != strikes.end(); }), owned.end());
    strikes.clear();
    float x = 80.0f;
    for (const auto& s : kStrikes)
    {
        if (s.group != g) continue;
        const float w = GlyphArrangement::getStringWidth (raj (10.5f, true, 0.6f), s.name) + 22.0f;
        auto* b = own<StrikeButton> (Rectangle<int> ((int) x, 465, (int) w, 25), s.name, s.note);
        b->selected = s.note == selectedNote;
        const String nm (s.name); const int note = s.note;
        b->onClick = [this, nm, note]
        {
            for (auto* o : strikes) { o->selected = o->note == note; o->repaint(); }
            audition (note, 100);
            kit.setSelection (nm, note, 100, group);
        };
        strikes.push_back (b);
        x += w + 5.0f;
    }
    repaint (20, 545, 1140, 25);                // active channel marker
}

void Panel::audition (int note, int velocity) { proc.audition (note, (float) velocity / 127.0f); }

void Panel::resetControls()
{
    auto* act = new ResetAction();
    for (auto* p : proc.parameterList())
        if (std::abs (p->getValue() - p->getDefaultValue()) > 1.0e-6f)
            act->changes.emplace_back (p, p->getValue(), p->getDefaultValue());
    if (act->changes.empty()) { delete act; return; }
    proc.undoManager().beginNewTransaction ("Reset controls");
    proc.undoManager().perform (act);
}

void Panel::showMap()
{
    PopupMenu m;
    m.addSectionHeader ("NOTES ROB KIT PLAYS  (C1 = 36)");
    const auto& arts = proc.kit().articulations();
    for (int n = 28; n <= 87; ++n)
        if (const int a = proc.kit().articulationForNote (n); a >= 0)
            m.addItem (1000 + n, String (n).paddedLeft ('0', 2) + "  " + MidiMessage::getMidiNoteName (n, true, true, 3).paddedRight (' ', 4)
                                  + "  " + arts[(size_t) a].name, true, false);
    m.addSeparator();
    m.addItem (1, "Right-click any control to MIDI-learn it", false);
    SafePointer<Panel> safe (this);
    m.showMenuAsync (PopupMenu::Options().withMaximumNumColumns (3), [safe] (int r)
    {
        if (safe != nullptr && r >= 1000) safe->audition (r - 1000, 100);     // choosing a row plays it
    });
}

void Panel::showSetup()
{
    PopupMenu m, sizes;
    const float cur = (float) proc.apvts.state.getProperty ("uiScale", 1.0f);
    const float scales[] { 0.75f, 1.0f, 1.25f, 1.5f, 2.0f };
    for (int i = 0; i < 5; ++i) sizes.addItem (10 + i, String (roundToInt (scales[i] * 100)) + " %", true, std::abs (cur - scales[i]) < 0.01f);
    m.addSubMenu ("Interface size", sizes);
    m.addItem (2, "Undo", proc.undoManager().canUndo());
    m.addItem (3, "Redo", proc.undoManager().canRedo());
    m.addSeparator();
    m.addItem (4, "Fonts: Rajdhani, Share Tech Mono (SIL Open Font License)", false);
    SafePointer<Panel> safe (this);
    m.showMenuAsync (PopupMenu::Options(), [safe, scales] (int r)
    {
        if (safe == nullptr) return;
        if (r >= 10 && r < 15 && safe->onScale) safe->onScale (scales[r - 10]);
        if (r == 2) safe->proc.undoManager().undo();
        if (r == 3) safe->proc.undoManager().redo();
    });
}

void Panel::mouseDown (const MouseEvent& e)
{
    // clicking a channel heading selects that group's articulations
    const float cw = 1138.0f / 6.0f;
    if (e.position.y >= 545.0f && e.position.y < 570.0f && e.position.x >= 21.0f && e.position.x < 1159.0f)
    {
        const int g = jlimit (0, NumGroups - 1, (int) ((e.position.x - 21.0f) / cw));
        int first = -1;
        for (const auto& s : kStrikes) if (s.group == g) { first = s.note; break; }
        selectGroup (g, first);
    }
}

void Panel::paint (Graphics& g)
{
    const float sc = g.getInternalContext().getPhysicalPixelScaleFactor();
    if (cache.getWidth() != roundToInt ((float) W * sc))
    {
        cache = Image (Image::ARGB, roundToInt ((float) W * sc), roundToInt ((float) H * sc), true);
        Graphics c (cache);
        c.addTransform (AffineTransform::scale (sc));
        const auto& im = Images::get();
        const auto all = getLocalBounds().toFloat();
        Path shell; shell.addRoundedRectangle (all.reduced (0.5f), 9.0f);
        c.setColour (Col::bg); c.fillPath (shell);
        {
            Graphics::ScopedSaveState ss (c);
            c.reduceClipRegion (shell);
            c.setOpacity (1.0f);
            for (int y = 0; y < H; y += 256) for (int x = 0; x < W; x += 256) c.drawImage (im.texture, Rectangle<float> ((float) x, (float) y, 256.0f, 256.0f));
            c.setGradientFill (ColourGradient (Colour (0x18202a2e), 0, 0, Colours::transparentBlack, (float) W * 0.4f, (float) H * 0.3f, false));
            c.fillRect (all);
        }
        c.setColour (Colour (0xff455056)); c.strokePath (shell, PathStrokeType (1.0f));
        c.setOpacity (1.0f);
        for (float x : { 7.0f, (float) W - 18.0f }) c.drawImage (im.screw, Rectangle<float> (x, 11.0f, 11.0f, 11.0f));

        // header
        c.setOpacity (1.0f);
        c.drawImage (im.wordmark, Rectangle<float> (20.0f, 19.0f, 183.0f, 47.0f), RectanglePlacement::centred);
        c.setColour (Colour (0xff405053)); c.drawVerticalLine (221, 27.0f, 59.0f);
        txt (c, "ACOUSTIC WEIGHT.", { 237.0f, 26.0f, 200.0f, 14.0f }, raj (10.0f, true, 1.6f), Colour (0xffd4d7ca));
        txt (c, "ELECTRIC ATTITUDE.", { 237.0f, 42.0f, 200.0f, 14.0f }, raj (10.0f, true, 1.6f), Col::dim);
        txt (c, String::fromUTF8 ("ROB\xe2\x80\x99S ORIGINAL DRUMS"), { 686.0f, 22.0f, 220.0f, 12.0f }, mono (9.0f, 1.9f), Col::dim);
        {
            const auto f = raj (22.0f, true, 2.5f);
            txt (c, "01", { 686.0f, 33.0f, 40.0f, 27.0f }, f, Col::bone);
            const float w01 = GlyphArrangement::getStringWidth (f, "01 ");
            txt (c, "/", { 686.0f + w01, 33.0f, 20.0f, 27.0f }, f, Colour (0xff6b7779));
            txt (c, "RED ROOM", { 686.0f + w01 + GlyphArrangement::getStringWidth (f, "/ "), 33.0f, 200.0f, 27.0f }, f, Col::bone);
        }
        c.setColour (Col::lime.withAlpha (0.35f)); c.fillEllipse (906.0f, 40.0f, 7.0f, 7.0f);
        c.setColour (Col::lime); c.fillEllipse (907.0f, 41.0f, 5.0f, 5.0f);
        txt (c, "SESSION PLAYER MAP", { 917.0f, 36.0f, 100.0f, 14.0f }, mono (8.5f, 0.4f), Col::lime);
        c.setColour (Colour (0xff394447)); c.drawHorizontalLine (85, 20.0f, (float) W - 20.0f);

        auto panel = [&] (Rectangle<float> r)
        {
            c.setColour (Colours::black.withAlpha (0.27f)); c.fillRoundedRectangle (r.translated (0, 3), 5.0f);
            c.setGradientFill (ColourGradient (Col::plateA, r.getX(), r.getY(), Col::plateB, r.getX() + r.getWidth() * 0.65f, r.getY() + r.getHeight() * 0.65f, false));
            c.fillRoundedRectangle (r, 5.0f);
            c.setColour (Col::border); c.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
        };
        auto top = [&] (Rectangle<float> r, const char* num, const char* title, const char* sub, const String& right)
        {
            c.setColour (Colour (0xff2a3539)); c.drawHorizontalLine ((int) r.getY() + 39, r.getX() + 1.0f, r.getRight() - 1.0f);
            txt (c, num, { r.getX() + 14.0f, r.getY(), 20.0f, 39.0f }, mono (10.0f), Colour (0xff627578));
            c.setColour (Colour (0xff394649)); c.drawVerticalLine ((int) r.getX() + 36, r.getY() + 13.0f, r.getY() + 26.0f);
            txt (c, title, { r.getX() + 46.0f, r.getY(), 200.0f, 39.0f }, raj (15.0f, true, 2.0f), Col::bone);
            if (sub != nullptr)
                txt (c, sub, { r.getX() + 46.0f + GlyphArrangement::getStringWidth (raj (15.0f, true, 2.0f), title) + 12.0f, r.getY(), 260.0f, 39.0f },
                      mono (8.0f, 1.0f), Col::dim);
            txt (c, right, { r.getRight() - 274.0f, r.getY(), 260.0f, 39.0f }, mono (8.5f, 1.0f), Colour (0xff91a09f), Justification::centredRight);
        };

        panel ({ 20.0f, 101.0f, 802.0f, 400.0f });
        top ({ 20.0f, 101.0f, 802.0f, 400.0f }, "01", "THE KIT", "HEAVY SHELLS / LIVE BRASS", String::fromUTF8 ("CLICK A HEAD TO AUDITION  \xe2\x86\x97"));
        c.setColour (Colour (0xff111a1f)); c.fillRect (21.0f, 456.0f, 800.0f, 44.0f);
        txt (c, "STRIKE", { 33.0f, 456.0f, 50.0f, 44.0f }, mono (9.0f), Colour (0xff829394));

        panel ({ 838.0f, 101.0f, 320.0f, 194.0f });
        top ({ 838.0f, 101.0f, 320.0f, 194.0f }, "02", "FEEL", nullptr, "HUMAN BY DESIGN");
        txt (c, "IN THE POCKET", { 1058.0f, 230.0f, 104.0f, 14.0f }, raj (12.0f, true, 2.0f), Col::cyan, Justification::centred);
        txt (c, "DRIFT 0 = ON GRID", { 1058.0f, 246.0f, 104.0f, 12.0f }, mono (8.0f, 0.6f), Colour (0xff687e7e), Justification::centred);

        panel ({ 838.0f, 307.0f, 320.0f, 194.0f });
        top ({ 838.0f, 307.0f, 320.0f, 194.0f }, "03", "THE ROOM", nullptr, "STEREO BUS / IMPACT");

        panel ({ 20.0f, 514.0f, 1140.0f, 151.0f });
        c.setColour (Colour (0xff2b383b)); c.drawHorizontalLine (544, 21.0f, 1159.0f);
        txt (c, "MAKE IT YOURS", { 32.0f, 514.0f, 160.0f, 30.0f }, raj (11.5f, true, 2.0f), Col::bone);
        txt (c, "LEVEL / TUNE / DECAY / TONE", { 160.0f, 514.0f, 260.0f, 30.0f }, mono (8.0f, 1.0f), Colour (0xff718688));
        const float cw = 1138.0f / 6.0f;
        for (int gI = 0; gI < NumGroups; ++gI)
        {
            const float x = 21.0f + cw * (float) gI;
            if (gI > 0) { c.setColour (Colour (0xff303d41)); c.drawVerticalLine ((int) x, 545.0f, 664.0f); }
            txt (c, groupName[gI], { x + 12.0f, 550.0f, 120.0f, 18.0f }, raj (12.5f, true, 1.2f), groupAccent[gI]);
            txt (c, "0" + String (gI + 1), { x + cw - 42.0f, 550.0f, 30.0f, 18.0f }, mono (8.0f), Colour (0xff71898b), Justification::centredRight);
        }
        txt (c, String::fromUTF8 ("RK\xe2\x80\x94" "01  /  ROB\xe2\x80\x99S SAMPLES"), { 271.0f, 665.0f, 300.0f, 34.0f }, mono (9.0f, 1.0f), Colour (0xff718287));
        txt (c, String::fromUTF8 ("ROB KIT 1.2  \xc2\xb7  AU  \xc2\xb7  1180 \xc3\x97 700"), { 760.0f, 665.0f, 400.0f, 34.0f }, mono (8.0f, 1.0f), Colour (0xff617276), Justification::centredRight);
    }
    g.setOpacity (1.0f);
    g.drawImage (cache, getLocalBounds().toFloat());

    // dynamic: active mixer channel marker and footer status
    const float cw = 1138.0f / 6.0f, x = 21.0f + cw * (float) group;
    g.setGradientFill (ColourGradient (Col::lime.withAlpha (0.03f), x, 545.0f, Colours::transparentBlack, x, 640.0f, false));
    g.fillRect (x, 545.0f, cw, 119.0f);
    g.setColour (groupAccent[group]); g.fillRect (x + 12.0f, 545.0f, 44.0f, 2.0f);
    g.setColour (Col::lime); g.fillEllipse (20.0f, 680.0f, 4.0f, 4.0f);
    txt (g, status, { 30.0f, 665.0f, 236.0f, 34.0f }, mono (9.0f, 1.0f), Colour (0xffb4c7ad));
}

void Panel::tick()
{
    for (auto* c : controls) if (c->isShowing()) c->refreshEffective();
    kit.tick();
    meters.tick();
    auto& t = proc.telemetry();
    const auto& arts = proc.kit().articulations();
    int latest = -1;
    for (size_t a = 0; a < arts.size() && a < seenHits.size(); ++a)
    {
        const auto n = t.hits[a].load (std::memory_order_relaxed);
        if (n != seenHits[a]) { seenHits[a] = n; latest = (int) a; }
    }
    if (latest >= 0)
    {
        status = arts[(size_t) latest].name + "  \xc2\xb7  VEL " + String (roundToInt (t.lastVel[(size_t) latest].load() * 127.0f));
        statusAge = 0.0f;
    }
    else if (status != "READY TO HIT" && (statusAge += 1.0f) > 75.0f) status = "READY TO HIT";
    if (status != lastStatus) { lastStatus = status; repaint (20, 665, 250, 34); }
}
} // namespace rr

//==============================================================================
RobKitEditor::RobKitEditor (RobKitProcessor& p) : AudioProcessorEditor (p), proc (p), panel (p)
{
    addAndMakeVisible (panel);
    panel.onScale = [this] (float s)
    {
        proc.apvts.state.setProperty ("uiScale", s, nullptr);
        setSize (roundToInt (rr::Panel::W * s), roundToInt (rr::Panel::H * s));
    };
    const float s = jlimit (0.75f, 2.0f, (float) p.apvts.state.getProperty ("uiScale", 1.0f));
    setResizable (true, true);
    setResizeLimits (885, 525, 2360, 1400);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) rr::Panel::W / rr::Panel::H);
    setSize (roundToInt (rr::Panel::W * s), roundToInt (rr::Panel::H * s));
    setWantsKeyboardFocus (true);
    startTimerHz (30);
}

void RobKitEditor::resized()
{
    panel.setTransform (AffineTransform::scale ((float) getWidth() / (float) rr::Panel::W));
    panel.setBounds (0, 0, rr::Panel::W, rr::Panel::H);
}

bool RobKitEditor::keyPressed (const KeyPress& k)
{
    if (k.getModifiers().isCommandDown() && k.getKeyCode() == 'Z')
    {
        if (k.getModifiers().isShiftDown()) proc.undoManager().redo(); else proc.undoManager().undo();
        return true;
    }
    return false;
}
