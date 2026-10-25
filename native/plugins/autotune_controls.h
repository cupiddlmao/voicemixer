#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <vector>

namespace voicemixer {
using namespace juce;

// Parameter controls deliberately do not create the plugin's Windows editor.
class AutoTuneControls final : public AudioProcessorEditor, private Timer {
    struct Dial {
        AudioProcessorParameter* parameter=nullptr;
        Label label;
        Slider slider;
        bool gesture=false;
        ~Dial(){if(gesture&&parameter)parameter->endChangeGesture();}
    };
    struct Choice {
        AudioProcessorParameter* parameter=nullptr;
        Label label;
        ComboBox combo;
    };
    struct Switch {
        AudioProcessorParameter* parameter=nullptr;
        ToggleButton button;
    };
    std::vector<std::unique_ptr<Dial>> dials;
    std::vector<std::unique_ptr<Choice>> choices;
    std::vector<std::unique_ptr<Switch>> switches;
    AudioProcessorParameter* classic=nullptr;
    Choice *modernScale=nullptr,*classicScale=nullptr;
    Label heading,subtitle,footer;

    AudioProcessorParameter* find(const String& name) const {
        for(auto* p:processor.getParameters())if(p->getName(128)==name)return p;
        return nullptr;
    }
    void labelStyle(Label& label,const String& text){
        label.setText(text,dontSendNotification);label.setColour(Label::textColourId,Colour(0xffc9d5de));
        label.setJustificationType(Justification::centred);addAndMakeVisible(label);
    }
    void addDial(const String& name,const String& title){
        auto dial=std::make_unique<Dial>();auto* d=dial.get();d->parameter=find(name);
        labelStyle(d->label,title);auto& slider=d->slider;
        slider.setComponentID(name);slider.setName(name);
        slider.setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle(Slider::TextBoxBelow,false,126,25);
        slider.setRange(0,1,.00001);slider.setRotaryParameters(MathConstants<float>::pi*1.25f,MathConstants<float>::pi*2.75f,true);
        slider.setColour(Slider::rotarySliderFillColourId,Colour(0xff3bb6f4));
        slider.setColour(Slider::rotarySliderOutlineColourId,Colour(0xff42505b));
        slider.setColour(Slider::thumbColourId,Colour(0xffe3edf2));
        slider.setColour(Slider::textBoxTextColourId,Colour(0xff51baf5));
        slider.setColour(Slider::textBoxBackgroundColourId,Colour(0xff20282e));
        slider.setColour(Slider::textBoxOutlineColourId,Colours::transparentBlack);
        if(auto* parameter=d->parameter){
            slider.setDoubleClickReturnValue(true,parameter->getDefaultValue());
            slider.textFromValueFunction=[parameter](double value){
                const auto text=parameter->getText(float(value),128),unit=parameter->getLabel();
                return unit.isEmpty()||text.endsWith(unit)?text:text+" "+unit;
            };
            slider.valueFromTextFunction=[parameter](const String& value){return parameter->getValueForText(value);};slider.updateText();
            slider.onDragStart=[d]{d->parameter->beginChangeGesture();d->gesture=true;};
            slider.onDragEnd=[d]{if(d->gesture){d->parameter->endChangeGesture();d->gesture=false;}};
            slider.onValueChange=[d]{
                if(!d->gesture)d->parameter->beginChangeGesture();
                d->parameter->setValueNotifyingHost(jlimit(0.f,1.f,float(d->slider.getValue())));
                if(!d->gesture)d->parameter->endChangeGesture();
            };
        }else{slider.setEnabled(false);slider.setTooltip("This plugin version does not expose "+name);}
        addAndMakeVisible(slider);dials.push_back(std::move(dial));
    }
    Choice* addChoice(const String& name,const String& title){
        auto choice=std::make_unique<Choice>();auto* c=choice.get();c->parameter=find(name);
        labelStyle(c->label,title);c->combo.setComponentID(name);c->combo.setName(name);
        c->combo.setColour(ComboBox::backgroundColourId,Colour(0xff20282e));
        c->combo.setColour(ComboBox::textColourId,Colour(0xff51baf5));
        c->combo.setColour(ComboBox::outlineColourId,Colour(0xff43515c));
        c->combo.setJustificationType(Justification::centred);
        const int steps=c->parameter?c->parameter->getNumSteps():0;
        if(steps>1&&steps<=128){
            for(int i=0;i<steps;++i)c->combo.addItem(c->parameter->getText(float(i)/float(steps-1),128),i+1);
            c->combo.onChange=[this,c,steps]{
                const int index=c->combo.getSelectedItemIndex();if(index<0)return;
                set(c->parameter,float(index)/float(steps-1));sync();
            };
        }else{c->combo.setEnabled(false);c->combo.setText("Unavailable",dontSendNotification);}
        addAndMakeVisible(c->combo);choices.push_back(std::move(choice));return c;
    }
    void addSwitch(const String& name,const String& title){
        auto item=std::make_unique<Switch>();auto* s=item.get();s->parameter=find(name);
        s->button.setButtonText(title);s->button.setComponentID(name);s->button.setName(name);
        s->button.setColour(ToggleButton::textColourId,Colour(0xffc9d5de));
        s->button.setColour(ToggleButton::tickColourId,Colour(0xff3bb6f4));
        s->button.setEnabled(s->parameter!=nullptr);
        s->button.onClick=[this,s]{set(s->parameter,s->button.getToggleState()?1.f:0.f);sync();};
        addAndMakeVisible(s->button);switches.push_back(std::move(item));
    }
    static void set(AudioProcessorParameter* p,float value){if(p){p->beginChangeGesture();p->setValueNotifyingHost(value);p->endChangeGesture();}}
    void sync(){
        for(auto& d:dials)if(d->parameter&&!d->gesture&&!d->slider.isMouseButtonDown()&&!d->slider.hasKeyboardFocus(true))d->slider.setValue(d->parameter->getValue(),dontSendNotification);
        for(auto& c:choices)if(c->parameter&&c->combo.getNumItems()>1)c->combo.setSelectedItemIndex(roundToInt(c->parameter->getValue()*float(c->combo.getNumItems()-1)),dontSendNotification);
        for(auto& s:switches)if(s->parameter)s->button.setToggleState(s->parameter->getValue()>=.5f,dontSendNotification);
        const bool isClassic=classic&&classic->getValue()>=.5f;
        for(auto* c:{modernScale,classicScale})if(c){const bool visible=(c==classicScale)==isClassic;c->label.setVisible(visible);c->combo.setVisible(visible);}
    }
    void timerCallback() override{sync();}
public:
    explicit AutoTuneControls(AudioPluginInstance& p):AudioProcessorEditor(p){
        heading.setText("AUTO-TUNE PRO",dontSendNotification);heading.setFont(Font(25));heading.setColour(Label::textColourId,Colours::white);addAndMakeVisible(heading);
        subtitle.setText("Voicemixer controls",dontSendNotification);subtitle.setColour(Label::textColourId,Colour(0xff7f9aaa));addAndMakeVisible(subtitle);
        addChoice("Input Type","INPUT TYPE");addChoice("Key","KEY");
        modernScale=addChoice("Modern Scale","SCALE");classicScale=addChoice("Scale","SCALE");
        addChoice("Correction Mode","CORRECTION MODE");
        classic=find("Use Classic Mode DSP");
        addSwitch("Use Classic Mode DSP","Classic DSP");addSwitch("Formant Correction","Formant correction");addSwitch("Master Bypass","Bypass");
        addDial("Retune Speed","Retune Speed");addDial("Flex-Tune","Flex-Tune");addDial("Humanize","Humanize");addDial("Natural Vibrato","Natural Vibrato");addDial("Wet-Dry Mix","Mix");
        addDial("Tracking","Tracking");addDial("Throat Length","Formant / Throat");addDial("Transpose","Transpose");addDial("Detune","Detune");addDial("Vibrato Rate","Vibrato Rate");
        footer.setText("Open Plugin UI for Graph editing. Parameters contains the remaining controls. Double-click a knob to reset it.",dontSendNotification);
        footer.setColour(Label::textColourId,Colour(0xff96a8b4));footer.setJustificationType(Justification::centred);addAndMakeVisible(footer);
        setSize(1000,570);sync();startTimerHz(20);
    }
    ~AutoTuneControls() override{stopTimer();}
    void paint(Graphics& g) override{
        g.fillAll(Colour(0xff252c32));g.setColour(Colour(0xff303b44));g.fillRect(0,0,getWidth(),58);
        g.setColour(Colour(0xff3bb6f4));g.fillRect(0,57,getWidth(),2);
        g.setColour(Colour(0xff42505b));g.drawHorizontalLine(184,20.f,float(getWidth()-20));g.drawHorizontalLine(532,20.f,float(getWidth()-20));
    }
    void resized() override{
        heading.setBounds(22,10,360,35);subtitle.setBounds(getWidth()-210,12,190,30);
        const int width=(getWidth()-48)/4;
        for(size_t i=0;i<choices.size();++i){auto& c=*choices[i];const int column=i<3?int(i):int(i)-1;c.label.setBounds(24+column*width,72,width-14,21);c.combo.setBounds(24+column*width,98,width-14,31);}
        for(size_t i=0;i<switches.size();++i)switches[i]->button.setBounds(28+int(i)*236,144,220,27);
        const int dialWidth=(getWidth()-40)/5;
        for(size_t i=0;i<dials.size();++i){auto& d=*dials[i];const int x=20+int(i%5)*dialWidth,y=196+int(i/5)*168;d.label.setBounds(x,y,dialWidth,24);d.slider.setBounds(x+12,y+27,dialWidth-24,128);}
        footer.setBounds(20,538,getWidth()-40,26);
    }
};
}
