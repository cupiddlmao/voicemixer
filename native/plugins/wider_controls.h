#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
namespace voicemixer {
using namespace juce;
// Host-side controls avoid Wine's foreign-window mouse/scaling bugs.
class WiderControls final : public AudioProcessorEditor,private Timer {
    AudioProcessorParameter *amount=nullptr,*low=nullptr,*power=nullptr,*mono=nullptr;
    Slider width,frequency;ToggleButton enabled{"Power"},monoCheck{"Mono check"};
    Label heading,widthLabel,lowLabel;
public:
    explicit WiderControls(AudioPluginInstance& p):AudioProcessorEditor(p){
        for(auto* parameter:p.getParameters()){
            const auto name=parameter->getName(128);
            if(name=="Amount")amount=parameter;else if(name=="LowBypass")low=parameter;
            else if(name=="Power")power=parameter;else if(name=="Mono")mono=parameter;
        }
        heading.setText("Wider - Stereo width",dontSendNotification);heading.setFont(Font(20));
        widthLabel.setText("Width",dontSendNotification);lowLabel.setText("Low bypass",dontSendNotification);
        for(auto* label:{&heading,&widthLabel,&lowLabel})addAndMakeVisible(label);
        width.setRange(0,200,.1);width.setTextValueSuffix(" %");frequency.setRange(0,1,.0001);
        frequency.textFromValueFunction=[this](double v){return low?low->getText(float(v),128):String(v);};
        frequency.valueFromTextFunction=[this](const String& v){return low?low->getValueForText(v):v.getDoubleValue();};frequency.updateText();
        for(auto* slider:{&width,&frequency}){slider->setSliderStyle(Slider::LinearHorizontal);slider->setTextBoxStyle(Slider::TextBoxRight,false,95,25);addAndMakeVisible(slider);}
        addAndMakeVisible(enabled);addAndMakeVisible(monoCheck);
        width.onValueChange=[this]{set(amount,float(width.getValue()/200));};frequency.onValueChange=[this]{set(low,float(frequency.getValue()));};
        enabled.onClick=[this]{set(power,enabled.getToggleState()?1.f:0.f);};monoCheck.onClick=[this]{set(mono,monoCheck.getToggleState()?1.f:0.f);};
        setSize(540,205);timerCallback();startTimerHz(20);
    }
    void paint(Graphics& g)override{g.fillAll(Colour(0xff344a57));}
    void resized()override{heading.setBounds(16,10,500,30);widthLabel.setBounds(16,54,88,28);width.setBounds(108,54,getWidth()-124,28);lowLabel.setBounds(16,96,88,28);frequency.setBounds(108,96,getWidth()-124,28);enabled.setBounds(16,151,115,28);monoCheck.setBounds(154,151,160,28);}
private:
    static void set(AudioProcessorParameter* p,float value){if(p){p->beginChangeGesture();p->setValueNotifyingHost(jlimit(0.f,1.f,value));p->endChangeGesture();}}
    void timerCallback()override{if(amount&&!width.isMouseButtonDown()&&!width.hasKeyboardFocus(true))width.setValue(jlimit(0.f,1.f,amount->getValue())*200,dontSendNotification);if(low&&!frequency.isMouseButtonDown()&&!frequency.hasKeyboardFocus(true))frequency.setValue(low->getValue(),dontSendNotification);if(power)enabled.setToggleState(power->getValue()>=.5f,dontSendNotification);if(mono)monoCheck.setToggleState(mono->getValue()>=.5f,dontSendNotification);}
};
}
