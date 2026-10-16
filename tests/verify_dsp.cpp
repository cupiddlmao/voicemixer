// Process the actual LADSPA exports with discontinuities, overload and invalid input.
#include "../native/dsp_ladspa.cpp"
#include <cassert>
#include <iostream>
#include <limits>
#include <vector>
int main(){
    const auto* gain=ladspa_descriptor(0);auto g=gain->instantiate(gain,48000);
    std::vector<float> in(4800,.5),out(4800);float mult=1,add=0;
    gain->connect_port(g,0,in.data());gain->connect_port(g,1,out.data());gain->connect_port(g,2,&mult);gain->connect_port(g,3,&add);gain->activate(g);gain->run(g,64);
    assert(out[0]==.5f);mult=0;gain->run(g,4800);assert(out[0]<.5f&&out[0]>.49f);assert(out[479]==0&&out.back()==0);
    for(int i=1;i<480;++i)assert(std::abs(out[i]-out[i-1])<.0011f);
    mult=1;gain->run(g,240);mult=.2;gain->run(g,4800);assert(std::abs(out.back()-.1f)<1e-6);gain->cleanup(g);
    const auto* mix=ladspa_descriptor(1);auto m=mix->instantiate(mix,48000);std::array<float,8> values{};
    for(int i=0;i<8;++i){mix->connect_port(m,i,in.data());mix->connect_port(m,9+i,&values[i]);}mix->connect_port(m,8,out.data());mix->activate(m);mix->run(m,1);
    values[7]=1;mix->run(m,4800);assert(out[0]>.0&&out[0]<.002);assert(out.back()==.5f);mix->cleanup(m);
    const auto* limiter=ladspa_descriptor(2);auto l=limiter->instantiate(limiter,48000);
    std::vector<float> left(4800),right(4800),ol(4800),orr(4800);float ceiling=-1;
    for(int i=0;i<4800;++i){left[i]=4*std::sin(i*.1);right[i]=left[i]*.25;}
    left[900]=std::numeric_limits<float>::quiet_NaN();right[1900]=std::numeric_limits<float>::infinity();
    limiter->connect_port(l,0,left.data());limiter->connect_port(l,1,right.data());limiter->connect_port(l,2,ol.data());limiter->connect_port(l,3,orr.data());limiter->connect_port(l,4,&ceiling);limiter->activate(l);limiter->run(l,4800);
    for(int i=0;i<4800;++i){assert(std::isfinite(ol[i])&&std::isfinite(orr[i]));assert(std::abs(ol[i])<=.8912511f&&std::abs(orr[i])<=.8912511f);if(i!=900&&i!=1900)assert(std::abs(orr[i]-ol[i]*.25)<1e-6);}
    limiter->cleanup(l);
    const auto* fx=ladspa_descriptor(3);auto f=fx->instantiate(fx,48000);assert(f);
    std::vector<float> impulse(48000),zero(48000),wetL(48000),wetR(48000);impulse[0]=.5;
    float room=.5,damping=0,delayMs=50,feedback=.5,verbReturn=0,delayReturn=1;
    for(int c=0;c<2;++c){fx->connect_port(f,c,zero.data());fx->connect_port(f,c+2,c==0?impulse.data():zero.data());fx->connect_port(f,c+4,c==0?wetL.data():wetR.data());}
    std::array<float*,6> controls{&room,&damping,&delayMs,&feedback,&verbReturn,&delayReturn};for(int c=0;c<6;++c)fx->connect_port(f,c+6,controls[c]);fx->activate(f);fx->run(f,48000);
    assert(std::abs(wetL[2400]-.5f)<1e-6&&std::abs(wetL[4800]-.25f)<1e-6);for(float v:wetR)assert(v==0);
    fx->connect_port(f,0,impulse.data());fx->connect_port(f,2,zero.data());verbReturn=1;delayReturn=0;fx->activate(f);fx->run(f,48000);double tail=0;for(int i=1000;i<20000;++i){assert(std::isfinite(wetL[i]));tail+=wetL[i]*wetL[i];}assert(tail>.001);fx->cleanup(f);
    std::cout<<"PASS: 10 ms gain/send fades, interrupted fades, linked stereo limiting, -1 dBFS ceiling non-finite protection, exact echo timing/feedback, reverb tail and stereo separation.\n";
}
