// Opt-in third-party VST3 smoke test. No microphones, speakers or PipeWire graph.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <cmath>
#include <iostream>
#include <atomic>
#include <thread>
#include <chrono>
#include <X11/Xlib.h>
#include <dlfcn.h>
using namespace juce;
int main(int argc,char** argv){
    ScopedJuceInitialiser_GUI gui;if(argc<2||argc>3)return 2;const bool soak=argc==3&&String(argv[2])=="--soak";bool soakFailed=false;
    AudioPluginFormatManager formats;formats.addDefaultFormats();OwnedArray<PluginDescription> found;
    for(int i=0;i<formats.getNumFormats();++i)formats.getFormat(i)->findAllTypesForFile(found,argv[1]);
    if(found.isEmpty())return 3;
    std::cerr<<"PHASE create instance"<<std::endl;
    String error;auto plugin=formats.createPluginInstance(*found[0],48000,512,error);
    if(!plugin){std::cerr<<error;return 4;}
    if(argc==3&&String(argv[2])=="--parameters"){
        Array<var> items;
        for(auto* parameter:plugin->getParameters()){
            auto* item=new DynamicObject;item->setProperty("index",parameter->getParameterIndex());item->setProperty("name",parameter->getName(128));item->setProperty("label",parameter->getLabel());item->setProperty("steps",parameter->getNumSteps());item->setProperty("value",parameter->getValue());item->setProperty("text",parameter->getCurrentValueAsText());
            Array<var> values;const int steps=parameter->getNumSteps();if(steps>1&&steps<=128)for(int i=0;i<steps;++i)values.add(parameter->getText(float(i)/float(steps-1),128));item->setProperty("choices",values);items.add(var(item));
        }
        std::cout<<JSON::toString(var(items))<<std::endl;return 0;
    }
    std::cerr<<"PHASE configure buses"<<std::endl;
    auto layout=plugin->getBusesLayout();if(layout.inputBuses.isEmpty()||layout.outputBuses.isEmpty())return 5;
    // Keep auxiliary inputs enabled and supply silence to unconnected buses.
    for(int i=1;i<layout.outputBuses.size();++i)layout.outputBuses.set(i,AudioChannelSet::disabled());
    layout.inputBuses.set(0,AudioChannelSet::stereo());layout.outputBuses.set(0,AudioChannelSet::stereo());
    if(!plugin->setBusesLayout(layout))return 6;
    std::cerr<<"PHASE prepare stereo audio"<<std::endl;plugin->prepareToPlay(48000,512);
    std::cerr<<"PHASE process synthetic audio"<<std::endl;
    AudioBuffer<float> audio(std::max(2,plugin->getTotalNumInputChannels()),512);MidiBuffer midi;double energy=0;int count=0;
    for(int b=0;b<300;++b){
        audio.clear();
        for(int n=0;n<512;++n){const auto value=.05f*std::sin(2*3.141592653589793*440*(b*512+n)/48000);audio.setSample(0,n,value);audio.setSample(1,n,value);}
        plugin->processBlock(audio,midi);
        for(int c=0;c<2;++c)for(int n=0;n<512;++n){const auto value=audio.getSample(c,n);if(!std::isfinite(value)||std::abs(value)>8)return 7;if(b>100){energy+=value*value;++count;}}
    }
    const double rms=std::sqrt(energy/std::max(1,count));if(rms<.00001)return 8;
    std::cerr<<"PHASE save and restore state; RMS="<<rms<<std::endl;
    MemoryBlock state;plugin->getStateInformation(state);if(state.isEmpty())return 9;plugin->setStateInformation(state.getData(),int(state.getSize()));
    std::cerr<<"PHASE open original editor"<<std::endl;
    if(!plugin->hasEditor())return 10;
    auto editor=std::unique_ptr<AudioProcessorEditor>(plugin->createEditorIfNeeded());if(!editor||editor->getWidth()<=0||editor->getHeight()<=0)return 11;
    {
        DocumentWindow window("Plugin editor smoke test",Colours::darkgrey,DocumentWindow::closeButton);
        window.setUsingNativeTitleBar(true);window.setContentNonOwned(editor.get(),true);window.centreWithSize(window.getWidth(),window.getHeight());window.setVisible(true);
        if(soak){
            std::atomic<bool> stop{false},invalid{false};
            std::thread processing([&]{
                AudioBuffer<float> live(jmax(2,plugin->getTotalNumInputChannels(),plugin->getTotalNumOutputChannels()),512);MidiBuffer events;int64 frame=0;
                while(!stop){
                    live.clear();for(int n=0;n<512;++n){const float x=.05f*std::sin(2*3.141592653589793*440*(frame+n)/48000);live.setSample(0,n,x);live.setSample(1,n,x);}
                    plugin->processBlock(live,events);frame+=512;
                    for(int c=0;c<2;++c)for(int n=0;n<512;++n)if(!std::isfinite(live.getSample(c,n)))invalid=true;
                    std::this_thread::sleep_for(std::chrono::microseconds(10667));
                }
            });
            // Optional pointer test is restricted to the private Xvfb test display.
            Display* pointerDisplay=nullptr;void* xtst=nullptr;
            using Motion=int(*)(Display*,int,int,int,unsigned long);using Button=int(*)(Display*,unsigned int,int,unsigned long);
            Motion motion=nullptr;Button button=nullptr;double sent=0;float prior=0;bool received=false;
            if(SystemStats::getEnvironmentVariable("VOICEMIXER_PRIVATE_POINTER_TEST","")=="1"){
                pointerDisplay=XOpenDisplay(nullptr);xtst=dlopen("libXtst.so.6",RTLD_NOW);
                if(xtst){motion=reinterpret_cast<Motion>(dlsym(xtst,"XTestFakeMotionEvent"));button=reinterpret_cast<Button>(dlsym(xtst,"XTestFakeButtonEvent"));}
            }
            AudioProcessorParameter* retune=nullptr;for(auto* p:plugin->getParameters())if(p->getName(128).containsIgnoreCase("retune")){retune=p;break;}
            for(int tick=0;tick<600;++tick){
                if(!pointerDisplay&&retune&&tick%5==0)retune->setValueNotifyingHost(float(tick%100)/100.f);
                if(pointerDisplay&&motion&&button&&retune){
                    const auto bounds=editor->getScreenBounds();
                    if(tick==30)retune->setValueNotifyingHost(.5f);
                    if(tick==50){
                        prior=retune->getValue();motion(pointerDisplay,-1,bounds.getX()+106,bounds.getY()+285,CurrentTime);button(pointerDisplay,1,true,CurrentTime);XFlush(pointerDisplay);
                    }
                    if(tick==55){sent=juce::Time::getMillisecondCounterHiRes();motion(pointerDisplay,-1,bounds.getX()+106,bounds.getY()+235,CurrentTime);XFlush(pointerDisplay);}
                    if(tick==70){button(pointerDisplay,1,false,CurrentTime);XFlush(pointerDisplay);}
                    if(sent&&!received&&std::abs(retune->getValue()-prior)>.001f){received=true;std::cerr<<"Pointer response: "<<int(juce::Time::getMillisecondCounterHiRes()-sent)<<" ms; retune="<<retune->getCurrentValueAsText()<<std::endl;}
                }
                const auto dispatchBegan=juce::Time::getMillisecondCounterHiRes();
                MessageManager::getInstance()->runDispatchLoopUntil(20);
                const auto dispatchElapsed=juce::Time::getMillisecondCounterHiRes()-dispatchBegan;
                if(dispatchElapsed>250)std::cerr<<"UI dispatch stall: "<<int(dispatchElapsed)<<" ms at tick "<<tick<<std::endl;
                if(tick==200||tick==400){
                    MemoryBlock snapshot;const auto began=juce::Time::getMillisecondCounterHiRes();plugin->getStateInformation(snapshot);const auto elapsed=juce::Time::getMillisecondCounterHiRes()-began;
                    std::cerr<<"Concurrent audio/editor/state save: "<<int(elapsed)<<" ms, "<<snapshot.getSize()<<" bytes"<<std::endl;
                    if(snapshot.isEmpty()||elapsed>2000)soakFailed=true;
                }
            }
            if(pointerDisplay){if(!received){std::cerr<<"Pointer test: no parameter response"<<std::endl;soakFailed=true;}XCloseDisplay(pointerDisplay);}if(xtst)dlclose(xtst);
            stop=true;processing.join();if(invalid)soakFailed=true;
        }else MessageManager::getInstance()->runDispatchLoopUntil(1500);
        const File framebuffer("/work/Xvfb_screen0");if(framebuffer.existsAsFile())framebuffer.copyFileTo(File("/work/editor-screen.xwd"));
        window.setVisible(false);window.clearContentComponent();
    }
    std::cout<<(soakFailed?"FAIL: ":"PASS: ")<<found[0]->name<<" "<<found[0]->version<<"; stereo synthetic processing RMS="<<rms<<"; latency="<<plugin->getLatencySamples()<<" samples; parameters="<<plugin->getParameters().size()<<"; state="<<state.getSize()<<" bytes; original editor="<<editor->getWidth()<<"x"<<editor->getHeight()<<". Mouse/keyboard interaction is not verified by this test."<<std::endl;
    editor.reset();plugin->releaseResources();return soakFailed?12:0;
}
