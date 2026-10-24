// Opt-in integration check; uses the user's installed Wider, never a real mic.
#include "../native/plugins/wider_controls.h"
#include <iostream>
#include <cmath>
using namespace juce;
int main(int argc,char** argv){
 ScopedJuceInitialiser_GUI gui;if(argc!=3)return 2;
 AudioPluginFormatManager formats;formats.addDefaultFormats();OwnedArray<PluginDescription> found;
 for(int i=0;i<formats.getNumFormats();++i)formats.getFormat(i)->findAllTypesForFile(found,argv[1]);
 if(found.isEmpty())return 3;String error;auto plugin=formats.createPluginInstance(*found[0],48000,512,error);if(!plugin){std::cerr<<error;return 4;}
 auto layout=plugin->getBusesLayout();layout.inputBuses.set(0,AudioChannelSet::stereo());layout.outputBuses.set(0,AudioChannelSet::stereo());plugin->setBusesLayout(layout);plugin->prepareToPlay(48000,512);
 voicemixer::WiderControls controls(*plugin);Slider* width=nullptr;
 for(auto* c:controls.getChildren())if(auto* s=dynamic_cast<Slider*>(c))if(s->getMaximum()==200)width=s;
 if(!width)return 5;
 auto energy=[&]{double diff=0,total=0;AudioBuffer<float> audio(2,512);MidiBuffer midi;for(int b=0;b<100;++b){for(int n=0;n<512;++n){float x=.03f*std::sin(2*3.141592653589793*777*(b*512+n)/48000);audio.setSample(0,n,x);audio.setSample(1,n,x);}plugin->processBlock(audio,midi);if(b>20)for(int n=0;n<512;++n){float l=audio.getSample(0,n),r=audio.getSample(1,n);diff+=(l-r)*(l-r);total+=l*l+r*r;}}return diff/total;};
 width->setValue(-200,sendNotificationSync);double zero=energy();if(width->getValue()!=0||zero>.00001)return 6;
 width->setValue(100,sendNotificationSync);double wide=energy();if(std::abs(plugin->getParameters()[0]->getValue()-.5f)>.001||wide<.1)return 7;
 width->setValue(300,sendNotificationSync);energy();if(width->getValue()!=200||std::abs(plugin->getParameters()[0]->getValue()-1.f)>.001)return 8;
 width->setValue(100,sendNotificationSync);energy();MemoryBlock state;plugin->getStateInformation(state);if(state.isEmpty())return 9;
 auto restored=formats.createPluginInstance(*found[0],48000,512,error);restored->setStateInformation(state.getData(),int(state.getSize()));if(std::abs(restored->getParameters()[0]->getValue()-.5f)>.001)return 10;
 Image image=controls.createComponentSnapshot(controls.getLocalBounds());FileOutputStream png{File(argv[2])};PNGImageFormat().writeImageToStream(image,png);
 std::cout<<"PASS: native Width slider changes real Windows Wider DSP, clamps to 0–200%, generates stereo width, and restores 100% from saved state. Ratio dry="<<zero<<" widened="<<wide<<std::endl;
 restored.reset();plugin->releaseResources();return 0;
}
