/*******************************************************************************
 BEGIN_JUCE_PIP_METADATA

 name:             AdditiveSynthesizer
 version:          1.0.0
 vendor:           YourCompany
 website:          https://yourcompany.com
 description:      Professional Additive Synthesizer with Harmonic Editor and Morphing

 dependencies:     juce_audio_basics, juce_audio_devices, juce_audio_formats,
                   juce_audio_plugin_client, juce_audio_processors, juce_audio_utils,
                   juce_core, juce_data_structures, juce_dsp, juce_events,
                   juce_graphics, juce_gui_basics, juce_gui_extra
 exporters:        xcode_mac, vs2022, linux_make

 moduleFlags:      JUCE_STRICT_REFCOUNTEDPTR=1

 type:             AudioProcessor
 mainClass:        AdditiveSynthAudioProcessor

 useLocalCopy:     1

 END_JUCE_PIP_METADATA
*******************************************************************************/

#pragma once

#include <JuceHeader.h>
#include <array>
#include <vector>
#include <memory>

//==============================================================================
// CONSTANTS AND CONFIGURATION
//==============================================================================
namespace Constants
{
    constexpr int MAX_HARMONICS = 128;
    constexpr int MAX_VOICES = 16;
    constexpr int VISIBLE_HARMONICS = 32;
    constexpr float TWO_PI = 6.28318530717958647692f;
    constexpr float PI = 3.14159265358979323846f;
    constexpr float A4_FREQUENCY = 440.0f;
    constexpr int EDITOR_WIDTH = 800;
    constexpr int EDITOR_HEIGHT = 600;
}

//==============================================================================
// HARMONIC STATE - Core Data Structure
//==============================================================================
struct HarmonicData
{
    float amplitude = 0.0f;
    float phase = 0.0f;
    bool enabled = false;
    
    HarmonicData() = default;
    HarmonicData(float amp, float ph = 0.0f) 
        : amplitude(amp), phase(ph), enabled(amp > 0.001f) {}
};

class HarmonicState
{
public:
    HarmonicState() { clear(); }
    
    void setHarmonic(int index, float amplitude, float phase = 0.0f)
    {
        if (index >= 0 && index < Constants::MAX_HARMONICS)
        {
            harmonics[index].amplitude = juce::jlimit(0.0f, 1.0f, amplitude);
            harmonics[index].phase = phase;
            harmonics[index].enabled = amplitude > 0.001f;
        }
    }
    
    void setHarmonicAmplitude(int index, float amplitude)
    {
        if (index >= 0 && index < Constants::MAX_HARMONICS)
        {
            harmonics[index].amplitude = juce::jlimit(0.0f, 1.0f, amplitude);
            harmonics[index].enabled = amplitude > 0.001f;
        }
    }
    
    const HarmonicData& getHarmonic(int index) const
    {
        static HarmonicData empty;
        return (index >= 0 && index < Constants::MAX_HARMONICS) ? harmonics[index] : empty;
    }
    
    float getHarmonicAmplitude(int index) const
    {
        return (index >= 0 && index < Constants::MAX_HARMONICS) ? harmonics[index].amplitude : 0.0f;
    }
    
    void morphTo(const HarmonicState& target, float amount)
    {
        amount = juce::jlimit(0.0f, 1.0f, amount);
        for (int i = 0; i < Constants::MAX_HARMONICS; ++i)
        {
            harmonics[i].amplitude = harmonics[i].amplitude * (1.0f - amount) + 
                                    target.harmonics[i].amplitude * amount;
            harmonics[i].phase = harmonics[i].phase * (1.0f - amount) + 
                               target.harmonics[i].phase * amount;
        }
    }
    
    void copyFrom(const HarmonicState& other)
    {
        harmonics = other.harmonics;
    }
    
    void clear()
    {
        for (auto& h : harmonics)
        {
            h.amplitude = 0.0f;
            h.phase = 0.0f;
            h.enabled = false;
        }
    }
    
    void loadPreset(const juce::String& presetName)
    {
        clear();
        
        if (presetName == "Saw")
        {
            for (int i = 0; i < 32; ++i)
                harmonics[i].amplitude = 1.0f / (i + 1);
        }
        else if (presetName == "Square")
        {
            for (int i = 0; i < 32; i += 2)
                harmonics[i].amplitude = 1.0f / (i + 1);
        }
        else if (presetName == "Triangle")
        {
            for (int i = 0; i < 32; i += 2)
            {
                float amp = 1.0f / ((i + 1) * (i + 1));
                harmonics[i].amplitude = (i % 4 == 0) ? amp : -amp;
            }
        }
        else if (presetName == "Sine")
        {
            harmonics[0].amplitude = 1.0f;
        }
        else if (presetName == "Organ")
        {
            harmonics[0].amplitude = 1.0f;
            harmonics[2].amplitude = 0.5f;
            harmonics[4].amplitude = 0.3f;
        }
        
        for (auto& h : harmonics)
            h.enabled = h.amplitude > 0.001f;
    }
    
    std::array<HarmonicData, Constants::MAX_HARMONICS> harmonics;
};

//==============================================================================
// SINE WAVE GENERATOR
//==============================================================================
class SineWaveGenerator
{
public:
    void prepare(double sr) { sampleRate = sr; updatePhaseIncrement(); }
    void reset() { currentPhase = 0.0f; }
    
    void setFrequency(float freq) { frequency = freq; updatePhaseIncrement(); }
    void setAmplitude(float amp) { amplitude = juce::jlimit(0.0f, 1.0f, amp); }
    
    float getNextSample()
    {
        if (amplitude < 0.001f) return 0.0f;
        
        float sample = amplitude * std::sin(currentPhase);
        currentPhase += phaseIncrement;
        
        if (currentPhase >= Constants::TWO_PI)
            currentPhase -= Constants::TWO_PI;
        
        return sample;
    }
    
private:
    double sampleRate = 44100.0;
    float frequency = 440.0f;
    float amplitude = 0.0f;
    float currentPhase = 0.0f;
    float phaseIncrement = 0.0f;
    
    void updatePhaseIncrement()
    {
        phaseIncrement = Constants::TWO_PI * frequency / static_cast<float>(sampleRate);
    }
};

//==============================================================================
// HARMONIC OSCILLATOR
//==============================================================================
class HarmonicOscillator
{
public:
    void prepare(double sampleRate)
    {
        for (auto& osc : oscillators)
            osc.prepare(sampleRate);
    }
    
    void reset()
    {
        for (auto& osc : oscillators)
            osc.reset();
    }
    
    void setFrequency(float freq)
    {
        fundamentalFrequency = freq;
        updateOscillatorFrequencies();
    }
    
    void setHarmonicState(const HarmonicState& state)
    {
        for (int i = 0; i < Constants::MAX_HARMONICS; ++i)
        {
            oscillators[i].setAmplitude(state.getHarmonicAmplitude(i));
        }
    }
    
    float getNextSample()
    {
        float sample = 0.0f;
        for (int i = 0; i < Constants::MAX_HARMONICS; ++i)
        {
            if (oscillators[i].getNextSample() != 0.0f)
                sample += oscillators[i].getNextSample();
        }
        return sample * masterGain;
    }
    
    void setGain(float gain) { masterGain = gain; }
    
private:
    std::array<SineWaveGenerator, Constants::MAX_HARMONICS> oscillators;
    float fundamentalFrequency = 440.0f;
    float masterGain = 0.5f;
    
    void updateOscillatorFrequencies()
    {
        for (int i = 0; i < Constants::MAX_HARMONICS; ++i)
        {
            oscillators[i].setFrequency(fundamentalFrequency * (i + 1));
        }
    }
};

//==============================================================================
// ENVELOPE PROCESSOR
//==============================================================================
class EnvelopeProcessor
{
public:
    enum class State { Idle, Attack, Decay, Sustain, Release };
    
    void prepare(double sr) 
    { 
        sampleRate = sr; 
        calculateRates();
    }
    
    void noteOn() 
    { 
        currentState = State::Attack;
        currentLevel = 0.0f;
    }
    
    void noteOff() 
    { 
        currentState = State::Release; 
    }
    
    void setAttack(float seconds) { attackTime = seconds; calculateRates(); }
    void setDecay(float seconds) { decayTime = seconds; calculateRates(); }
    void setSustain(float level) { sustainLevel = juce::jlimit(0.0f, 1.0f, level); }
    void setRelease(float seconds) { releaseTime = seconds; calculateRates(); }
    
    float getNextSample()
    {
        switch (currentState)
        {
            case State::Idle:
                return 0.0f;
                
            case State::Attack:
                currentLevel += attackRate;
                if (currentLevel >= 1.0f)
                {
                    currentLevel = 1.0f;
                    currentState = State::Decay;
                }
                break;
                
            case State::Decay:
                currentLevel -= decayRate;
                if (currentLevel <= sustainLevel)
                {
                    currentLevel = sustainLevel;
                    currentState = State::Sustain;
                }
                break;
                
            case State::Sustain:
                currentLevel = sustainLevel;
                break;
                
            case State::Release:
                currentLevel -= releaseRate;
                if (currentLevel <= 0.0f)
                {
                    currentLevel = 0.0f;
                    currentState = State::Idle;
                }
                break;
        }
        
        return currentLevel;
    }
    
    bool isActive() const { return currentState != State::Idle; }
    
private:
    State currentState = State::Idle;
    double sampleRate = 44100.0;
    float currentLevel = 0.0f;
    
    float attackTime = 0.01f;
    float decayTime = 0.1f;
    float sustainLevel = 0.7f;
    float releaseTime = 0.5f;
    
    float attackRate = 0.0f;
    float decayRate = 0.0f;
    float releaseRate = 0.0f;
    
    void calculateRates()
    {
        attackRate = 1.0f / (attackTime * sampleRate);
        decayRate = (1.0f - sustainLevel) / (decayTime * sampleRate);
        releaseRate = sustainLevel / (releaseTime * sampleRate);
    }
};

//==============================================================================
// ADDITIVE VOICE
//==============================================================================
class AdditiveVoice : public juce::SynthesiserVoice
{
public:
    bool canPlaySound(juce::SynthesiserSound*) override { return true; }
    
    void startNote(int midiNoteNumber, float velocity,
                   juce::SynthesiserSound*, int) override
    {
        currentVelocity = velocity;
        float freq = 440.0f * std::pow(2.0f, (midiNoteNumber - 69) / 12.0f);
        oscillator.setFrequency(freq);
        envelope.noteOn();
    }
    
    void stopNote(float, bool allowTailOff) override
    {
        if (allowTailOff)
            envelope.noteOff();
        else
            clearCurrentNote();
    }
    
    void pitchWheelMoved(int) override {}
    void controllerMoved(int, int) override {}
    
    void renderNextBlock(juce::AudioBuffer<float>& outputBuffer,
                        int startSample, int numSamples) override
    {
        if (!envelope.isActive())
        {
            clearCurrentNote();
            return;
        }
        
        for (int i = 0; i < numSamples; ++i)
        {
            float envLevel = envelope.getNextSample();
            float sample = oscillator.getNextSample() * envLevel * currentVelocity;
            
            for (int ch = 0; ch < outputBuffer.getNumChannels(); ++ch)
                outputBuffer.addSample(ch, startSample + i, sample);
        }
    }
    
    void prepare(double sampleRate)
    {
        oscillator.prepare(sampleRate);
        envelope.prepare(sampleRate);
    }
    
    void setHarmonicState(const HarmonicState& state)
    {
        oscillator.setHarmonicState(state);
    }
    
    void setEnvelope(float attack, float decay, float sustain, float release)
    {
        envelope.setAttack(attack);
        envelope.setDecay(decay);
        envelope.setSustain(sustain);
        envelope.setRelease(release);
    }
    
private:
    HarmonicOscillator oscillator;
    EnvelopeProcessor envelope;
    float currentVelocity = 1.0f;
};

//==============================================================================
// ADDITIVE SOUND
//==============================================================================
class AdditiveSound : public juce::SynthesiserSound
{
public:
    bool appliesToNote(int) override { return true; }
    bool appliesToChannel(int) override { return true; }
};

//==============================================================================
// MORPHING ENGINE
//==============================================================================
class MorphingEngine
{
public:
    void setSourceState(const HarmonicState& source) { sourceState.copyFrom(source); }
    void setTargetState(const HarmonicState& target) { targetState.copyFrom(target); }
    void setMorphAmount(float amount) { morphAmount = juce::jlimit(0.0f, 1.0f, amount); }
    
    HarmonicState getCurrentState() const
    {
        HarmonicState result;
        result.copyFrom(sourceState);
        result.morphTo(targetState, morphAmount);
        return result;
    }
    
    float getMorphAmount() const { return morphAmount; }
    
private:
    HarmonicState sourceState, targetState;
    float morphAmount = 0.0f;
};

//==============================================================================
// CUSTOM LOOK AND FEEL
//==============================================================================
class AdditiveSynthLookAndFeel : public juce::LookAndFeel_V4
{
public:
    AdditiveSynthLookAndFeel()
    {
        setColour(juce::Slider::thumbColourId, juce::Colours::lightblue);
        setColour(juce::Slider::trackColourId, juce::Colour(0xff4a9eff));
        setColour(juce::Slider::backgroundColourId, juce::Colour(0xff2a2a2a));
    }
    
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                         float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                         juce::Slider& slider) override
    {
        auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(10);
        auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) / 2.0f;
        auto toAngle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
        auto centre = bounds.getCentre();
        
        // Draw outer circle
        g.setColour(juce::Colour(0xff3a3a3a));
        g.fillEllipse(bounds);
        
        g.setColour(juce::Colour(0xff4a4a4a));
        g.drawEllipse(bounds, 2.0f);
        
        // Draw pointer
        juce::Path p;
        auto pointerLength = radius * 0.6f;
        auto pointerThickness = 3.0f;
        p.addRectangle(-pointerThickness * 0.5f, -radius, pointerThickness, pointerLength);
        p.applyTransform(juce::AffineTransform::rotation(toAngle).translated(centre.x, centre.y));
        
        g.setColour(juce::Colour(0xff4a9eff));
        g.fillPath(p);
    }
};

//==============================================================================
// HARMONIC EDITOR COMPONENT
//==============================================================================
class HarmonicEditor : public juce::Component
{
public:
    HarmonicEditor()
    {
        setSize(600, 220);
    }
    
    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff1e1e1e));
        
        auto bounds = getLocalBounds().reduced(10);
        bounds.removeFromBottom(20); // Space for labels
        
       
        // Draw grid lines
        g.setColour(juce::Colours::white.withAlpha(0.1f));
        for (int i = 1; i <= 4; ++i)
        {
            int y = bounds.getY() + bounds.getHeight() * i / 5;
            g.drawLine(bounds.getX(), y, bounds.getRight(), y);
        }
        
        // Draw harmonic bars
        int barWidth = bounds.getWidth() / Constants::VISIBLE_HARMONICS;
        
        for (int i = 0; i < Constants::VISIBLE_HARMONICS; ++i)
        {
            float amp = currentState.getHarmonicAmplitude(i);
            int barHeight = static_cast<int>(amp * bounds.getHeight());
            
            int x = bounds.getX() + i * barWidth + 1;
            int y = bounds.getBottom() - barHeight;
            
            juce::Colour barColour = (i == selectedHarmonic) 
                ? juce::Colour(0xffff6b4a) 
                : juce::Colour(0xff4a9eff);
            
            if (amp > 0.001f)
            {
                g.setColour(barColour);
                g.fillRect(x, y, barWidth - 2, barHeight);
                
                // Glow effect
                g.setColour(barColour.withAlpha(0.3f));
                g.drawRect(x - 1, y - 1, barWidth, barHeight + 2);
            }
        }
        
        // Draw frequency labels
        g.setColour(juce::Colours::grey);
        g.setFont(9.0f);
        auto labelBounds = getLocalBounds().removeFromBottom(15);
        g.drawText("1", labelBounds.removeFromLeft(barWidth * 1), juce::Justification::left, false);
        g.drawText("8", labelBounds.removeFromLeft(barWidth * 7), juce::Justification::left, false);
        g.drawText("16", labelBounds.removeFromLeft(barWidth * 8), juce::Justification::left, false);
        g.drawText("32", labelBounds, juce::Justification::left, false);
    }
    
    void mouseDown(const juce::MouseEvent& e) override
    {
        isDragging = true;
        updateHarmonicFromMouse(e.position);
    }
    
    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (isDragging)
            updateHarmonicFromMouse(e.position);
    }
    
    void mouseUp(const juce::MouseEvent&) override
    {
        isDragging = false;
    }
    
    void setHarmonicState(const HarmonicState& state)
    {
        currentState.copyFrom(state);
        repaint();
    }
    
    const HarmonicState& getHarmonicState() const { return currentState; }
    
    std::function<void(const HarmonicState&)> onStateChanged;
    
private:
    HarmonicState currentState;
    int selectedHarmonic = -1;
    bool isDragging = false;
    
    void updateHarmonicFromMouse(juce::Point<float> pos)
    {
        auto bounds = getLocalBounds().reduced(10);
        bounds.removeFromBottom(20);
        
        int barWidth = bounds.getWidth() / Constants::VISIBLE_HARMONICS;
        int harmonic = static_cast<int>((pos.x - bounds.getX()) / barWidth);
        
        if (harmonic >= 0 && harmonic < Constants::VISIBLE_HARMONICS)
        {
            selectedHarmonic = harmonic;
            float amplitude = juce::jlimit(0.0f, 1.0f, 
                (bounds.getBottom() - pos.y) / bounds.getHeight());
            
            currentState.setHarmonicAmplitude(harmonic, amplitude);
            
            if (onStateChanged)
                onStateChanged(currentState);
            
            repaint();
        }
    }
};

//==============================================================================
// WAVEFORM VISUALIZER
//==============================================================================
class WaveformVisualizer : public juce::Component, private juce::Timer
{
public:
    WaveformVisualizer()
    {
        setSize(600, 120);
        startTimerHz(30);
    }
    
    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff1a1a1a));
        
        auto bounds = getLocalBounds().reduced(10).toFloat();
        
        // Draw centerline
        g.setColour(juce::Colours::white.withAlpha(0.1f));
        g.drawLine(bounds.getX(), bounds.getCentreY(), 
                   bounds.getRight(), bounds.getCentreY());
        
        // Draw waveform
        if (waveformPath.getLength() > 0)
        {
            g.setColour(juce::Colour(0xff4a9eff));
            g.strokePath(waveformPath, juce::PathStrokeType(2.0f));
        }
    }
    
    void updateWaveform(const HarmonicState& state)
    {
        waveformPath.clear();
        
        auto bounds = getLocalBounds().reduced(10).toFloat();
        int numSamples = 400;
        float sampleRate = 44100.0f;
        float frequency = 440.0f;
        
        for (int i = 0; i < numSamples; ++i)
        {
            float t = i / sampleRate;
            float sample = 0.0f;
            
            // Sum harmonics
            for (int h = 0; h < 16; ++h)
            {
                float amp = state.getHarmonicAmplitude(h);
                if (amp > 0.001f)
                {
                    sample += amp * std::sin(Constants::TWO_PI * frequency * (h + 1) * t);
                }
            }
            
            float x = bounds.getX() + (i / (float)numSamples) * bounds.getWidth();
            float y = bounds.getCentreY() - sample * bounds.getHeight() * 0.4f;
            
            if (i == 0)
                waveformPath.startNewSubPath(x, y);
            else
                waveformPath.lineTo(x, y);
        }
        
        repaint();
    }
    
private:
    juce::Path waveformPath;
    
    void timerCallback() override
    {
        // Animate waveform if needed
    }
};

//==============================================================================
// AUDIO PROCESSOR
//==============================================================================
class AdditiveSynthAudioProcessor : public juce::AudioProcessor
{
public:
    AdditiveSynthAudioProcessor()
        : AudioProcessor(BusesProperties()
                        .withOutput("Output", juce::AudioChannelSet::stereo(), true))
    {
        synthesiser.clearVoices();
        for (int i = 0; i < Constants::MAX_VOICES; ++i)
            synthesiser.addVoice(new AdditiveVoice());
        
        synthesiser.clearSounds();
        synthesiser.addSound(new AdditiveSound());
        
        // Initialize with saw wave
        harmonicState.loadPreset("Saw");
        updateVoicesWithHarmonicState();
    }
    
    ~AdditiveSynthAudioProcessor() override = default;
    
    void prepareToPlay(double sampleRate, int samplesPerBlock) override
    {
        synthesiser.setCurrentPlaybackSampleRate(sampleRate);
        
        for (int i = 0; i < synthesiser.getNumVoices(); ++i)
        {
            if (auto* voice = dynamic_cast<AdditiveVoice*>(synthesiser.getVoice(i)))
            {
                voice->prepare(sampleRate);
            }
        }
    }
    
    void releaseResources() override {}
    
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override
    {
        buffer.clear();
        synthesiser.renderNextBlock(buffer, midiMessages, 0, buffer.getNumSamples());
    }
    
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    
    const juce::String getName() const override { return "AdditiveSynth"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}
    
    // Public interface
    HarmonicState& getHarmonicState() { return harmonicState; }
    void setHarmonicState(const HarmonicState& state)
    {
        harmonicState.copyFrom(state);
        updateVoicesWithHarmonicState();
    }
    
    void setEnvelopeParameters(float attack, float decay, float sustain, float release)
    {
        for (int i = 0; i < synthesiser.getNumVoices(); ++i)
        {
            if (auto* voice = dynamic_cast<AdditiveVoice*>(synthesiser.getVoice(i)))
            {
                voice->setEnvelope(attack, decay, sustain, release);
            }
        }
    }
    
    MorphingEngine& getMorphingEngine() { return morphingEngine; }
    
private:
    juce::Synthesiser synthesiser;
    HarmonicState harmonicState;
    MorphingEngine morphingEngine;
    
    void updateVoicesWithHarmonicState()
    {
        for (int i = 0; i < synthesiser.getNumVoices(); ++i)
        {
            if (auto* voice = dynamic_cast<AdditiveVoice*>(synthesiser.getVoice(i)))
            {
                voice->setHarmonicState(harmonicState);
            }
        }
    }
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AdditiveSynthAudioProcessor)
};

//==============================================================================
// AUDIO PROCESSOR EDITOR
//==============================================================================
class AdditiveSynthAudioProcessorEditor : public juce::AudioProcessorEditor,
                                         private juce::Timer
{
public:
    AdditiveSynthAudioProcessorEditor(AdditiveSynthAudioProcessor& p)
        : AudioProcessorEditor(&p), processor(p)
    {
        setLookAndFeel(&customLookAndFeel);
        
        // Setup components
        addAndMakeVisible(harmonicEditor);
        addAndMakeVisible(waveformVisualizer);
        addAndMakeVisible(presetComboBox);
        addAndMakeVisible(morphSlider);
        
        // Setup preset combo box
        presetComboBox.addItem("Saw", 1);
        presetComboBox.addItem("Square", 2);
        presetComboBox.addItem("Triangle", 3);
        presetComboBox.addItem("Sine", 4);
        presetComboBox.addItem("Organ", 5);
        presetComboBox.setSelectedId(1);
        
        presetComboBox.onChange = [this]
        {
            auto preset = presetComboBox.getText();
            processor.getHarmonicState().loadPreset(preset);
            processor.setHarmonicState(processor.getHarmonicState());
            harmonicEditor.setHarmonicState(processor.getHarmonicState());
            waveformVisualizer.updateWaveform(processor.getHarmonicState());
        };
        
        // Setup harmonic editor callback
        harmonicEditor.onStateChanged = [this](const HarmonicState& state)
        {
            processor.setHarmonicState(state);
            waveformVisualizer.updateWaveform(state);
        };
        
        // Setup morph slider
        morphSlider.setRange(0.0, 1.0, 0.01);
        morphSlider.setValue(0.0);
        morphSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        morphSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 20);
        
        morphSlider.onValueChange = [this]
        {
            float amount = morphSlider.getValue();
            processor.getMorphingEngine().setMorphAmount(amount);
            auto morphedState = processor.getMorphingEngine().getCurrentState();
            processor.setHarmonicState(morphedState);
            harmonicEditor.setHarmonicState(morphedState);
            waveformVisualizer.updateWaveform(morphedState);
        };
        
        // Setup envelope sliders
        attackSlider.setRange(0.001, 2.0, 0.001);
        attackSlider.setValue(0.01);
        attackSlider.setSliderStyle(juce::Slider::Rotary);
        attackSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 20);
        attackSlider.onValueChange = [this] { updateEnvelope(); };
        addAndMakeVisible(attackSlider);
        
        decaySlider.setRange(0.001, 2.0, 0.001);
        decaySlider.setValue(0.1);
        decaySlider.setSliderStyle(juce::Slider::Rotary);
        decaySlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 20);
        decaySlider.onValueChange = [this] { updateEnvelope(); };
        addAndMakeVisible(decaySlider);
        
        sustainSlider.setRange(0.0, 1.0, 0.01);
        sustainSlider.setValue(0.7);
        sustainSlider.setSliderStyle(juce::Slider::Rotary);
        sustainSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 20);
        sustainSlider.onValueChange = [this] { updateEnvelope(); };
        addAndMakeVisible(sustainSlider);
        
        releaseSlider.setRange(0.001, 5.0, 0.001);
        releaseSlider.setValue(0.5);
        releaseSlider.setSliderStyle(juce::Slider::Rotary);
        releaseSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 20);
        releaseSlider.onValueChange = [this] { updateEnvelope(); };
        addAndMakeVisible(releaseSlider);
        
        // Setup labels
        addAndMakeVisible(titleLabel);
        titleLabel.setText("ADDITIVE SYNTHESIZER", juce::dontSendNotification);
        titleLabel.setFont(juce::FontOptions(18.0f).withStyle("bold"));
        titleLabel.setJustificationType(juce::Justification::centred);
        titleLabel.setColour(juce::Label::textColourId, juce::Colours::lightblue);
        
        addAndMakeVisible(harmonicEditorLabel);
        harmonicEditorLabel.setText("HARMONIC EDITOR", juce::dontSendNotification);
        harmonicEditorLabel.setFont(juce::FontOptions(12.0f).withStyle("bold"));
        
        addAndMakeVisible(waveformLabel);
        waveformLabel.setText("WAVEFORM VISUALIZER", juce::dontSendNotification);
        waveformLabel.setFont(juce::FontOptions(12.0f).withStyle("bold"));
        
        addAndMakeVisible(morphLabel);
        morphLabel.setText("MORPHING", juce::dontSendNotification);
        morphLabel.setFont(juce::FontOptions(12.0f).withStyle("bold"));
        
        addAndMakeVisible(envelopeLabel);
        envelopeLabel.setText("ENVELOPE", juce::dontSendNotification);
                           envelopeLabel.setFont(juce::FontOptions(12.0f).withStyle("bold"));
        
        addAndMakeVisible(attackLabel);
        attackLabel.setText("Attack", juce::dontSendNotification);
        attackLabel.setJustificationType(juce::Justification::centred);
        attackLabel.setFont(juce::Font (juce::FontOptions(10.0f)));
        
        addAndMakeVisible(decayLabel);
        decayLabel.setText("Decay", juce::dontSendNotification);
        decayLabel.setJustificationType(juce::Justification::centred);
        decayLabel.setFont(juce::Font (juce::FontOptions(10.0f)));
        
        addAndMakeVisible(sustainLabel);
        sustainLabel.setText("Sustain", juce::dontSendNotification);
        sustainLabel.setJustificationType(juce::Justification::centred);
        sustainLabel.setFont(juce::Font (juce::FontOptions(10.0f)));
        
        addAndMakeVisible(releaseLabel);
        releaseLabel.setText("Release", juce::dontSendNotification);
        releaseLabel.setJustificationType(juce::Justification::centred);
        releaseLabel.setFont(juce::Font (juce::FontOptions(10.0f)));
        
        addAndMakeVisible(presetLabel);
        presetLabel.setText("Presets:", juce::dontSendNotification);
        presetLabel.setFont(juce::Font (juce::FontOptions(11.0f)));
        
        // Setup source/target buttons for morphing
        addAndMakeVisible(setSourceButton);
        setSourceButton.setButtonText("Set Source A");
        setSourceButton.onClick = [this]
        {
            sourceState.copyFrom(processor.getHarmonicState());
            processor.getMorphingEngine().setSourceState(sourceState);
        };
        
        addAndMakeVisible(setTargetButton);
        setTargetButton.setButtonText("Set Target B");
        setTargetButton.onClick = [this]
        {
            targetState.copyFrom(processor.getHarmonicState());
            processor.getMorphingEngine().setTargetState(targetState);
        };
        
        // Initialize with current state
        harmonicEditor.setHarmonicState(processor.getHarmonicState());
        waveformVisualizer.updateWaveform(processor.getHarmonicState());
        
        setSize(Constants::EDITOR_WIDTH, Constants::EDITOR_HEIGHT);
        startTimerHz(30);
    }
    
    ~AdditiveSynthAudioProcessorEditor() override
    {
        setLookAndFeel(nullptr);
    }
    
    void paint(juce::Graphics& g) override
    {
        // Background gradient
        g.fillAll(juce::Colour(0xff2a2a2a));
        
        g.setColour(juce::Colours::white);
       
        auto bounds = getLocalBounds();
        g.setGradientFill(juce::ColourGradient(
            juce::Colour(0xff2a2a2a), bounds.getX(), bounds.getY(),
            juce::Colour(0xff1e1e1e), bounds.getRight(), bounds.getBottom(),
            false));
        g.fillAll();
        
        // Draw panel borders
        g.setColour(juce::Colour(0xff444444));
        g.drawRect(leftPanelBounds, 1);
        g.drawRect(centerPanelBounds, 1);
        g.drawRect(rightPanelBounds, 1);
        g.setColour(juce::Colours::white);
        g.setFont(16.0f);
       
        juce::Rectangle<int> area(rightPanelBounds.getX(), rightPanelBounds.getY()+80, getWidth() -40, getHeight() - 80);
        juce::String text = "- CC1 (Mod Wheel " "Morph Amount (0-100%)  - CC16-47 "  " Harmonics 1-32 (0-100% amplitude)" " - CC70                Sustain Level (0-100%)  - CC72                Release Time (0-5s)   - CC73                Attack Time (0-2s)  - CC75                Decay Time (0-2s)   - CC7                 Master Volume (0-100%) ";
        g.drawRect(area, 2);
        g.drawFittedText(text, area, juce::Justification::left, 10);
        
    }
    
    void resized() override
    {
        auto bounds = getLocalBounds().reduced(10);
        
        // Title area
        auto titleArea = bounds.removeFromTop(40);
        titleLabel.setBounds(titleArea);
        
        bounds.removeFromTop(10); // Spacing
        
        // Main layout: left panel | center panel | right panel
        leftPanelBounds = bounds.removeFromLeft(180);
        bounds.removeFromLeft(10); // Spacing
        
        rightPanelBounds = bounds.removeFromRight(180);
        bounds.removeFromRight(10); // Spacing
        
        centerPanelBounds = bounds;
        
        // LEFT PANEL - Envelope controls
        auto leftArea = leftPanelBounds.reduced(10);
        
        envelopeLabel.setBounds(leftArea.removeFromTop(20));
        leftArea.removeFromTop(10);
        
        // Envelope knobs in 2x2 grid
        auto envelopeArea = leftArea.removeFromTop(180);
        auto topRow = envelopeArea.removeFromTop(90);
        auto bottomRow = envelopeArea;
        
        auto attackArea = topRow.removeFromLeft(topRow.getWidth() / 2);
        attackLabel.setBounds(attackArea.removeFromTop(15));
        attackSlider.setBounds(attackArea);
        
        auto decayArea = topRow;
        decayLabel.setBounds(decayArea.removeFromTop(15));
        decaySlider.setBounds(decayArea);
        
        auto sustainArea = bottomRow.removeFromLeft(bottomRow.getWidth() / 2);
        sustainLabel.setBounds(sustainArea.removeFromTop(15));
        sustainSlider.setBounds(sustainArea);
        
        auto releaseArea = bottomRow;
        releaseLabel.setBounds(releaseArea.removeFromTop(15));
        releaseSlider.setBounds(releaseArea);
        
        // CENTER PANEL - Harmonic editor and visualizer
        auto centerArea = centerPanelBounds.reduced(10);
        
        harmonicEditorLabel.setBounds(centerArea.removeFromTop(20));
        centerArea.removeFromTop(5);
        harmonicEditor.setBounds(centerArea.removeFromTop(220));
        
        centerArea.removeFromTop(15);
        
        waveformLabel.setBounds(centerArea.removeFromTop(20));
        centerArea.removeFromTop(5);
        waveformVisualizer.setBounds(centerArea.removeFromTop(120));
        
        // RIGHT PANEL - Presets and morphing
        auto rightArea = rightPanelBounds.reduced(10);
        
        presetLabel.setBounds(rightArea.removeFromTop(20));
        rightArea.removeFromTop(5);
        presetComboBox.setBounds(rightArea.removeFromTop(25));
        
        rightArea.removeFromTop(20);
        
        morphLabel.setBounds(rightArea.removeFromTop(20));
        rightArea.removeFromTop(5);
        
        setSourceButton.setBounds(rightArea.removeFromTop(30));
        rightArea.removeFromTop(5);
        setTargetButton.setBounds(rightArea.removeFromTop(30));
        rightArea.removeFromTop(10);
        
        morphSlider.setBounds(rightArea.removeFromTop(80));
    }
    
private:
    AdditiveSynthAudioProcessor& processor;
    AdditiveSynthLookAndFeel customLookAndFeel;
    
    // Components
    HarmonicEditor harmonicEditor;
    WaveformVisualizer waveformVisualizer;
    
    juce::ComboBox presetComboBox;
    juce::Slider morphSlider;
    juce::Slider attackSlider, decaySlider, sustainSlider, releaseSlider;
    
    juce::TextButton setSourceButton, setTargetButton;
    
    juce::Label titleLabel;
    juce::Label harmonicEditorLabel, waveformLabel, morphLabel, envelopeLabel;
    juce::Label presetLabel;
    juce::Label attackLabel, decayLabel, sustainLabel, releaseLabel;

    
    // Panel bounds for drawing
    juce::Rectangle<int> leftPanelBounds, centerPanelBounds, rightPanelBounds;
    
    // Morphing states
    HarmonicState sourceState, targetState;
    
    void updateEnvelope()
    {
        processor.setEnvelopeParameters(
            static_cast<float>(attackSlider.getValue()),
            static_cast<float>(decaySlider.getValue()),
            static_cast<float>(sustainSlider.getValue()),
            static_cast<float>(releaseSlider.getValue())
        );
    }
    
    void timerCallback() override
    {
        // Update visualizations if needed
        repaint();
    }
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AdditiveSynthAudioProcessorEditor)
};

// Implementation of createEditor
juce::AudioProcessorEditor* AdditiveSynthAudioProcessor::createEditor()
{
    return new AdditiveSynthAudioProcessorEditor(*this);
}

//==============================================================================
// This creates new instances of the plugin
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AdditiveSynthAudioProcessor();
}
