#include <juce_audio_processors/juce_audio_processors.h>
// A real VST3 fixture: known stereo gain and persisted parameter state.
class TestGain final : public juce::AudioProcessor {
    juce::AudioParameterFloat* gain;
public:
    TestGain():AudioProcessor(BusesProperties().withInput("Input",juce::AudioChannelSet::stereo(),true).withInput("Aux",juce::AudioChannelSet::stereo(),true).withOutput("Output",juce::AudioChannelSet::stereo(),true)){
        addParameter(gain=new juce::AudioParameterFloat("gain","Gain",0.f,1.f,.5f));
    }
    void prepareToPlay(double,int)override{} void releaseResources()override{}
    void processBlock(juce::AudioBuffer<float>& b,juce::MidiBuffer&)override{
        // The host must allocate and silence the unconnected auxiliary bus.
        if(b.getNumChannels()<4){b.clear();return;}
        for(int c=2;c<4;++c)for(int n=0;n<b.getNumSamples();++n)if(b.getSample(c,n)!=0){b.clear();return;}
        b.applyGain(*gain);
    }
    bool isBusesLayoutSupported(const BusesLayout& b)const override{return b.getMainInputChannelSet()==juce::AudioChannelSet::stereo()&&b.getMainOutputChannelSet()==juce::AudioChannelSet::stereo();}
    const juce::String getName()const override{return "Mixer Test Gain";}
    bool acceptsMidi()const override{return false;}bool producesMidi()const override{return false;}double getTailLengthSeconds()const override{return 0;}
    int getNumPrograms()override{return 1;}int getCurrentProgram()override{return 0;}void setCurrentProgram(int)override{}const juce::String getProgramName(int)override{return {};}
    void changeProgramName(int,const juce::String&)override{}
    bool hasEditor()const override{return true;}juce::AudioProcessorEditor* createEditor()override{return new juce::GenericAudioProcessorEditor(*this);}
    void getStateInformation(juce::MemoryBlock& b)override{juce::MemoryOutputStream(b,false).writeFloat(*gain);}
    void setStateInformation(const void* data,int size)override{gain->setValueNotifyingHost(juce::MemoryInputStream(data,size,false).readFloat());}
};
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new TestGain;}
