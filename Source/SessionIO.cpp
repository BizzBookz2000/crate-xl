// Files: sessions, bank presets, packs, MIDI import/export, audio export, pad clipboard and undo data.
#include "Engine.h"
#include "FileUtils.h"


using juce::var;

// ============================================================================
// Serializer
// ============================================================================
struct Serializer
{
    // How samples are referenced: a function turning a sample into a var, and back.
    using SampleOut = std::function<var (const cr::SamplePtr&)>;
    using SampleIn  = std::function<cr::SamplePtr (const var&)>;

    Engine& engine;
    explicit Serializer (Engine& e) : engine (e) {}

    static var obj() { return var (new juce::DynamicObject()); }
    static void set (var& o, const juce::Identifier& k, const var& v) { o.getDynamicObject()->setProperty (k, v); }

    // ---- plugins / inserts --------------------------------------------------------------
    static var pluginToVar (juce::AudioPluginInstance& p)
    {
        var o = obj();
        juce::PluginDescription d;
        p.fillInPluginDescription (d);
        if (auto xml = d.createXml()) set (o, "desc", xml->toString (juce::XmlElement::TextFormat().singleLine()));
        juce::MemoryBlock mb;
        p.getStateInformation (mb);
        set (o, "state", mb.toBase64Encoding());
        return o;
    }

    std::unique_ptr<juce::AudioPluginInstance> varToPlugin (const var& v)
    {
        if (! v.isObject()) return nullptr;
        auto xml = juce::parseXML (v.getProperty ("desc", "").toString());
        if (xml == nullptr) return nullptr;
        juce::PluginDescription d;
        if (! d.loadFromXml (*xml)) return nullptr;
        juce::String err;
        auto inst = engine.plugins.create (d, engine.getSampleRate(), 512, err);
        if (! inst) { missingPlugins.addIfNotAlreadyThere (d.name); return nullptr; }
        juce::MemoryBlock mb;
        if (mb.fromBase64Encoding (v.getProperty ("state", "").toString()) && mb.getSize() > 0)
            inst->setStateInformation (mb.getData(), (int) mb.getSize());
        return inst;
    }

    static var slotToVar (const cr::InsertSlot& s)
    {
        var o = obj();
        set (o, "bypass", s.bypass);
        if (s.builtin)
        {
            set (o, "type", s.builtin->getType());
            juce::Array<var> ps;
            for (auto& p : s.builtin->params) ps.add (p.value);
            set (o, "params", ps);
        }
        else if (s.plugin) set (o, "plugin", pluginToVar (*s.plugin));
        return o;
    }

    void varToSlot (const var& v, cr::InsertSlot& s)
    {
        s.bypass = v.getProperty ("bypass", false);
        const auto type = v.getProperty ("type", "").toString();
        if (type.isNotEmpty())
        {
            s.builtin = createBuiltinEffect (type);
            if (s.builtin)
                if (auto* ps = v.getProperty ("params", var()).getArray())
                    for (int i = 0; i < ps->size(); ++i) s.builtin->setParam (i, (float) (*ps)[i]);
        }
        else if (v.hasProperty ("plugin")) s.plugin = varToPlugin (v.getProperty ("plugin", var()));
    }

    // ---- strips -----------------------------------------------------------------------------
    static var stripToVar (const cr::Strip& st)
    {
        var o = obj();
        set (o, "name", st.name);
        set (o, "volume", st.volume);
        set (o, "pan", st.pan);
        set (o, "mute", st.mute);
        set (o, "solo", st.solo);
        set (o, "output", st.output);
        juce::Array<var> sends, pre, ins;
        for (int a = 0; a < cr::numAux; ++a) { sends.add (st.send[a]); pre.add (st.sendPre[a]); }
        set (o, "sends", sends);
        set (o, "sendPre", pre);
        for (auto& s : st.inserts) ins.add (slotToVar (s));
        set (o, "inserts", ins);
        if (st.frozen) set (o, "frozen", true);
        if (! st.eq.isDefault() || st.eq.bypassEq || st.eq.bypassFilters || st.eq.bypassInserts)
        {
            const auto& q = st.eq;
            var e = obj();
            set (e, "trim", q.trimDb); set (e, "invert", q.invert); set (e, "width", q.width);
            set (e, "bypassInserts", q.bypassInserts); set (e, "bypassEq", q.bypassEq); set (e, "bypassFilters", q.bypassFilters);
            juce::Array<var> bands;
            for (auto& b : q.band)
            {
                var bv = obj();
                set (bv, "on", b.on); set (bv, "freq", b.freq); set (bv, "gain", b.gain); set (bv, "q", b.q); set (bv, "bell", b.bell);
                bands.add (bv);
            }
            set (e, "bands", bands);
            set (e, "hpfOn", q.hpfOn); set (e, "hpfFreq", q.hpfFreq); set (e, "hpfSlope", q.hpfSlope);
            set (e, "lpfOn", q.lpfOn); set (e, "lpfFreq", q.lpfFreq); set (e, "lpfSlope", q.lpfSlope);
            set (o, "eq", e);
        }
        return o;
    }

    void varToStrip (const var& v, cr::Strip& st, bool keepName = false)
    {
        if (! v.isObject()) return;
        if (! keepName) st.name = v.getProperty ("name", st.name).toString();
        st.volume = v.getProperty ("volume", 0.8f);
        st.pan = v.getProperty ("pan", 0.0f);
        st.mute = v.getProperty ("mute", false);
        st.solo = v.getProperty ("solo", false);
        st.output = v.getProperty ("output", 0);
        if (auto* a = v.getProperty ("sends", var()).getArray()) for (int i = 0; i < juce::jmin (cr::numAux, a->size()); ++i) st.send[i] = (*a)[i];
        if (auto* a = v.getProperty ("sendPre", var()).getArray()) for (int i = 0; i < juce::jmin (cr::numAux, a->size()); ++i) st.sendPre[i] = (*a)[i];
        st.eq = {};
        st.frozen = v.getProperty ("frozen", false);
        if (auto e = v.getProperty ("eq", var()); e.isObject())
        {
            auto& q = st.eq;
            q.trimDb = e.getProperty ("trim", 0.0f); q.invert = e.getProperty ("invert", false); q.width = e.getProperty ("width", 1.0f);
            q.bypassInserts = e.getProperty ("bypassInserts", false); q.bypassEq = e.getProperty ("bypassEq", false); q.bypassFilters = e.getProperty ("bypassFilters", false);
            if (auto* bands = e.getProperty ("bands", var()).getArray())
                for (int i = 0; i < juce::jmin (4, bands->size()); ++i)
                {
                    const auto& bv = (*bands)[i];
                    auto& b = q.band[i];
                    b.on = bv.getProperty ("on", b.on); b.freq = bv.getProperty ("freq", b.freq); b.gain = bv.getProperty ("gain", b.gain);
                    b.q = bv.getProperty ("q", b.q); b.bell = bv.getProperty ("bell", b.bell);
                }
            q.hpfOn = e.getProperty ("hpfOn", false); q.hpfFreq = e.getProperty ("hpfFreq", 80.0f); q.hpfSlope = e.getProperty ("hpfSlope", 1);
            q.lpfOn = e.getProperty ("lpfOn", false); q.lpfFreq = e.getProperty ("lpfFreq", 16000.0f); q.lpfSlope = e.getProperty ("lpfSlope", 1);
        }
        st.inserts.clear();
        if (auto* a = v.getProperty ("inserts", var()).getArray())
            for (auto& e : *a)
            {
                cr::InsertSlot s;
                varToSlot (e, s);
                if (! s.isEmpty()) st.inserts.push_back (std::move (s));
            }
    }

    // ---- layers & pads ----------------------------------------------------------------------
    static var layerToVar (const cr::Layer& l, const SampleOut& sampleOut)
    {
        var o = obj();
        set (o, "name", l.name);
        const auto& p = l.p;
        var pv = obj();
        set (pv, "trigger", (int) p.trigger); set (pv, "loop", (int) p.loop); set (pv, "crossfade", p.crossfade);
        set (pv, "polyphony", p.polyphony); set (pv, "legato", p.legato); set (pv, "glide", p.glide);
        set (pv, "tune", p.tune); set (pv, "fine", p.fine); set (pv, "gain", p.gainDb); set (pv, "pan", p.pan); set (pv, "saturate", p.saturate);
        set (pv, "reverse", p.reverse); set (pv, "liveStretch", p.liveStretch); set (pv, "diskStream", p.diskStream);
        set (pv, "sampleBpm", p.sampleBpm); set (pv, "sampleBeats", p.sampleBeats);
        set (pv, "envOn", p.envOn); set (pv, "envMode", (int) p.envMode);
        set (pv, "attack", p.attack); set (pv, "hold", p.hold); set (pv, "decay", p.decay); set (pv, "sustain", p.sustain); set (pv, "release", p.release);
        set (pv, "filter", (int) p.filter); set (pv, "cutoff", p.cutoff); set (pv, "resonance", p.resonance);
        set (pv, "lofi", (int) p.lofi); set (pv, "mute", p.mute);
        set (o, "params", pv);

        juce::Array<var> lfos;
        for (auto& lf : l.lfo)
        {
            var lv = obj();
            set (lv, "shape", (int) lf.shape); set (lv, "rate", lf.rate); set (lv, "sync", lf.sync); set (lv, "syncIndex", lf.syncIndex); set (lv, "retrigger", lf.retrigger);
            lfos.add (lv);
        }
        set (o, "lfo", lfos);
        var me = obj();
        set (me, "attack", l.modEnv.attack); set (me, "hold", l.modEnv.hold); set (me, "decay", l.modEnv.decay); set (me, "sustain", l.modEnv.sustain); set (me, "release", l.modEnv.release);
        set (o, "modEnv", me);
        var st = obj();
        set (st, "numSteps", l.step.numSteps); set (st, "syncIndex", l.step.syncIndex); set (st, "smooth", l.step.smooth); set (st, "retrigger", l.step.retrigger);
        juce::Array<var> steps;
        for (auto v : l.step.steps) steps.add (v);
        set (st, "steps", steps);
        set (o, "step", st);
        juce::Array<var> mods;
        for (auto& r : l.mods)
        {
            var rv = obj();
            set (rv, "source", (int) r.source); set (rv, "target", (int) r.target); set (rv, "amount", r.amount); set (rv, "bypass", r.bypass);
            mods.add (rv);
        }
        set (o, "mods", mods);
        juce::Array<var> fx;
        for (auto& s : l.fx) fx.add (slotToVar (s));
        set (o, "fx", fx);
        juce::Array<var> zones;
        for (auto& z : l.zones)
        {
            var zv = obj();
            set (zv, "sample", sampleOut (z.sample));
            set (zv, "rootKey", z.rootKey); set (zv, "lowKey", z.lowKey); set (zv, "highKey", z.highKey);
            set (zv, "lowVel", z.lowVel); set (zv, "highVel", z.highVel); set (zv, "pitchTrack", z.pitchTrack); set (zv, "mute", z.mute);
            set (zv, "start", z.start); set (zv, "end", z.end); set (zv, "loopStart", z.loopStart); set (zv, "loopEnd", z.loopEnd);
            set (zv, "tune", z.tune); set (zv, "gain", z.gainDb);
            juce::Array<var> sl;
            for (auto f : z.slices) sl.add (f);
            set (zv, "slices", sl);
            zones.add (zv);
        }
        set (o, "zones", zones);
        return o;
    }

    void varToLayer (const var& o, cr::Layer& l, const SampleIn& sampleIn)
    {
        l.name = o.getProperty ("name", "Layer").toString();
        const var pv = o.getProperty ("params", var());
        auto& p = l.p;
        p.trigger = (cr::TriggerMode) (int) pv.getProperty ("trigger", 0);
        p.loop = (cr::LoopMode) (int) pv.getProperty ("loop", 0);
        p.crossfade = pv.getProperty ("crossfade", 0.0f);
        p.polyphony = pv.getProperty ("polyphony", 1);
        p.legato = pv.getProperty ("legato", false);
        p.glide = pv.getProperty ("glide", 0.05f);
        p.tune = pv.getProperty ("tune", 0.0f); p.fine = pv.getProperty ("fine", 0.0f);
        p.gainDb = pv.getProperty ("gain", -6.0f); p.pan = pv.getProperty ("pan", 0.0f); p.saturate = pv.getProperty ("saturate", 0.0f);
        p.reverse = pv.getProperty ("reverse", false); p.liveStretch = pv.getProperty ("liveStretch", false); p.diskStream = pv.getProperty ("diskStream", false);
        p.sampleBpm = pv.getProperty ("sampleBpm", 120.0f); p.sampleBeats = pv.getProperty ("sampleBeats", 4);
        p.envOn = pv.getProperty ("envOn", true); p.envMode = (cr::EnvMode) (int) pv.getProperty ("envMode", 1);
        p.attack = pv.getProperty ("attack", 0.001f); p.hold = pv.getProperty ("hold", 0.0f); p.decay = pv.getProperty ("decay", 0.5f);
        p.sustain = pv.getProperty ("sustain", 1.0f); p.release = pv.getProperty ("release", 0.05f);
        p.filter = (cr::FilterType) (int) pv.getProperty ("filter", 0); p.cutoff = pv.getProperty ("cutoff", 20000.0f); p.resonance = pv.getProperty ("resonance", 0.707f);
        p.lofi = (cr::LofiMode) (int) pv.getProperty ("lofi", 0); p.mute = pv.getProperty ("mute", false);

        if (auto* a = o.getProperty ("lfo", var()).getArray())
            for (int i = 0; i < juce::jmin (2, a->size()); ++i)
            {
                const var& lv = (*a)[i];
                auto& lf = l.lfo[(size_t) i];
                lf.shape = (cr::LfoShape) (int) lv.getProperty ("shape", 0); lf.rate = lv.getProperty ("rate", 2.0f);
                lf.sync = lv.getProperty ("sync", false); lf.syncIndex = lv.getProperty ("syncIndex", 5); lf.retrigger = lv.getProperty ("retrigger", true);
            }
        const var me = o.getProperty ("modEnv", var());
        if (me.isObject())
        {
            l.modEnv.attack = me.getProperty ("attack", 0.005f); l.modEnv.hold = me.getProperty ("hold", 0.0f); l.modEnv.decay = me.getProperty ("decay", 0.3f);
            l.modEnv.sustain = me.getProperty ("sustain", 0.0f); l.modEnv.release = me.getProperty ("release", 0.2f);
        }
        const var st = o.getProperty ("step", var());
        if (st.isObject())
        {
            l.step.numSteps = st.getProperty ("numSteps", 16); l.step.syncIndex = st.getProperty ("syncIndex", 1);
            l.step.smooth = st.getProperty ("smooth", 0.0f); l.step.retrigger = st.getProperty ("retrigger", true);
            if (auto* a = st.getProperty ("steps", var()).getArray())
                for (int i = 0; i < juce::jmin (32, a->size()); ++i) l.step.steps[(size_t) i] = (*a)[i];
        }
        l.mods.clear();
        if (auto* a = o.getProperty ("mods", var()).getArray())
            for (auto& rv : *a)
            {
                cr::ModRoute r;
                r.source = (cr::ModSource) juce::jlimit (0, (int) cr::ModSource::count - 1, (int) rv.getProperty ("source", 1));
                r.target = (cr::ModTarget) juce::jlimit (0, (int) cr::ModTarget::count - 1, (int) rv.getProperty ("target", 1));
                r.amount = rv.getProperty ("amount", 0.5f); r.bypass = rv.getProperty ("bypass", false);
                l.mods.push_back (r);
            }
        if (auto* a = o.getProperty ("fx", var()).getArray())
            for (int i = 0; i < juce::jmin (cr::numLayerFx, a->size()); ++i) varToSlot ((*a)[i], l.fx[(size_t) i]);
        l.zones.clear();
        if (auto* a = o.getProperty ("zones", var()).getArray())
            for (auto& zv : *a)
            {
                cr::Zone z;
                z.sample = sampleIn (zv.getProperty ("sample", var()));
                z.rootKey = zv.getProperty ("rootKey", 60); z.lowKey = zv.getProperty ("lowKey", 0); z.highKey = zv.getProperty ("highKey", 127);
                z.lowVel = zv.getProperty ("lowVel", 1); z.highVel = zv.getProperty ("highVel", 127);
                z.pitchTrack = zv.getProperty ("pitchTrack", true); z.mute = zv.getProperty ("mute", false);
                z.start = zv.getProperty ("start", 0.0f); z.end = zv.getProperty ("end", 1.0f);
                z.loopStart = zv.getProperty ("loopStart", 0.0f); z.loopEnd = zv.getProperty ("loopEnd", 1.0f);
                z.tune = zv.getProperty ("tune", 0.0f); z.gainDb = zv.getProperty ("gain", 0.0f);
                if (auto* sl = zv.getProperty ("slices", var()).getArray()) for (auto& f : *sl) z.slices.push_back ((float) f);
                l.zones.push_back (std::move (z));
            }
    }

    static var padToVar (const cr::Pad& pad, const SampleOut& sampleOut)
    {
        var o = obj();
        set (o, "name", pad.name);
        set (o, "colour", pad.colour.toString());
        set (o, "padNote", pad.padNote);
        set (o, "keysMode", pad.keysMode);
        set (o, "choke", pad.choke);
        set (o, "link", pad.link);
        var mv = obj();
        set (mv, "inDevice", pad.midi.inDevice); set (mv, "inChannel", pad.midi.inChannel);
        set (mv, "outDevice", pad.midi.outDevice); set (mv, "outChannel", pad.midi.outChannel);
        set (o, "midi", mv);
        set (o, "channel", stripToVar (pad.channel));
        juce::Array<var> layers;
        for (auto& l : pad.layers) layers.add (layerToVar (l, sampleOut));
        set (o, "layers", layers);
        if (pad.instrument) set (o, "instrument", pluginToVar (*pad.instrument));
        return o;
    }

    void varToPad (const var& o, cr::Pad& pad, const SampleIn& sampleIn)
    {
        pad.name = o.getProperty ("name", "").toString();
        pad.colour = juce::Colour::fromString (o.getProperty ("colour", "0").toString());
        pad.padNote = o.getProperty ("padNote", 60);
        pad.keysMode = o.getProperty ("keysMode", false);
        pad.choke = o.getProperty ("choke", 0);
        pad.link = o.getProperty ("link", 0);
        const var mv = o.getProperty ("midi", var());
        pad.midi.inDevice = mv.getProperty ("inDevice", "").toString(); pad.midi.inChannel = mv.getProperty ("inChannel", 0);
        pad.midi.outDevice = mv.getProperty ("outDevice", "").toString(); pad.midi.outChannel = mv.getProperty ("outChannel", 0);
        varToStrip (o.getProperty ("channel", var()), pad.channel);
        pad.layers.clear();
        if (auto* a = o.getProperty ("layers", var()).getArray())
            for (auto& lv : *a)
            {
                cr::Layer l;
                varToLayer (lv, l, sampleIn);
                pad.layers.push_back (std::move (l));
            }
        pad.instrument.reset();
        if (o.hasProperty ("instrument")) pad.instrument = varToPlugin (o.getProperty ("instrument", var()));
    }

    // ---- patterns -------------------------------------------------------------------------------
    static var lanesToVar (const std::vector<cr::AutomationLane>& lanes)
    {
        juce::Array<var> arr;
        for (auto& l : lanes)
        {
            var lv = obj();
            set (lv, "param", l.param);
            juce::Array<var> pts;
            for (auto& p : l.points) { pts.add (p.tick); pts.add (p.value); }
            set (lv, "points", pts);
            arr.add (lv);
        }
        return arr;
    }

    static void varToLanes (const var& v, std::vector<cr::AutomationLane>& lanes)
    {
        lanes.clear();
        if (auto* a = v.getArray())
            for (auto& lv : *a)
            {
                cr::AutomationLane l;
                l.param = lv.getProperty ("param", "").toString();
                l.address = cr::ParamAddress::parse (l.param);
                if (auto* pts = lv.getProperty ("points", var()).getArray())
                    for (int i = 0; i + 1 < pts->size(); i += 2) l.points.push_back ({ (int) (*pts)[i], (float) (*pts)[i + 1] });
                l.sort();
                if (l.address.isValid()) lanes.push_back (std::move (l));
            }
    }

    static var patternToVar (const cr::Pattern& p)
    {
        var o = obj();
        set (o, "name", p.name);
        set (o, "length", p.length);
        juce::Array<var> notes;
        for (auto& n : p.notes) { notes.add (n.pad); notes.add (n.key); notes.add (n.start); notes.add (n.length); notes.add (n.velocity); }
        set (o, "notes", notes);
        set (o, "automation", lanesToVar (p.automation));
        return o;
    }

    static cr::Pattern varToPattern (const var& o)
    {
        cr::Pattern p;
        p.name = o.getProperty ("name", "Pattern").toString();
        p.length = juce::jmax (1, (int) o.getProperty ("length", cr::barTicks()));
        if (auto* a = o.getProperty ("notes", var()).getArray())
            for (int i = 0; i + 4 < a->size(); i += 5)
            {
                cr::Note n;
                n.pad = juce::jlimit (0, cr::numPads - 1, (int) (*a)[i]); n.key = (*a)[i + 1]; n.start = (*a)[i + 2];
                n.length = juce::jmax (1, (int) (*a)[i + 3]); n.velocity = (*a)[i + 4];
                p.notes.push_back (n);
            }
        varToLanes (o.getProperty ("automation", var()), p.automation);
        return p;
    }

    // ---- banks --------------------------------------------------------------------------------------
    static var bankToVar (const cr::Bank& b, bool withPatterns, const SampleOut& sampleOut)
    {
        var o = obj();
        set (o, "name", b.name);
        set (o, "colour", b.colour.toString());
        juce::Array<var> pads;
        for (int p = 0; p < cr::numPads; ++p)
        {
            const auto& pad = b.pads[(size_t) p];
            const bool isDefault = ! pad.hasSound() && pad.name.isEmpty() && pad.layers.empty() && pad.channel.inserts.empty()
                                   && pad.midi.outDevice.isEmpty() && pad.midi.inDevice.isEmpty() && pad.midi.inChannel == 0 && pad.midi.outChannel == 0;
            if (isDefault) continue;
            var pv = padToVar (pad, sampleOut);
            set (pv, "index", p);
            pads.add (pv);
        }
        set (o, "pads", pads);
        var mv = obj();
        set (mv, "inDevice", b.midi.inDevice); set (mv, "inChannel", b.midi.inChannel); set (mv, "multiChannel", b.midi.multiChannel);
        set (mv, "outDevice", b.midi.outDevice); set (mv, "outChannel", b.midi.outChannel); set (mv, "baseNote", b.midi.baseNote);
        set (o, "midi", mv);
        set (o, "strip", stripToVar (b.strip));
        set (o, "recordArm", b.recordArm);
        if (withPatterns)
        {
            juce::Array<var> pats;
            for (auto& p : b.patterns) pats.add (patternToVar (p));
            set (o, "patterns", pats);
            set (o, "workingPattern", b.workingPattern);
        }
        return o;
    }

    void varToBank (const var& o, cr::Bank& b, bool withPatterns, const SampleIn& sampleIn)
    {
        b.created = true;
        b.name = o.getProperty ("name", b.name).toString();
        b.colour = juce::Colour::fromString (o.getProperty ("colour", b.colour.toString()).toString());
        for (auto& pad : b.pads) { pad.layers.clear(); pad.instrument.reset(); pad.name.clear(); pad.channel.inserts.clear(); }
        if (auto* a = o.getProperty ("pads", var()).getArray())
            for (auto& pv : *a)
            {
                const int idx = pv.getProperty ("index", -1);
                if (juce::isPositiveAndBelow (idx, cr::numPads)) varToPad (pv, b.pads[(size_t) idx], sampleIn);
            }
        const var mv = o.getProperty ("midi", var());
        if (mv.isObject())
        {
            b.midi.inDevice = mv.getProperty ("inDevice", "").toString(); b.midi.inChannel = mv.getProperty ("inChannel", 0);
            b.midi.multiChannel = mv.getProperty ("multiChannel", false); b.midi.outDevice = mv.getProperty ("outDevice", "").toString();
            b.midi.outChannel = mv.getProperty ("outChannel", 1); b.midi.baseNote = mv.getProperty ("baseNote", 0);
        }
        varToStrip (o.getProperty ("strip", var()), b.strip);
        b.recordArm = o.getProperty ("recordArm", true);
        if (withPatterns && o.hasProperty ("patterns"))
        {
            b.patterns.clear();
            if (auto* a = o.getProperty ("patterns", var()).getArray()) for (auto& pv : *a) b.patterns.push_back (varToPattern (pv));
            if (b.patterns.empty()) { b.patterns.push_back ({}); b.patterns[0].name = "Pattern 1"; b.patterns[0].length = cr::barTicks(); }
            b.workingPattern = juce::jlimit (0, (int) b.patterns.size() - 1, (int) o.getProperty ("workingPattern", 0));
            b.playingPattern = b.workingPattern;
        }
    }

    // ---- sample references for files on disk -------------------------------------------------------------
    // Edited (in-memory) samples are written next to the document; others are referenced by path
    // (relative to the document folder when possible). copyAll copies every sample into the folder.
    SampleOut fileSampleOut (const juce::File& docFile, bool copyAll)
    {
        auto folder = docFile.getParentDirectory().getChildFile (docFile.getFileNameWithoutExtension() + " Samples");
        auto written = std::make_shared<std::map<const cr::SampleData*, juce::String>>();
        return [this, docFile, folder, copyAll, written] (const cr::SamplePtr& s) -> var
        {
            if (! s) return {};
            auto it = written->find (s.get());
            if (it != written->end()) return it->second;
            juce::File f = s->file;
            if (s->edited || copyAll || ! f.existsAsFile())
            {
                folder.createDirectory();
                auto target = folder.getChildFile (juce::File::createLegalFileName (s->name.isNotEmpty() ? s->name : "sample") + ".wav");
                if (s->edited || ! f.existsAsFile())
                {
                    target = folder.getNonexistentChildFile (juce::File::createLegalFileName (s->name.isNotEmpty() ? s->name : "sample"), ".wav");
                    const auto* buf = &s->buffer;
                    juce::AudioBuffer<float> tmp;
                    if (s->isStreamed()) { tmp.setSize (2, (int) s->numFrames); s->mapped->read (&tmp, 0, (int) s->numFrames, 0, true, true); buf = &tmp; }
                    if (auto w = createWavWriter (target, s->sampleRate, 2, 24)) w->writeFromAudioSampleBuffer (*buf, 0, buf->getNumSamples());
                }
                else if (! target.existsAsFile() || target.getSize() != f.getSize())
                {
                    target = target.existsAsFile() ? folder.getNonexistentChildFile (target.getFileNameWithoutExtension(), f.getFileExtension()) : target.withFileExtension (f.getFileExtension());
                    f.copyFileTo (target);
                }
                f = target;
            }
            juce::String ref = f.isAChildOf (docFile.getParentDirectory()) ? f.getRelativePathFrom (docFile.getParentDirectory()) : f.getFullPathName();
            (*written)[s.get()] = ref;
            return ref;
        };
    }

    SampleIn fileSampleIn (const juce::File& docFile)
    {
        auto cache = std::make_shared<std::map<juce::String, cr::SamplePtr>>();
        return [this, docFile, cache] (const var& v) -> cr::SamplePtr
        {
            const auto ref = v.toString();
            if (ref.isEmpty()) return nullptr;
            auto it = cache->find (ref);
            if (it != cache->end()) return it->second;
            juce::File f = juce::File::isAbsolutePath (ref) ? juce::File (ref) : docFile.getParentDirectory().getChildFile (ref);
            if (! f.existsAsFile())
            {
                // look for a file with the same name in the library
                const auto name = juce::File (ref.replaceCharacter ('\\', '/')).getFileName();
                for (const auto& entry : juce::RangedDirectoryIterator (Engine::getLibraryFolder(), true, name, juce::File::findFiles))
                {
                    f = entry.getFile();
                    break;
                }
            }
            auto s = engine.loadSample (f);
            if (! s) missingSamples.addIfNotAlreadyThere (ref);
            (*cache)[ref] = s;
            return s;
        };
    }

    juce::StringArray missingSamples, missingPlugins;
};

// ============================================================================
// Library folders
// ============================================================================
juce::File Engine::getLibraryFolder()
{
    auto f = appFolderIn (juce::File::getSpecialLocation (juce::File::userDocumentsDirectory));
    if (! f.isDirectory())
    {
        f.createDirectory();
        for (auto sub : { "Samples", "Sessions", "Banks", "Packs", "Recordings", "Exports" }) f.getChildFile (sub).createDirectory();
    }
    return f;
}

// ============================================================================
// Pad clipboard and pad undo data (samples are shared in memory)
// ============================================================================
namespace
{
struct MemorySamples
{
    std::vector<cr::SamplePtr> samples;
    Serializer::SampleOut out()
    {
        return [this] (const cr::SamplePtr& s) -> var
        {
            if (! s) return {};
            for (size_t i = 0; i < samples.size(); ++i) if (samples[i] == s) return "mem:" + juce::String ((int) i);
            samples.push_back (s);
            return "mem:" + juce::String ((int) samples.size() - 1);
        };
    }
    Serializer::SampleIn in()
    {
        return [this] (const var& v) -> cr::SamplePtr
        {
            const auto s = v.toString();
            if (! s.startsWith ("mem:")) return nullptr;
            const int i = s.substring (4).getIntValue();
            return juce::isPositiveAndBelow (i, (int) samples.size()) ? samples[(size_t) i] : nullptr;
        };
    }
};
} // namespace

void Engine::copyPad (int b, int p)
{
    auto clip = std::make_shared<PadClip>();
    MemorySamples ms;
    {
        const juce::ScopedLock sl (lock);
        clip->data = Serializer::padToVar (sess.banks[(size_t) b].pads[(size_t) p], ms.out());
    }
    clip->samples = ms.samples;
    padClipboard = clip;
}

void Engine::pastePad (int b, int p)
{
    if (! padClipboard) return;
    checkpointPad (b, p);
    createBank (b);
    MemorySamples ms;
    ms.samples = padClipboard->samples;
    Serializer ser (*this);
    cr::Pad fresh;
    ser.varToPad (padClipboard->data, fresh, ms.in());
    for (auto& s : fresh.channel.inserts) if (s.builtin) s.builtin->prepare (sampleRate, maxBlock);
    for (auto& l : fresh.layers) for (auto& f : l.fx) if (f.builtin) f.builtin->prepare (sampleRate, maxBlock);
    if (fresh.instrument) fresh.instrument->prepareToPlay (sampleRate, maxBlock);
    {
        const juce::ScopedLock sl (lock);
        auto& pad = sess.banks[(size_t) b].pads[(size_t) p];
        for (auto& v : voices) if (v.active && v.bank == b && v.pad == p) { v.active = false; v.sample.reset(); v.stretched.reset(); }
        for (auto& l : pad.layers) for (auto& z : l.zones) retire (z.sample);
        if (pad.instrument) retirePlugin (std::move (pad.instrument));
        const auto chName = pad.channel.name;
        pad.name = fresh.name;
        pad.colour = fresh.colour;
        pad.layers = std::move (fresh.layers);
        pad.instrument = std::move (fresh.instrument);
        pad.padNote = fresh.padNote; pad.keysMode = fresh.keysMode; pad.choke = fresh.choke; pad.link = fresh.link; pad.midi = fresh.midi;
        pad.channel.volume = fresh.channel.volume; pad.channel.pan = fresh.channel.pan;
        for (int a = 0; a < cr::numAux; ++a) { pad.channel.send[a] = fresh.channel.send[a]; pad.channel.sendPre[a] = fresh.channel.sendPre[a]; }
        pad.channel.inserts = std::move (fresh.channel.inserts);
        pad.channel.eq = fresh.channel.eq;
        pad.channel.name = chName;
    }
    notifyStructureChanged();
    sendChangeMessage();
}

Engine::UndoEntry Engine::makePadEntry (int b, int p)
{
    UndoEntry e;
    e.isPad = true;
    e.bank = b;
    e.pad = p;
    MemorySamples ms;
    {
        const juce::ScopedLock sl (lock);
        e.padData = Serializer::padToVar (sess.banks[(size_t) b].pads[(size_t) p], ms.out());
    }
    e.samples = ms.samples;
    return e;
}

void Engine::restorePadEntry (const UndoEntry& e)
{
    auto saved = padClipboard;
    auto clip = std::make_shared<PadClip>();
    clip->data = e.padData;
    clip->samples = e.samples;
    padClipboard = clip;
    // pastePad would push another undo entry: paste without checkpointing
    MemorySamples ms;
    ms.samples = e.samples;
    Serializer ser (*this);
    cr::Pad fresh;
    ser.varToPad (e.padData, fresh, ms.in());
    for (auto& s : fresh.channel.inserts) if (s.builtin) s.builtin->prepare (sampleRate, maxBlock);
    for (auto& l : fresh.layers) for (auto& f : l.fx) if (f.builtin) f.builtin->prepare (sampleRate, maxBlock);
    if (fresh.instrument) fresh.instrument->prepareToPlay (sampleRate, maxBlock);
    {
        const juce::ScopedLock sl (lock);
        auto& pad = sess.banks[(size_t) e.bank].pads[(size_t) e.pad];
        for (auto& v : voices) if (v.active && v.bank == e.bank && v.pad == e.pad) { v.active = false; v.sample.reset(); v.stretched.reset(); }
        for (auto& l : pad.layers) for (auto& z : l.zones) retire (z.sample);
        if (pad.instrument) retirePlugin (std::move (pad.instrument));
        pad.name = fresh.name;
        pad.colour = fresh.colour;
        pad.layers = std::move (fresh.layers);
        pad.instrument = std::move (fresh.instrument);
        pad.padNote = fresh.padNote; pad.keysMode = fresh.keysMode; pad.choke = fresh.choke; pad.link = fresh.link; pad.midi = fresh.midi;
        pad.channel.volume = fresh.channel.volume; pad.channel.pan = fresh.channel.pan;
        pad.channel.inserts = std::move (fresh.channel.inserts);
    }
    padClipboard = saved;
    notifyStructureChanged();
}

// ============================================================================
// Sessions
// ============================================================================
bool Engine::saveSession (const juce::File& file, juce::String& error, bool copySamples)
{
    Serializer ser (*this);
    auto sampleOut = ser.fileSampleOut (file, copySamples);
    var root = Serializer::obj();
    {
        const juce::ScopedLock sl (lock);
        Serializer::set (root, "app", "Crate XL");
        Serializer::set (root, "version", 1);
        Serializer::set (root, "appVersion", JUCE_APPLICATION_VERSION_STRING);
        Serializer::set (root, "bpm", sess.bpm);
        Serializer::set (root, "beatsPerBar", sess.beatsPerBar);
        Serializer::set (root, "quantize", sess.quantize);
        Serializer::set (root, "swing", sess.swing);
        Serializer::set (root, "preRollBars", sess.preRollBars);
        Serializer::set (root, "metronome", sess.metronome);
        Serializer::set (root, "metronomeVolume", sess.metronomeVolume);
        Serializer::set (root, "metronomeOnlyWhenRecording", sess.metronomeOnlyWhenRecording);
        Serializer::set (root, "loopOn", sess.loopOn);
        Serializer::set (root, "loopStart", sess.loopStart);
        Serializer::set (root, "loopEnd", sess.loopEnd);
        Serializer::set (root, "sceneSync", sess.sceneSyncTicks);
        Serializer::set (root, "globalTune", sess.globalTuneCents);
        Serializer::set (root, "auxCount", sess.auxCount);

        juce::Array<var> banks;
        for (int b = 0; b < cr::numBanks; ++b)
        {
            const auto& bank = sess.banks[(size_t) b];
            if (! bank.created) continue;
            var bv = Serializer::bankToVar (bank, true, sampleOut);
            Serializer::set (bv, "index", b);
            Serializer::set (bv, "source", (int) bank.source);
            Serializer::set (bv, "playingPattern", bank.playingPattern);
            banks.add (bv);
        }
        Serializer::set (root, "banks", banks);

        juce::Array<var> tracks;
        for (int t = 0; t < cr::maxAudioTracks; ++t)
        {
            const auto& tr = sess.audioTracks[(size_t) t];
            if (! tr.created) continue;
            var tv = Serializer::obj();
            Serializer::set (tv, "index", t);
            Serializer::set (tv, "name", tr.name);
            Serializer::set (tv, "colour", tr.colour.toString());
            Serializer::set (tv, "strip", Serializer::stripToVar (tr.strip));
            if (tr.frozenFrom >= 0) Serializer::set (tv, "frozenFrom", tr.frozenFrom);
            Serializer::set (tv, "inputSource", tr.inputSource);
            Serializer::set (tv, "inputPair", tr.inputPair);
            Serializer::set (tv, "inputMono", tr.inputMono);
            Serializer::set (tv, "inputGain", tr.inputGainDb);
            Serializer::set (tv, "gate", tr.gate);
            Serializer::set (tv, "gateThreshold", tr.gateThresholdDb);
            juce::Array<var> clips;
            for (auto& c : tr.clips)
            {
                var cv = Serializer::obj();
                Serializer::set (cv, "sample", sampleOut (c.sample));
                Serializer::set (cv, "start", c.start);
                Serializer::set (cv, "offset", (juce::int64) c.offset);
                Serializer::set (cv, "length", (juce::int64) c.length);
                Serializer::set (cv, "gain", c.gainDb);
                clips.add (cv);
            }
            Serializer::set (tv, "clips", clips);
            tracks.add (tv);
        }
        Serializer::set (root, "audioTracks", tracks);

        juce::Array<var> aux;
        for (auto& a : sess.aux) aux.add (Serializer::stripToVar (a));
        Serializer::set (root, "aux", aux);
        Serializer::set (root, "master", Serializer::stripToVar (sess.master));

        juce::Array<var> song;
        for (auto& c : sess.song) { song.add (c.bank); song.add (c.pattern); song.add (c.start); song.add (c.length); song.add (c.offset); }
        Serializer::set (root, "song", song);
        Serializer::set (root, "songAutomation", Serializer::lanesToVar (sess.songAutomation));

        juce::Array<var> scenes;
        for (auto& s : sess.scenes)
        {
            var sv = Serializer::obj();
            Serializer::set (sv, "name", s.name);
            juce::Array<var> slots;
            for (int b = 0; b < cr::numBanks; ++b) if (s.slots[(size_t) b] != -1) { slots.add (b); slots.add (s.slots[(size_t) b]); }
            Serializer::set (sv, "slots", slots);
            scenes.add (sv);
        }
        Serializer::set (root, "scenes", scenes);

        juce::Array<var> macros;
        for (auto& m : sess.macros)
        {
            var mv = Serializer::obj();
            Serializer::set (mv, "name", m.name);
            Serializer::set (mv, "value", m.value);
            juce::Array<var> binds;
            for (auto& bd : m.bindings) { var bv = Serializer::obj(); Serializer::set (bv, "param", bd.param); Serializer::set (bv, "min", bd.min); Serializer::set (bv, "max", bd.max); binds.add (bv); }
            Serializer::set (mv, "bindings", binds);
            macros.add (mv);
        }
        Serializer::set (root, "macros", macros);

        juce::Array<var> maps;
        for (auto& m : sess.midiMappings) { var mv = Serializer::obj(); Serializer::set (mv, "channel", m.channel); Serializer::set (mv, "cc", m.controller); Serializer::set (mv, "param", m.param); maps.add (mv); }
        Serializer::set (root, "midiMappings", maps);
    }

    file.getParentDirectory().createDirectory();
    auto tmp = file.getSiblingFile (file.getFileName() + ".tmp");
    if (! tmp.replaceWithText (juce::JSON::toString (root, false)))
    {
        error = "Could not write " + file.getFullPathName();
        return false;
    }
    if (! tmp.moveFileTo (file)) { error = "Could not replace " + file.getFullPathName(); return false; }
    if (! file.getFileName().contains ("(Auto-saved)")) sess.file = file;
    return true;
}

bool Engine::loadSession (const juce::File& file, juce::String& error)
{
    var root;
    const auto res = juce::JSON::parse (file.loadFileAsString(), root);
    if (res.failed() || ! root.isObject()) { error = "Not a valid session file: " + res.getErrorMessage(); return false; }

    stop();
    resetSessionToDefaults();
    Serializer ser (*this);
    auto sampleIn = ser.fileSampleIn (file);

    // banks (plugins and samples are created before taking the lock)
    std::vector<std::pair<int, cr::Bank*>> dummy;
    juce::ignoreUnused (dummy);
    {
        const juce::ScopedLock sl (lock);
        sess.bpm = root.getProperty ("bpm", 120.0);
        sess.beatsPerBar = juce::jlimit (1, 16, (int) root.getProperty ("beatsPerBar", 4));
        cr::beatsPerBarRef() = sess.beatsPerBar;
        sess.quantize = root.getProperty ("quantize", 2);
        sess.swing = root.getProperty ("swing", 0.0f);
        sess.preRollBars = root.getProperty ("preRollBars", 1);
        sess.metronome = root.getProperty ("metronome", false);
        sess.metronomeVolume = root.getProperty ("metronomeVolume", 0.5f);
        sess.metronomeOnlyWhenRecording = root.getProperty ("metronomeOnlyWhenRecording", false);
        sess.loopOn = root.getProperty ("loopOn", false);
        sess.loopStart = root.getProperty ("loopStart", 0);
        sess.loopEnd = root.getProperty ("loopEnd", cr::barTicks());
        sess.sceneSyncTicks = root.getProperty ("sceneSync", cr::barTicks());
        sess.globalTuneCents = root.getProperty ("globalTune", 0.0f);
        sess.auxCount = juce::jlimit (1, cr::numAux, (int) root.getProperty ("auxCount", 2));

        if (auto* a = root.getProperty ("banks", var()).getArray())
            for (auto& bv : *a)
            {
                const int b = bv.getProperty ("index", -1);
                if (! juce::isPositiveAndBelow (b, cr::numBanks)) continue;
                auto& bank = sess.banks[(size_t) b];
                ser.varToBank (bv, bank, true, sampleIn);
                bank.source = (cr::TrackSource) (int) bv.getProperty ("source", 0);
                bank.playingPattern = juce::jlimit (0, (int) bank.patterns.size() - 1, (int) bv.getProperty ("playingPattern", 0));
            }

        if (auto* a = root.getProperty ("audioTracks", var()).getArray())
            for (auto& tv : *a)
            {
                const int t = tv.getProperty ("index", -1);
                if (! juce::isPositiveAndBelow (t, cr::maxAudioTracks)) continue;
                auto& tr = sess.audioTracks[(size_t) t];
                tr.created = true;
                tr.name = tv.getProperty ("name", tr.name).toString();
                tr.colour = juce::Colour::fromString (tv.getProperty ("colour", tr.colour.toString()).toString());
                ser.varToStrip (tv.getProperty ("strip", var()), tr.strip);
                tr.frozenFrom = tv.getProperty ("frozenFrom", -1);
                tr.inputSource = tv.getProperty ("inputSource", 0);
                tr.inputPair = tv.getProperty ("inputPair", 0);
                tr.inputMono = tv.getProperty ("inputMono", false);
                tr.inputGainDb = tv.getProperty ("inputGain", 0.0f);
                tr.gate = tv.getProperty ("gate", false);
                tr.gateThresholdDb = tv.getProperty ("gateThreshold", -50.0f);
                if (auto* ca = tv.getProperty ("clips", var()).getArray())
                    for (auto& cv : *ca)
                    {
                        cr::AudioClip c;
                        c.sample = sampleIn (cv.getProperty ("sample", var()));
                        if (! c.sample || c.sample->isStreamed()) continue;
                        c.start = cv.getProperty ("start", 0);
                        c.offset = (juce::int64) cv.getProperty ("offset", 0);
                        c.length = (juce::int64) cv.getProperty ("length", (juce::int64) c.sample->numFrames);
                        c.gainDb = cv.getProperty ("gain", 0.0f);
                        tr.clips.push_back (c);
                    }
            }

        if (auto* a = root.getProperty ("aux", var()).getArray())
            for (int i = 0; i < juce::jmin (cr::numAux, a->size()); ++i) ser.varToStrip ((*a)[i], sess.aux[(size_t) i]);
        ser.varToStrip (root.getProperty ("master", var()), sess.master);

        if (auto* a = root.getProperty ("song", var()).getArray())
            for (int i = 0; i + 3 < a->size(); i += 5)
            {
                cr::SongClip c;
                c.bank = (*a)[i]; c.pattern = (*a)[i + 1]; c.start = (*a)[i + 2]; c.length = (*a)[i + 3];
                c.offset = i + 4 < a->size() ? (int) (*a)[i + 4] : 0;
                if (juce::isPositiveAndBelow (c.bank, cr::numBanks) && sess.banks[(size_t) c.bank].created) sess.song.push_back (c);
            }
        Serializer::varToLanes (root.getProperty ("songAutomation", var()), sess.songAutomation);

        if (auto* a = root.getProperty ("scenes", var()).getArray())
            for (auto& sv : *a)
            {
                cr::Scene s;
                s.name = sv.getProperty ("name", "Scene").toString();
                if (auto* slots = sv.getProperty ("slots", var()).getArray())
                    for (int i = 0; i + 1 < slots->size(); i += 2)
                    {
                        const int b = (*slots)[i];
                        if (juce::isPositiveAndBelow (b, cr::numBanks)) s.slots[(size_t) b] = (*slots)[i + 1];
                    }
                sess.scenes.push_back (s);
            }

        if (auto* a = root.getProperty ("macros", var()).getArray())
            for (int i = 0; i < juce::jmin (cr::numMacros, a->size()); ++i)
            {
                auto& m = sess.macros[(size_t) i];
                const var& mv = (*a)[i];
                m.name = mv.getProperty ("name", m.name).toString();
                m.value = mv.getProperty ("value", 0.0f);
                m.bindings.clear();
                if (auto* bs = mv.getProperty ("bindings", var()).getArray())
                    for (auto& bv : *bs)
                    {
                        cr::MacroBinding bd;
                        bd.param = bv.getProperty ("param", "").toString();
                        bd.address = cr::ParamAddress::parse (bd.param);
                        bd.min = bv.getProperty ("min", 0.0f);
                        bd.max = bv.getProperty ("max", 1.0f);
                        if (bd.address.isValid()) m.bindings.push_back (bd);
                    }
            }

        if (auto* a = root.getProperty ("midiMappings", var()).getArray())
            for (auto& mv : *a)
            {
                cr::MidiMapping m;
                m.channel = mv.getProperty ("channel", 1);
                m.controller = mv.getProperty ("cc", 0);
                m.param = mv.getProperty ("param", "").toString();
                m.address = cr::ParamAddress::parse (m.param);
                if (m.address.isValid()) sess.midiMappings.push_back (m);
            }
        sess.file = file;
    }
    prepareAllProcessors();
    rebuildMidiTables();
    requestStretches();

    juce::StringArray problems;
    if (! ser.missingSamples.isEmpty()) problems.add ("Missing samples: " + ser.missingSamples.joinIntoString (", "));
    if (! ser.missingPlugins.isEmpty()) problems.add ("Missing plugins: " + ser.missingPlugins.joinIntoString (", "));
    error = problems.joinIntoString ("\n");
    sendChangeMessage();
    return true;
}

// ============================================================================
// Bank presets (.crbank + "<name> Samples" folder)
// ============================================================================
bool Engine::saveBankPreset (int b, const juce::File& file, juce::String& error, bool savePatterns)
{
    Serializer ser (*this);
    auto sampleOut = ser.fileSampleOut (file, true);
    var root;
    {
        const juce::ScopedLock sl (lock);
        root = Serializer::bankToVar (sess.banks[(size_t) b], savePatterns, sampleOut);
    }
    Serializer::set (root, "app", "Crate XL");
    Serializer::set (root, "type", "bank");
    file.getParentDirectory().createDirectory();
    if (! file.replaceWithText (juce::JSON::toString (root, false))) { error = "Could not write " + file.getFullPathName(); return false; }
    return true;
}

bool Engine::loadBankPreset (int b, const juce::File& file, juce::String& error, bool loadPatterns)
{
    var root;
    const auto res = juce::JSON::parse (file.loadFileAsString(), root);
    if (res.failed() || ! root.isObject()) { error = "Not a valid bank file."; return false; }
    for (int p = 0; p < cr::numPads; ++p) if (sess.banks[(size_t) b].pads[(size_t) p].hasSound()) { checkpointPad (b, p); }
    Serializer ser (*this);
    auto sampleIn = ser.fileSampleIn (file);
    {
        const juce::ScopedLock sl (lock);
        for (auto& v : voices) if (v.active && v.bank == b) { v.active = false; v.sample.reset(); v.stretched.reset(); }
        auto& bank = sess.banks[(size_t) b];
        for (auto& pad : bank.pads)
        {
            for (auto& l : pad.layers) for (auto& z : l.zones) retire (z.sample);
            if (pad.instrument) retirePlugin (std::move (pad.instrument));
            for (auto& s : pad.channel.inserts) if (s.plugin) retirePlugin (std::move (s.plugin));
            for (auto& l : pad.layers) for (auto& f : l.fx) if (f.plugin) retirePlugin (std::move (f.plugin));
        }
        for (auto& s : bank.strip.inserts) if (s.plugin) retirePlugin (std::move (s.plugin));
        ser.varToBank (root, bank, loadPatterns, sampleIn);
        if (bank.name.isEmpty()) bank.name = file.getFileNameWithoutExtension();
        bank.strip.name = bank.name;
    }
    prepareAllProcessors();
    if (! ser.missingSamples.isEmpty()) error = "Missing samples: " + ser.missingSamples.joinIntoString (", ");
    sendChangeMessage();
    return true;
}

static juce::File bankClipboardFile()
{
    return juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("CrateXL_BankClipboard").getChildFile ("Bank.crbank");
}

bool Engine::copyBank (int b, juce::String& error)
{
    if (! juce::isPositiveAndBelow (b, cr::numBanks) || ! sess.banks[(size_t) b].created) { error = "That bank is empty."; return false; }
    auto f = bankClipboardFile();
    f.getParentDirectory().deleteRecursively();
    return saveBankPreset (b, f, error, true);
}

bool Engine::hasBankClipboard() const { return bankClipboardFile().existsAsFile(); }

bool Engine::pasteBank (int b, juce::String& error)
{
    if (! hasBankClipboard()) { error = "Copy a bank first."; return false; }
    checkpoint();
    createBank (b);
    const auto keepName = sess.banks[(size_t) b].name;
    if (! loadBankPreset (b, bankClipboardFile(), error, true)) return false;
    juce::ignoreUnused (keepName);
    sess.banks[(size_t) b].name << " copy";
    sess.banks[(size_t) b].strip.name = sess.banks[(size_t) b].name;
    sendChangeMessage();
    return true;
}

int Engine::duplicateBank (int b, juce::String& error)
{
    const int f = firstFreeBank();
    if (f < 0) { error = "All 128 banks are in use."; return -1; }
    if (! copyBank (b, error) || ! pasteBank (f, error)) return -1;
    auto& nb = sess.banks[(size_t) f];
    nb.colour = sess.banks[(size_t) b].colour;
    return f;
}

int Engine::firstFreeBank() const
{
    for (int b = 0; b < cr::numBanks; ++b) if (! sess.banks[(size_t) b].created) return b;
    return -1;
}

// ============================================================================
// Packs: a zip of bank presets and their samples
// ============================================================================
bool Engine::exportPack (const juce::File& file, const juce::String& packName, const juce::String& author, juce::String& error)
{
    auto temp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("CratePack_" + juce::String (juce::Random::getSystemRandom().nextInt()));
    temp.createDirectory();
    auto banksDir = temp.getChildFile ("Banks");
    banksDir.createDirectory();
    for (int b = 0; b < cr::numBanks; ++b)
    {
        const auto& bank = sess.banks[(size_t) b];
        if (! bank.created) continue;
        bool any = false;
        for (auto& p : bank.pads) if (p.hasSound()) { any = true; break; }
        if (! any) continue;
        juce::String err;
        if (! saveBankPreset (b, banksDir.getChildFile (juce::File::createLegalFileName (bank.name) + ".crbank"), err, true)) { error = err; temp.deleteRecursively(); return false; }
    }
    var info = Serializer::obj();
    Serializer::set (info, "name", packName);
    Serializer::set (info, "author", author);
    Serializer::set (info, "created", juce::Time::getCurrentTime().toISO8601 (true));
    temp.getChildFile ("pack.json").replaceWithText (juce::JSON::toString (info));

    juce::ZipFile::Builder zb;
    for (const auto& entry : juce::RangedDirectoryIterator (temp, true, "*", juce::File::findFiles))
        zb.addFile (entry.getFile(), 6, entry.getFile().getRelativePathFrom (temp).replaceCharacter ('\\', '/'));
    file.deleteFile();
    auto out = file.createOutputStream();
    bool ok = out != nullptr && zb.writeToStream (*out, nullptr);
    out.reset();
    temp.deleteRecursively();
    if (! ok) error = "Could not write the pack file.";
    return ok;
}

bool Engine::installPack (const juce::File& file, juce::String& error)
{
    juce::ZipFile zip (file);
    if (zip.getNumEntries() == 0) { error = "This is not a pack file."; return false; }
    auto dest = getPacksFolder().getChildFile (file.getFileNameWithoutExtension());
    dest.createDirectory();
    const auto r = zip.uncompressTo (dest, true);
    if (r.failed()) { error = r.getErrorMessage(); return false; }
    return true;
}

// ============================================================================
// MIDI files
// ============================================================================
bool Engine::importMidi (const juce::File& file, int b, juce::String& error, bool gm)
{
    juce::FileInputStream in (file);
    juce::MidiFile mf;
    if (! in.openedOk() || ! mf.readFrom (in)) { error = "Could not read the MIDI file."; return false; }
    const int tpq = mf.getTimeFormat() > 0 ? mf.getTimeFormat() : 96;
    // GM drum notes -> pads 1..16
    static const int gmMap[] = { 36, 38, 42, 46, 49, 51, 45, 48, 50, 37, 39, 44, 41, 43, 47, 56 };

    // Type 1: one source per track that has notes. Type 0 (everything on one track):
    // split by MIDI channel instead, so multi-instrument files still land on separate pads.
    std::vector<int> noteTracks;
    std::vector<int> sourceChannel;   // 0 = all channels
    for (int t = 0; t < mf.getNumTracks(); ++t)
    {
        auto* seq = mf.getTrack (t);
        for (int i = 0; i < seq->getNumEvents(); ++i) if (seq->getEventPointer (i)->message.isNoteOn()) { noteTracks.push_back (t); sourceChannel.push_back (0); break; }
    }
    if (noteTracks.empty()) { error = "The MIDI file has no notes."; return false; }
    if (noteTracks.size() == 1 && ! gm)
    {
        std::array<bool, 17> used {};
        auto* seq = mf.getTrack (noteTracks[0]);
        for (int i = 0; i < seq->getNumEvents(); ++i) if (seq->getEventPointer (i)->message.isNoteOn()) used[(size_t) seq->getEventPointer (i)->message.getChannel()] = true;
        if (std::count (used.begin(), used.end(), true) > 1)
        {
            const int t0 = noteTracks[0];
            noteTracks.clear(); sourceChannel.clear();
            for (int ch = 1; ch <= 16; ++ch) if (used[(size_t) ch]) { noteTracks.push_back (t0); sourceChannel.push_back (ch); }
        }
    }
    // tempo and meter: the file's first tempo and time signature become the session's
    {
        juce::MidiMessageSequence meta;
        mf.findAllTempoEvents (meta);
        if (meta.getNumEvents() > 0)
        {
            const double spq = meta.getEventPointer (0)->message.getTempoSecondsPerQuarterNote();
            if (spq > 0.0) setBpm (juce::jlimit (20.0, 300.0, 60.0 / spq));
        }
    }

    checkpoint();
    createBank (b);
    cr::Pattern pat;
    pat.name = file.getFileNameWithoutExtension();
    int maxTick = 0;
    for (size_t ti = 0; ti < noteTracks.size(); ++ti)
    {
        juce::MidiMessageSequence seq (*mf.getTrack (noteTracks[ti]));
        seq.updateMatchedPairs();
        for (int i = 0; i < seq.getNumEvents(); ++i)
        {
            auto* e = seq.getEventPointer (i);
            if (! e->message.isNoteOn()) continue;
            if (sourceChannel[ti] > 0 && e->message.getChannel() != sourceChannel[ti]) continue;
            const double startQ = e->message.getTimeStamp() / tpq;
            const double endQ = e->noteOffObject != nullptr ? e->noteOffObject->message.getTimeStamp() / tpq : startQ + 0.25;
            cr::Note n;
            n.start = (int) std::round (startQ * cr::ppq);
            n.length = juce::jmax (1, (int) std::round ((endQ - startQ) * cr::ppq));
            n.velocity = e->message.getFloatVelocity();
            const int note = e->message.getNoteNumber();
            if (noteTracks.size() > 1) { n.pad = juce::jmin (cr::numPads - 1, (int) ti); n.key = note; }
            else if (gm)
            {
                int pad = -1;
                for (int k = 0; k < 16; ++k) if (gmMap[k] == note) pad = k;
                if (pad < 0) continue;
                n.pad = pad;
            }
            else n.pad = juce::jlimit (0, cr::numPads - 1, note);
            pat.notes.push_back (n);
            maxTick = juce::jmax (maxTick, n.start + n.length);
        }
    }
    pat.length = juce::jmax (cr::barTicks(), (int) std::ceil ((double) maxTick / cr::barTicks()) * cr::barTicks());
    {
        const juce::ScopedLock sl (lock);
        auto& bank = sess.banks[(size_t) b];
        bank.patterns.push_back (std::move (pat));
        bank.workingPattern = (int) bank.patterns.size() - 1;
        if (noteTracks.size() > 1)
            for (size_t ti = 0; ti < noteTracks.size() && ti < (size_t) cr::numPads; ++ti) bank.pads[ti].keysMode = true;
    }
    sendChangeMessage();
    return true;
}

bool Engine::exportPatternMidi (int b, int p, const juce::File& file, juce::String& error)
{
    juce::MidiMessageSequence seq;
    {
        const juce::ScopedLock sl (lock);
        const auto& bank = sess.banks[(size_t) b];
        if (! juce::isPositiveAndBelow (p, (int) bank.patterns.size())) { error = "No such pattern."; return false; }
        for (auto& n : bank.patterns[(size_t) p].notes)
        {
            const int note = n.key >= 0 ? n.key : juce::jlimit (0, 127, n.pad);
            seq.addEvent (juce::MidiMessage::noteOn (1, note, n.velocity), n.start);
            seq.addEvent (juce::MidiMessage::noteOff (1, note), n.start + n.length);
        }
    }
    seq.addEvent (juce::MidiMessage::tempoMetaEvent ((int) (60000000.0 / sess.bpm)), 0);
    seq.sort();
    seq.updateMatchedPairs();
    juce::MidiFile mf;
    mf.setTicksPerQuarterNote (cr::ppq);
    mf.addTrack (seq);
    file.deleteFile();
    juce::FileOutputStream out (file);
    if (! out.openedOk() || ! mf.writeTo (out)) { error = "Could not write the MIDI file."; return false; }
    return true;
}

bool Engine::importAudioToTracks (const juce::StringArray& files, juce::String& error)
{
    checkpoint();
    for (auto& path : files)
    {
        auto s = loadSample (juce::File (path), false);
        if (! s) { error << "Could not load " << juce::File (path).getFileName() << "\n"; continue; }
        const int t = createAudioTrack();
        if (t < 0) { error << "No free audio tracks.\n"; break; }
        const juce::ScopedLock sl (lock);
        auto& tr = sess.audioTracks[(size_t) t];
        tr.name = s->name;
        tr.strip.name = s->name;
        cr::AudioClip c;
        c.sample = s;
        c.start = (int) position;
        c.length = s->numFrames;
        tr.clips.push_back (c);
    }
    sendChangeMessage();
    return error.isEmpty();
}

// ============================================================================
// Audio export (offline render)
// ============================================================================
bool Engine::exportAudio (const juce::File& file, const ExportOptions& options, juce::String& error, std::function<bool (float)> progress)
{
    const int startTick = juce::jmax (0, options.startTick);
    const int endTick = options.endTick > startTick ? options.endTick : juce::jmax (startTick + cr::barTicks(), getSongLengthTicks());
    const double deviceRate = sampleRate;
    const int deviceBlock = maxBlock;
    const double rate = options.sampleRate > 0 ? options.sampleRate : deviceRate;

    std::vector<std::pair<int, juce::String>> jobs;   // solo key, file suffix
    if (! options.stems) jobs.push_back ({ -1, {} });
    else
    {
        auto wanted = [&] (int key) { return options.trackKeys.empty() || std::find (options.trackKeys.begin(), options.trackKeys.end(), key) != options.trackKeys.end(); };
        for (int b = 0; b < cr::numBanks; ++b) if (sess.banks[(size_t) b].created && wanted (b)) jobs.push_back ({ b, sess.banks[(size_t) b].name });
        for (int t = 0; t < cr::maxAudioTracks; ++t) if (sess.audioTracks[(size_t) t].created && wanted (1000 + t)) jobs.push_back ({ 1000 + t, sess.audioTracks[(size_t) t].name });
        for (int a = 0; a < sess.auxCount; ++a) if (wanted (2000 + a)) jobs.push_back ({ 2000 + a, sess.aux[(size_t) a].name });
        for (int key : options.trackKeys)
            if (key >= 10000)
            {
                const int k = key - 10000;
                jobs.push_back ({ key, cr::bankName (k / cr::numPads) + " Pad " + juce::String (k % cr::numPads + 1) });
            }
    }
    if (jobs.empty()) { error = "Nothing to export."; return false; }

    deviceManager.removeAudioCallback (this);
    stop();
    // remember the transport state
    const bool wasLoop = sess.loopOn;
    std::array<cr::TrackSource, cr::numBanks> sources {};
    for (int b = 0; b < cr::numBanks; ++b) sources[(size_t) b] = sess.banks[(size_t) b].source;

    prepare (rate, 512);
    bool ok = true;
    const double tps = sess.bpm * cr::ppq / 60.0;
    const juce::int64 musicSamples = (juce::int64) std::ceil ((endTick - startTick) / tps * rate);
    const juce::int64 tail = options.includeTail ? (juce::int64) (rate * 2.0) : 0;
    const juce::int64 total = juce::jmin ((juce::int64) (rate * 60.0 * 60.0), musicSamples + tail);

    for (size_t j = 0; j < jobs.size() && ok; ++j)
    {
        auto target = (jobs[j].first < 0 || (options.writeToExactFile && jobs.size() == 1)) ? file
                                        : file.getSiblingFile (file.getFileNameWithoutExtension() + " - " + juce::File::createLegalFileName (jobs[j].second) + file.getFileExtension());
        juce::AudioBuffer<float> mix (2, (int) total);
        mix.clear();
        {
            const juce::ScopedLock sl (lock);
            offlineRender = true;
            exportSoloKey = jobs[j].first;
            exportBypassMaster = options.bypassMaster;
            exportIncludeReturns = options.includeReturns;
            sess.loopOn = false;
            for (auto& v : voices) { v.active = false; v.sample.reset(); v.stretched.reset(); }
            scheduledOffs.clear();
            prepareAllProcessors();
            for (auto& c : sess.aux) for (auto& s : c.inserts) if (s.builtin) s.builtin->reset();
            position = startTick;
            for (int b = 0; b < cr::numBanks; ++b) { patternOrigin[(size_t) b] = position; sess.banks[(size_t) b].queued = {}; }
            for (auto& b : sess.banks) for (auto& p : b.patterns) for (auto& n : p.notes) n.skipThisPass = false;
            playing = true;
            recording = false;
            inPreRoll = false;
        }
        juce::int64 done = 0;
        juce::AudioBuffer<float> block (2, 512);
        while (done < total)
        {
            const int n = (int) juce::jmin ((juce::int64) 512, total - done);
            {
                const juce::ScopedLock sl (lock);
                if (done >= musicSamples) playing = false;   // tail: let voices and effects ring out
                block.clear();
                renderBlock (nullptr, 0, block.getArrayOfWritePointers(), 2, n);
            }
            for (int c = 0; c < 2; ++c) mix.copyFrom (c, (int) done, block, c, 0, n);
            done += n;
            if (progress && ! progress (((float) j + (float) done / (float) total) / (float) jobs.size())) { ok = false; error = "Export cancelled."; break; }
        }
        if (! ok) break;
        if (options.normalize)
        {
            const float peak = mix.getMagnitude (0, mix.getNumSamples());
            if (peak > 0.0001f) mix.applyGain (0.98f / peak);
        }
        int bits = options.bitDepth;
        if (bits == 12)
        {
            for (int c = 0; c < 2; ++c)
            {
                auto* d = mix.getWritePointer (c);
                for (int i = 0; i < mix.getNumSamples(); ++i) d[i] = std::round (juce::jlimit (-1.0f, 1.0f, d[i]) * 2047.0f) / 2047.0f;
            }
            bits = 16;
        }
        if (auto w = createWavWriter (target, rate, 2, bits)) w->writeFromAudioSampleBuffer (mix, 0, mix.getNumSamples());
        else { ok = false; error = "Could not write " + target.getFullPathName(); }
    }

    {
        const juce::ScopedLock sl (lock);
        playing = false;
        offlineRender = false;
        exportSoloKey = -1;
        exportBypassMaster = exportIncludeReturns = false;
        sess.loopOn = wasLoop;
        for (int b = 0; b < cr::numBanks; ++b) sess.banks[(size_t) b].source = sources[(size_t) b];
        for (auto& v : voices) { v.active = false; v.sample.reset(); v.stretched.reset(); }
        scheduledOffs.clear();
        position = startTick;
        uiPosition = position;
    }
    prepare (deviceRate, deviceBlock);
    deviceManager.addAudioCallback (this);
    return ok;
}

bool Engine::saveSliceFiles (int b, int p, int l, int z, const std::vector<float>& slices, juce::String& error)
{
    cr::SamplePtr s;
    float zoneEnd = 1.0f;
    {
        const juce::ScopedLock sl (lock);
        auto& zone = sess.banks[(size_t) b].pads[(size_t) p].layers[(size_t) l].zones[(size_t) z];
        s = zone.sample;
        zoneEnd = zone.end;
    }
    if (! s) { error = "No sample."; return false; }
    if (s->isStreamed()) s = loadSample (s->file, false);
    if (! s || s->isStreamed()) { error = "Could not read the sample."; return false; }
    auto folder = getSamplesFolder().getChildFile (juce::File::createLegalFileName (s->name) + " slices");
    folder.createDirectory();
    const int len = s->buffer.getNumSamples();
    for (size_t i = 0; i < slices.size(); ++i)
    {
        const int a = (int) (slices[i] * len);
        const int e = (int) ((i + 1 < slices.size() ? slices[i + 1] : zoneEnd) * len);
        if (e - a < 2) continue;
        juce::AudioBuffer<float> part (2, e - a);
        for (int c = 0; c < 2; ++c) part.copyFrom (c, 0, s->buffer, c, a, e - a);
        auto f = folder.getChildFile (juce::File::createLegalFileName (s->name) + "_" + juce::String ((int) i + 1).paddedLeft ('0', 2) + ".wav");
        if (auto w = createWavWriter (f, s->sampleRate, 2, 24)) w->writeFromAudioSampleBuffer (part, 0, part.getNumSamples());
    }
    return true;
}
