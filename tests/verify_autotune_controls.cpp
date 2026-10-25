// Opt-in real-plugin integration check. Uses synthetic audio and a private prefix.
#include "../native/plugins/autotune_controls.h"
#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>
#include <cmath>
#include <X11/Xlib.h>
#include <dlfcn.h>
using namespace juce;
int main(int argc,char** argv){
    ScopedJuceInitialiser_GUI gui;if(argc!=3)return 2;
    AudioPluginFormatManager formats;formats.addDefaultFormats();OwnedArray<PluginDescription> found;
    for(int i=0;i<formats.getNumFormats();++i)formats.getFormat(i)->findAllTypesForFile(found,argv[1]);
    if(found.isEmpty())return 3;String error;auto plugin=formats.createPluginInstance(*found[0],48000,512,error);
    if(!plugin){std::cerr<<error;return 4;}plugin->enableAllBuses();auto layout=plugin->getBusesLayout();layout.inputBuses.set(0,AudioChannelSet::stereo());layout.outputBuses.set(0,AudioChannelSet::stereo());
    if(!plugin->setBusesLayout(layout))return 5;plugin->prepareToPlay(48000,512);
    auto parameter=[&](String name)->AudioProcessorParameter*{for(auto* p:plugin->getParameters())if(p->getName(128)==name)return p;return nullptr;};
    voicemixer::AutoTuneControls controls(*plugin);
    auto component=[&](String name)->Component*{for(auto* c:controls.getChildren())if(c->getComponentID()==name)return c;return nullptr;};
    auto* retune=dynamic_cast<Slider*>(component("Retune Speed"));auto* key=dynamic_cast<ComboBox*>(component("Key"));auto* modern=dynamic_cast<ComboBox*>(component("Modern Scale"));auto* legacy=dynamic_cast<ComboBox*>(component("Scale"));auto* classic=dynamic_cast<ToggleButton*>(component("Use Classic Mode DSP"));
    if(!retune||!key||!modern||!legacy||!classic||!parameter("Retune Speed"))return 6;
    std::atomic<bool> stop{false},invalid{false};std::atomic<int> blocks{0};
    std::thread audio([&]{
        AudioBuffer<float> buffer(jmax(2,plugin->getTotalNumInputChannels(),plugin->getTotalNumOutputChannels()),512);MidiBuffer midi;int64 sample=0;
        while(!stop){buffer.clear();for(int n=0;n<512;++n){const float x=.03f*std::sin(2*MathConstants<double>::pi*440*(sample+n)/48000);buffer.setSample(0,n,x);buffer.setSample(1,n,x);}plugin->processBlock(buffer,midi);sample+=512;
            for(int c=0;c<2;++c)for(int n=0;n<512;++n)if(!std::isfinite(buffer.getSample(c,n)))invalid=true;
            ++blocks;std::this_thread::sleep_for(std::chrono::microseconds(10667));
        }
    });
    bool failed=false;double worst=0;auto check=[&](bool good,const String& label){if(!good){failed=true;std::cerr<<"FAIL check: "<<label<<std::endl;}};
    {
        DocumentWindow window("Auto-Tune native controls test",Colours::darkgrey,DocumentWindow::closeButton);window.setUsingNativeTitleBar(true);window.setContentNonOwned(&controls,true);window.centreWithSize(window.getWidth(),window.getHeight());window.setVisible(true);
        for(int i=0;i<100;++i){
            const float wanted=parameter("Retune Speed")->getValueForText(i%2?"5":"35");
            const auto began=juce::Time::getMillisecondCounterHiRes();retune->setValue(wanted,sendNotificationSync);worst=jmax(worst,juce::Time::getMillisecondCounterHiRes()-began);
            check(std::abs(parameter("Retune Speed")->getValue()-wanted)<=.0001f,"retune update");
            MessageManager::getInstance()->runDispatchLoopUntil(10);
        }
        if(SystemStats::getEnvironmentVariable("VOICEMIXER_PRIVATE_POINTER_TEST","")=="1"){
            auto* display=XOpenDisplay(nullptr);void* xtst=dlopen("libXtst.so.6",RTLD_NOW);
            using Motion=int(*)(Display*,int,int,int,unsigned long);using Button=int(*)(Display*,unsigned int,int,unsigned long);
            auto motion=xtst?reinterpret_cast<Motion>(dlsym(xtst,"XTestFakeMotionEvent")):nullptr;
            auto button=xtst?reinterpret_cast<Button>(dlsym(xtst,"XTestFakeButtonEvent")):nullptr;
            if(!display||!motion||!button)failed=true;
            else{
                XRaiseWindow(display,(::Window)window.getPeer()->getNativeHandle());XSetInputFocus(display,(::Window)window.getPeer()->getNativeHandle(),RevertToParent,CurrentTime);
                retune->setValue(.5,sendNotificationSync);MessageManager::getInstance()->runDispatchLoopUntil(50);
                const auto bounds=retune->getScreenBounds();const int x=bounds.getCentreX(),y=bounds.getY()+40;
                motion(display,-1,x,y,CurrentTime);button(display,1,true,CurrentTime);XFlush(display);MessageManager::getInstance()->runDispatchLoopUntil(20);
                const float before=parameter("Retune Speed")->getValue();const auto began=juce::Time::getMillisecondCounterHiRes();
                motion(display,-1,x,y-40,CurrentTime);XFlush(display);MessageManager::getInstance()->runDispatchLoopUntil(60);
                const bool moved=std::abs(parameter("Retune Speed")->getValue()-before)>.001f;
                const auto elapsed=juce::Time::getMillisecondCounterHiRes()-began;
                button(display,1,false,CurrentTime);XFlush(display);MessageManager::getInstance()->runDispatchLoopUntil(20);
                if(!moved||elapsed>250)failed=true;
                std::cout<<"Real pointer drag: changed="<<moved<<"; observation="<<elapsed<<" ms"<<std::endl;
            }
            if(display)XCloseDisplay(display);if(xtst)dlclose(xtst);
        }
        key->setSelectedItemIndex(7,sendNotificationSync);check(parameter("Key")->getCurrentValueAsText()=="G","key "+parameter("Key")->getCurrentValueAsText());
        classic->setToggleState(false,sendNotificationSync);if(classic->onClick)classic->onClick();
        modern->setSelectedItemIndex(1,sendNotificationSync);check(parameter("Modern Scale")->getCurrentValueAsText()=="Major"&&modern->isVisible()&&!legacy->isVisible(),"modern scale "+parameter("Modern Scale")->getCurrentValueAsText());
        classic->setToggleState(true,sendNotificationSync);if(classic->onClick)classic->onClick();
        legacy->setSelectedItemIndex(1,sendNotificationSync);check(parameter("Scale")->getCurrentValueAsText()=="Minor"&&!modern->isVisible()&&legacy->isVisible(),"classic scale "+parameter("Scale")->getCurrentValueAsText());
        // External parameter changes are reflected by the native controls too.
        parameter("Key")->setValueNotifyingHost(2.f/11.f);MessageManager::getInstance()->runDispatchLoopUntil(100);check(key->getSelectedItemIndex()==2,"external key sync "+String(key->getSelectedItemIndex()));
        classic->setToggleState(false,sendNotificationSync);if(classic->onClick)classic->onClick();
        retune->setValue(parameter("Retune Speed")->getValueForText("5"),sendNotificationSync);
        MessageManager::getInstance()->runDispatchLoopUntil(100);
        MemoryBlock state;const auto began=juce::Time::getMillisecondCounterHiRes();plugin->getStateInformation(state);const auto saveMs=juce::Time::getMillisecondCounterHiRes()-began;
        if(state.isEmpty()||saveMs>1000||worst>250)failed=true;
        const auto savedRetune=parameter("Retune Speed")->getValue();parameter("Retune Speed")->setValueNotifyingHost(.9f);plugin->setStateInformation(state.getData(),int(state.getSize()));
        MessageManager::getInstance()->runDispatchLoopUntil(100);check(std::abs(parameter("Retune Speed")->getValue()-savedRetune)<=.001f,"state restore: wanted "+String(savedRetune)+" actual "+String(parameter("Retune Speed")->getValue()));
        MessageManager::getInstance()->runDispatchLoopUntil(100);
        FileOutputStream png{File(argv[2])};PNGImageFormat().writeImageToStream(controls.createComponentSnapshot(controls.getLocalBounds()),png);
        std::cout<<"Native controls: 100 retune changes; worst="<<worst<<" ms; state save="<<saveMs<<" ms; audio blocks="<<blocks.load()<<std::endl;
        window.setVisible(false);window.clearContentComponent();
    }
    stop=true;audio.join();if(invalid||blocks<20)failed=true;
    // The editor must be destroyed before the processor releases its resources.
    std::cout<<(failed?"FAIL":"PASS")<<": Auto-Tune native control integration"<<std::endl;
    return failed?7:0;
}
