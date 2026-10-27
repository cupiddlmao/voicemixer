// Small allocation-free LADSPA processors. All storage is allocated at instantiate.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <new>
#include <vector>
extern "C" {
typedef float LADSPA_Data;
typedef void* LADSPA_Handle;
typedef int LADSPA_PortDescriptor;
struct LADSPA_PortRangeHint { int HintDescriptor; float LowerBound,UpperBound; };
struct LADSPA_Descriptor {
    unsigned long UniqueID;const char* Label;int Properties;const char* Name;const char* Maker;const char* Copyright;
    unsigned long PortCount;const LADSPA_PortDescriptor* PortDescriptors;const char* const* PortNames;const LADSPA_PortRangeHint* PortRangeHints;void* ImplementationData;
    LADSPA_Handle (*instantiate)(const LADSPA_Descriptor*,unsigned long);
    void (*connect_port)(LADSPA_Handle,unsigned long,LADSPA_Data*);
    void (*activate)(LADSPA_Handle);void (*run)(LADSPA_Handle,unsigned long);
    void (*run_adding)(LADSPA_Handle,unsigned long);void (*set_run_adding_gain)(LADSPA_Handle,LADSPA_Data);
    void (*deactivate)(LADSPA_Handle);void (*cleanup)(LADSPA_Handle);
};
}
namespace {
struct Ramp {
    float value=0,target=0,delta=0;unsigned remaining=0;bool initialized=false;
    void set(float next,unsigned frames){
        if(!initialized){value=target=next;initialized=true;return;}
        if(next==target)return;
        target=next;remaining=std::max(1u,frames);delta=(target-value)/remaining;
    }
    float tick(){if(remaining){value+=delta;if(!--remaining)value=target;}return value;}
};
struct Comb {std::vector<float> samples;unsigned position=0;float damped=0;};
struct State {std::array<float*,18> ports{};std::array<Ramp,8> ramps{};unsigned rate,kind;float reduction=1;std::array<Comb,8> comb;std::array<std::vector<float>,2> echo;unsigned echoPosition=0;};
float finite(float v){return std::isfinite(v)?v:0;}
float control(State& s,int i,float fallback){return s.ports[i]?finite(*s.ports[i]):fallback;}
LADSPA_Handle instantiate(const LADSPA_Descriptor* d,unsigned long rate){auto* s=new(std::nothrow) State;if(s){s->rate=rate;s->kind=d->UniqueID-49010;
    if(s->kind==3){const int lengths[4]={1116,1188,1277,1356};try{for(int c=0;c<8;++c)s->comb[c].samples.resize(std::max(1ul,(lengths[c%4]+(c/4)*23)*rate/44100));for(auto& ring:s->echo)ring.resize(rate*2+1);}catch(...){delete s;return nullptr;}}
    }return s;}
void connectPort(LADSPA_Handle h,unsigned long port,float* data){if(port<18)static_cast<State*>(h)->ports[port]=data;}
void activate(LADSPA_Handle h){auto& s=*static_cast<State*>(h);s.ramps={};s.reduction=1;s.echoPosition=0;for(auto& line:s.comb){std::fill(line.samples.begin(),line.samples.end(),0);line.position=0;line.damped=0;}for(auto& ring:s.echo)std::fill(ring.begin(),ring.end(),0);}
void run(LADSPA_Handle h,unsigned long count){
    auto& s=*static_cast<State*>(h);const unsigned rampFrames=std::max(1u,s.rate/100); // 10 ms, sample-by-sample
    if(s.kind==0){
        s.ramps[0].set(std::clamp(control(s,2,1),0.f,16.f),rampFrames);
        for(unsigned long i=0;i<count;++i)s.ports[1][i]=finite(s.ports[0][i])*s.ramps[0].tick()+control(s,3,0);
    }else if(s.kind==1){
        for(int c=0;c<8;++c)s.ramps[c].set(std::clamp(control(s,9+c,0),0.f,4.f),rampFrames);
        for(unsigned long i=0;i<count;++i){float sum=0;for(int c=0;c<8;++c){const float gain=s.ramps[c].tick();if(s.ports[c])sum+=finite(s.ports[c][i])*gain;}s.ports[8][i]=finite(sum);}
    }else if(s.kind==2){
        const float ceiling=std::pow(10.f,std::clamp(control(s,4,-1),-12.f,0.f)/20);
        const float release=std::exp(-1.f/(.08f*s.rate));
        for(unsigned long i=0;i<count;++i){const float l=finite(s.ports[0][i]),r=finite(s.ports[1][i]);const float peak=std::max(std::abs(l),std::abs(r));
            const float wanted=peak>ceiling?ceiling/peak:1;
            s.reduction=wanted<s.reduction?wanted:std::min(wanted,1-(1-s.reduction)*release);
            s.ports[2][i]=std::clamp(l*s.reduction,-ceiling,ceiling);s.ports[3][i]=std::clamp(r*s.reduction,-ceiling,ceiling);
        }
    }else{
        s.ramps[3].set(std::clamp(control(s,6,.5),0.f,1.f),rampFrames);s.ramps[4].set(std::clamp(control(s,7,.35),0.f,.98f),rampFrames);
        s.ramps[5].set(std::clamp(control(s,9,.3),0.f,.85f),rampFrames);
        s.ramps[0].set(std::clamp(control(s,8,350),1.f,2000.f),rampFrames);
        s.ramps[1].set(std::clamp(control(s,10,1),0.f,4.f),rampFrames);s.ramps[2].set(std::clamp(control(s,11,1),0.f,4.f),rampFrames);
        const unsigned capacity=s.echo[0].size();
        for(unsigned long i=0;i<count;++i){const auto delay=std::clamp(unsigned(s.ramps[0].tick()*s.rate/1000),1u,capacity-1);const auto read=(s.echoPosition+capacity-delay)%capacity;const float reverbGain=s.ramps[1].tick(),delayGain=s.ramps[2].tick();
            const float feedback=.35f+.58f*s.ramps[3].tick(),damping=s.ramps[4].tick(),echoFeedback=s.ramps[5].tick();
            for(int channel=0;channel<2;++channel){const float input=finite(s.ports[channel][i]);float sum=0;
                for(int k=0;k<4;++k){auto& line=s.comb[channel*4+k];const float delayed=line.samples[line.position];line.damped=delayed*(1-damping)+line.damped*damping;if(std::abs(line.damped)<1e-20f)line.damped=0;line.samples[line.position]=std::clamp(input*.25f+line.damped*feedback,-32.f,32.f);if(++line.position==line.samples.size())line.position=0;sum+=delayed;}
                const float echo=s.echo[channel][read];s.echo[channel][s.echoPosition]=std::clamp(finite(s.ports[2+channel][i])+echo*echoFeedback,-32.f,32.f);
                s.ports[4+channel][i]=finite(sum*reverbGain+echo*delayGain);
            }
            if(++s.echoPosition==capacity)s.echoPosition=0;
        }

    }
}
void cleanup(LADSPA_Handle h){delete static_cast<State*>(h);}
const LADSPA_PortDescriptor gainPorts[]={9,10,5,5};
const char* const gainNames[]={"In","Out","Mult","Add"};
const LADSPA_PortRangeHint gainHints[]={{0,0,0},{0,0,0},{0x243,0,16},{0x203,-16,16}};
const LADSPA_PortDescriptor mixPorts[]={9,9,9,9,9,9,9,9,10,5,5,5,5,5,5,5,5};
const char* const mixNames[]={"In 1","In 2","In 3","In 4","In 5","In 6","In 7","In 8","Out","Gain 1","Gain 2","Gain 3","Gain 4","Gain 5","Gain 6","Gain 7","Gain 8"};
const LADSPA_PortRangeHint mixHints[]={{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0x243,0,4},{0x203,0,4},{0x203,0,4},{0x203,0,4},{0x203,0,4},{0x203,0,4},{0x203,0,4},{0x203,0,4}};
const LADSPA_PortDescriptor limiterPorts[]={9,9,10,10,5};
const char* const limiterNames[]={"In L","In R","Out L","Out R","Ceiling"};
const LADSPA_PortRangeHint limiterHints[]={{0,0,0},{0,0,0},{0,0,0},{0,0,0},{3,-12,0}};
const LADSPA_PortDescriptor fxPorts[]={9,9,9,9,10,10,5,5,5,5,5,5};
const char* const fxNames[]={"Reverb L","Reverb R","Delay L","Delay R","Out L","Out R","Room","Damping","Delay ms","Feedback","Reverb return","Delay return"};
const LADSPA_PortRangeHint fxHints[]={{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{3,0,1},{3,0,.98},{3,1,2000},{3,0,.85},{3,0,4},{3,0,4}};
const LADSPA_Descriptor descriptors[]={
 {49010,"smooth_gain",4,"Linux Mixer smooth gain","Linux Mixer","GPL-3.0-or-later",4,gainPorts,gainNames,gainHints,nullptr,instantiate,connectPort,activate,run,nullptr,nullptr,nullptr,cleanup},
 {49011,"smooth_mixer8",4,"Linux Mixer smooth mixer","Linux Mixer","GPL-3.0-or-later",17,mixPorts,mixNames,mixHints,nullptr,instantiate,connectPort,activate,run,nullptr,nullptr,nullptr,cleanup},
 {49012,"stereo_limiter",4,"Linux Mixer stereo peak limiter","Linux Mixer","GPL-3.0-or-later",5,limiterPorts,limiterNames,limiterHints,nullptr,instantiate,connectPort,activate,run,nullptr,nullptr,nullptr,cleanup},
 {49013,"stereo_fx",4,"Linux Mixer reverb and delay returns","Linux Mixer","GPL-3.0-or-later",12,fxPorts,fxNames,fxHints,nullptr,instantiate,connectPort,activate,run,nullptr,nullptr,nullptr,cleanup}
};
}
extern "C" const LADSPA_Descriptor* ladspa_descriptor(unsigned long index){return index<4?&descriptors[index]:nullptr;}
